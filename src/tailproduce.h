// TODO(dkorolev): Perhaps we should move StreamsRegistry to become the part of the StreamManager base class?

#ifndef TAILPRODUCE_H
#define TAILPRODUCE_H

#include <vector>
#include <set>
#include <string>
#include <type_traits>
#include <mutex>
#include <sstream>

#include <glog/logging.h>

#include "cereal/archives/binary.hpp"
#include "cereal/archives/json.hpp"

#include "cereal/types/string.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/map.hpp"

#include "cereal/types/polymorphic.hpp"

#include "helpers.h"

namespace TailProduce {
    class Stream;

    class StreamsRegistry {
      public:
        struct StreamsRegistryEntry {
            // The pointer to an instance of the stream is owned by TailProduce framework.
            // * For static frameworks (streams list is fully available at compile time),
            //   these pointers point to existing, statically initialized, members
            //   of the instance of cover TailProduce class.
            // * For dynamic frameworks, they point to dynamically allocated instances
            //   of per-stream implementations, which are also owned by the instance
            //   of the cover TailProduce class.
            const Stream* impl;
            std::string name;
            std::string entry_type;
            std::string order_key_type;
        };
        std::vector<StreamsRegistryEntry> streams;
        std::set<std::string> names;

        void Add(TailProduce::Stream* impl,
                 const std::string& name,
                 const std::string& entry_type,
                 const std::string& order_key_type) {
            if (names.find(name) != names.end()) {
                LOG(FATAL) << "Attempted to register the '" << name << "' stream more than once.";
            }
            names.insert(name);
            streams.push_back(StreamsRegistryEntry{impl, name, entry_type, order_key_type});
        }
    };

    struct Stream {
        Stream(StreamsRegistry& registry,
               const std::string& stream_name,
               const std::string& entry_type_name,
               const std::string& order_key_type_name) {
            registry.Add(this, stream_name, entry_type_name, order_key_type_name);
        }
    };

    // A serializable entry.
    template<typename T1, typename T2> struct OrderKeyExtractorImpl {};
    struct Entry {
        // Need the following fully specialized template within namespace ::TailProduce:
        //     template<> struct OrderKeyExtractorImpl<OrderKeyType, EntryType> {
        //         static OrderKeyType ExtractOrderKey(const EntryType& entry) { ... }
        //      };
        // for each desired pair of { OrderKeyType, EntryType }.
        // template<typename T> static void SerializeEntry(std::ostream& os, const T& entry);
        // template<typename T> static void DeSerializeEntry(std::istream& is, T& entry);
    };

    // An interface to extract order keys in certain types. With fixed-size serialization.
    struct OrderKey {
        // enum { size_in_bytes = 0 };  // TO GO AWAY -- D.K.
        // bool operator<(const T& rhs) const;
        // void SerializeOrderKey(uint8_t* ptr) const;
        // void DeSerializeOrderKey(const uint8_t* ptr);
        template<typename T_ORDER_KEY> static void StaticAppendAsStorageKey(const T_ORDER_KEY& primary_key,
                                                                            const uint32_t secondary_key,
                                                                            std::vector<uint8_t>& output) {
            using TOK = ::TailProduce::OrderKey;
            static_assert(std::is_base_of<TOK, T_ORDER_KEY>::value,
                          "StreamManager::T_ORDER_KEY should be derived from OrderKey.");
            static_assert(T_ORDER_KEY::size_in_bytes > 0,
                          "StreamManager::T_ORDER_KEY::size_in_bytes should be positive.");
            uint8_t result[T_ORDER_KEY::size_in_bytes + 1 + 11];
            primary_key.SerializeOrderKey(result);
            result[T_ORDER_KEY::size_in_bytes] = ':';
            snprintf(reinterpret_cast<char*>(result + T_ORDER_KEY::size_in_bytes + 1), 11, "%010u", secondary_key);
            std::copy(result, result + sizeof(result) - 1, std::back_inserter(output));
        }
        template<typename T_ORDER_KEY> static std::vector<uint8_t> StaticSerializeAsStorageKey(
                const T_ORDER_KEY& primary_key,
                const uint32_t secondary_key) {
            std::vector<uint8_t> output;
            StaticAppendAsStorageKey<T_ORDER_KEY>(primary_key, secondary_key, output);
            return output;
        }
    };
    struct Storage {};   // Data storage proxy, originally LevelDB.
    struct Producer {};  // Client-defined job.

    // Cereal-based serialization.
    template<typename T> struct CerealJSONSerializable {
       static void SerializeEntry(std::ostream& os, const T& entry) {
           (cereal::JSONOutputArchive(os))(entry);
           os << std::endl;
       }
       static void DeSerializeEntry(std::istream& is, T& entry) {
           cereal::JSONInputArchive ar(is);
           ar(entry);
       }
    };

    template<typename T_ENTRY, typename T_ORDER_KEY> class StreamInstance : public Stream {
      public:
        typedef T_ENTRY ENTRY_TYPE;
        typedef T_ORDER_KEY ORDER_KEY_TYPE;
        StreamInstance(
            TailProduce::StreamsRegistry& registry,
            const std::string& stream_name,
            const std::string& entry_type_name,
            const std::string& order_key_type_name)
            : TailProduce::Stream(registry,
                                  stream_name,
                                  entry_type_name,
                                  order_key_type_name) {
            using TE = ::TailProduce::Entry;
            static_assert(std::is_base_of<TE, T_ENTRY>::value,
                          "StreamInstance::T_ENTRY should be derived from Entry.");
            using TOK = ::TailProduce::OrderKey;
            static_assert(std::is_base_of<TOK, T_ORDER_KEY>::value,
                          "StreamInstance::T_ORDER_KEY should be derived from OrderKey.");
            static_assert(T_ORDER_KEY::size_in_bytes > 0,
                          "StreamInstance::T_ORDER_KEY::size_in_bytes should be positive.");
        }
    };

    struct StreamManager {};

    struct Exception : std::exception {};
    struct InternalError : Exception {};
    struct OrderKeysGoBackwardsException : Exception {};
    struct ListenerHasNoDataToRead : Exception {};
    struct AttemptedToAdvanceListenerWithNoDataAvailable : Exception {};

    // StorageKeyBuilder implements the BuildStorageKey function to convert
    // { stream name, typed order key, secondary key } into std::vector<uint8_t>-s.
    template<typename T> struct StorageKeyBuilder {
        explicit StorageKeyBuilder(const std::string& stream_name)
          : prefix(bytes("d:" + stream_name + ":")),
            end_stream_key(bytes("d:" + stream_name + ":\xff")) {
        }
        std::vector<uint8_t> BuildStorageKey(const typename T::head_pair_type& key) const {
            std::vector<uint8_t> storage_key = prefix;
            OrderKey::template StaticAppendAsStorageKey<typename T::order_key_type>(key.first, key.second, storage_key);
            return storage_key;
        }
        const std::vector<uint8_t> end_stream_key;
        StorageKeyBuilder() = delete;
        StorageKeyBuilder(const StorageKeyBuilder&) = delete;
        StorageKeyBuilder(StorageKeyBuilder&&) = delete;
        void operator=(const StorageKeyBuilder&) = delete;
        const std::vector<uint8_t> prefix;
    };

    // UnsafeListener contains the logic of creating and re-creating storage-level read iterators,
    // presenting data in serialized format and keeping track of HEAD order keys.
    template<typename T> struct UnsafeListener : StorageKeyBuilder<T> {
        typedef StorageKeyBuilder<T> key_builder;
        UnsafeListener() = delete;

        // Unbounded.
        UnsafeListener(const T& stream, const typename T::head_pair_type& begin = typename T::head_pair_type())
          : key_builder(stream.name),
            stream(stream),
            storage(stream.manager->storage),
            cursor_key(key_builder::BuildStorageKey(begin)),
            need_to_increment_cursor(false),
            has_end_key(true),
            reached_end(false) {
        }
        UnsafeListener(const T& stream, const typename T::order_key_type& begin)
          : UnsafeListener(stream, std::make_pair(begin, 0)) {
        }

        // Bounded.
        UnsafeListener(const T& stream,
                       const typename T::head_pair_type& begin,
                       const typename T::head_pair_type& end)
          : key_builder(stream.name),
            stream(stream),
            storage(stream.manager->storage),
            cursor_key(key_builder::BuildStorageKey(begin)),
            need_to_increment_cursor(false),
            has_end_key(true),
            end_key(key_builder::BuildStorageKey(end)),
            reached_end(false) {
        }
        UnsafeListener(const T& stream,
                       const typename T::order_key_type& begin,
                       const typename T::order_key_type& end)
          : UnsafeListener(stream, std::make_pair(begin, 0),
            std::make_pair(end, 0)) {
        }

        UnsafeListener(UnsafeListener&&) = default;
        
        const typename T::head_pair_type& GetHead() const {
            return stream.head;
        }

        // Note that listeners expose HasData() / ReachedEnd(), and not Done().
        // This is because the listener, unlike the iterator, supports dynamically added data,
        // and, therefore, the standard `for (auto i = Iterator(); !i.Done(); i.Next())` loop is meaningless.

        // HasData() returns true if more data is available.
        // Can change from false to true if/when new data is available.
        bool HasData() const {
            if (reached_end) {
                return false;
            } else {
                if (!iterator) {
                    iterator.reset(new iterator_type(storage, cursor_key, key_builder::end_stream_key));
                    if (need_to_increment_cursor && !iterator->Done()) {
                        iterator->Next();
                    }
                }
                if (iterator->Done()) {
                    iterator.reset(nullptr);
                    return false;
                }
                assert(iterator && !iterator->Done());
                if (has_end_key && iterator->Key() >= end_key) {
                    reached_end = true;
                    iterator.reset(nullptr);
                    return false;
                } else {
                    // TODO(dkorolev): Handle HEAD going beyond end_key resulting in ReachedEnd().
                    return true;
                }
            }
        }

        // ReachedEnd() returns true if the end has been reached and no data may even be read from this iterator.
        // Can only happen if the iterator has a fixed `end`, it has been reached and the HEAD of this stream
        // is beyond this end.
        bool ReachedEnd() const {
            HasData();
            return reached_end;
        }

        // ExportEntry() populates the passed in entry object if data is available.
        // Will throw an exception if no data is available.
        void ExportEntry(typename T::entry_type& entry) {
            if (!HasData()) {
                throw ::TailProduce::ListenerHasNoDataToRead();
            }
            if (!iterator) {
                throw ::TailProduce::InternalError();
            }
            // TODO(dkorolev): Make this proof-of-concept code efficient.
            const std::vector<uint8_t> value = iterator->Value();
            const std::string value_as_string(value.begin(), value.end());
            std::istringstream is(value_as_string);
            T::entry_type::DeSerializeEntry(is, entry);
        }

        // AdvanceToNextEntry() advances the listener to the next available entry.
        // Will throw an exception if no further data is (yet) available.
        void AdvanceToNextEntry() {
            if (!HasData()) {
                throw ::TailProduce::AttemptedToAdvanceListenerWithNoDataAvailable();
            }
            if (!iterator) {
                throw ::TailProduce::InternalError();
            }
            cursor_key = iterator->Key();
            need_to_increment_cursor = true;
            iterator->Next();
        }

      private:
        typedef typename T::storage_type storage_type;
        typedef typename T::storage_type::Iterator iterator_type;
        UnsafeListener(const UnsafeListener&) = delete;
        void operator=(const UnsafeListener&) = delete;
        const T& stream;
        storage_type& storage;
        std::vector<uint8_t> cursor_key;
        bool need_to_increment_cursor;
        const bool has_end_key;
        const std::vector<uint8_t> end_key;
        mutable bool reached_end;
        mutable std::unique_ptr<iterator_type> iterator;
    };

    // UnsafePublisher contains the logic of appending data to the streams and updating their HEAD order keys.
    template<typename T> struct UnsafePublisher : StorageKeyBuilder<T> {
        typedef StorageKeyBuilder<T> key_builder;
        UnsafePublisher() = delete;
        explicit UnsafePublisher(T& stream)
          : key_builder(stream.name),
            stream(stream) {
        }

        UnsafePublisher(T& stream, const typename T::order_key_type& order_key)
          : key_builder(stream.name),
            stream(stream) {
            PushHead(order_key);
        }

        void Push(const typename T::entry_type& entry) {
            typedef ::TailProduce::OrderKeyExtractorImpl<typename T::order_key_type, typename T::entry_type> impl;
            PushHead(impl::ExtractOrderKey(entry));
            std::ostringstream value_output_stream;
            T::entry_type::SerializeEntry(value_output_stream, entry);
            auto k = key_builder::BuildStorageKey(stream.head);
            stream.manager->storage.Set(key_builder::BuildStorageKey(stream.head), bytes(value_output_stream.str()));
        }

        void PushHead(const typename T::order_key_type& order_key) {
            typename T::head_pair_type new_head(order_key, 0);
            if (new_head.first < stream.head.first) {
                // Order keys should only be increasing.
                throw ::TailProduce::OrderKeysGoBackwardsException();
            }
            if (!(stream.head.first < new_head.first)) {
                new_head.second = stream.head.second + 1;
            }
            // TODO(dkorolev): Perhaps more checks here?
            stream.manager->storage.SetAllowingOverwrite(
                stream.head_storage_key,
                OrderKey::template StaticSerializeAsStorageKey<typename T::order_key_type>(new_head.first,
                                                                                           new_head.second));
            stream.head = new_head;
        }

        // TODO: PushSecondaryKey for merge usecases.

        const typename T::head_pair_type& GetHead() const {
            return stream.head;
        }

      private:
        UnsafePublisher(const UnsafePublisher&) = delete;
        void operator=(const UnsafePublisher&) = delete;
        T& stream;
    };
};

/*
// TailProduce static framework macros.
// Static framework is the one that lists all the streams in the source file,
// thus allowing all C++ template and static typing powers to come into play.
#define TAILPRODUCE_STATIC_FRAMEWORK_BEGIN(name, base) \
    class name : public base { \
      private: \
        ::TailProduce::StreamsRegistry registry_;  \
      public: \
        const ::TailProduce::StreamsRegistry& registry() const { return registry_; }

#define TAILPRODUCE_STREAM(name, entry_type, order_key_type) \
        typedef ::TailProduce::StreamInstance<entry_type, order_key_type> STREAM_TYPE_##name; \
        STREAM_TYPE_##name name = STREAM_TYPE_##name(registry_, #name, #entry_type, #order_key_type)

#define TAILPRODUCE_STATIC_FRAMEWORK_END() \
    }
*/

#endif  // TAILPRODUCE_H
