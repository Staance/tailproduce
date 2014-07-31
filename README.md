## Setup, checkout, build and run tests

In Ubuntu 14.04.

```
sudo apt-get update
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get update
sudo apt-get install -y git binutils make cmake gcc-4.8 g++-4.8 libgtest-dev libgoogle-glog-dev libboost1.55-dev libboost-system1.55-dev libboost-filesystem1.55-dev clang-3.5 clang-format-3.5 libsnappy-dev
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 20
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 20
sudo update-alternatives --config gcc
sudo update-alternatives --config g++
(cd /usr/src/gtest; sudo cmake . ; sudo make)
git clone https://github.com/dkorolev/tailproduce.git
cd tailproduce
make love
```

## Investigating pull request indexing

This change would either make it as pull request 1 or as pull request 29.
