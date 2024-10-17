#!/bin/bash

REPO_ROOT=`pwd`
GITHUB_PROXY="https://mirror.ghproxy.com/github.com"

sudo apt-get install -y build-essential \
                        libibverbs-dev \
                        libgoogle-glog-dev \
                        libgtest-dev \
                        libjsoncpp-dev \
                        libnuma-dev \
                        libpython3-dev \
                        libboost-all-dev \
                        libssl-dev \
                        libgrpc-dev \
                        libgrpc++-dev \
                        libprotobuf-dev \
                        protobuf-compiler-grpc \
                        pybind11-dev

mkdir ${REPO_ROOT}/thirdparties
cd ${REPO_ROOT}/thirdparties
git clone ${GITHUB_PROXY}/microsoft/cpprestsdk.git
cd cpprestsdk
mkdir build && cd build
cmake .. -DCPPREST_EXCLUDE_WEBSOCKETS=ON
make -j$(nproc) && sudo make install

cd ${REPO_ROOT}/thirdparties
git clone ${GITHUB_PROXY}/etcd-cpp-apiv3/etcd-cpp-apiv3.git
cd etcd-cpp-apiv3
mkdir build && cd build
cmake ..
make -j$(nproc) && sudo make install

echo "*** Dependencies Installed! ***"
