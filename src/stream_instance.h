#ifndef STREAMINSTANCE_H
#define STREAMINSTANCE_H

#include <string>
#include <type_traits>

#include "entry.h"
#include "order_key.h"
#include "streams_registry.h"
#include "storage.h"
#include "stream.h"
#include "config_values.h"

namespace TailProduce {
    template <typename T_ENTRY, typename T_ORDER_KEY> class StreamInstance : public Stream<T_ORDER_KEY> {
      public:
        typedef T_ENTRY ENTRY_TYPE;
        typedef T_ORDER_KEY ORDER_KEY_TYPE;
        StreamInstance(TailProduce::StreamsRegistry& registry,
                       TailProduce::ConfigValues& cv,
                       const std::string& stream_name,
                       const std::string& entry_type_name,
                       const std::string& order_key_type_name)
            : Stream<T_ORDER_KEY>(registry, cv, stream_name, entry_type_name, order_key_type_name) {
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
};

#endif
