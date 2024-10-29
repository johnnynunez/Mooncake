# ALinx&Centos编译

## 建议版本

- cmake: 3.22.x
- boost-devel: 1.66.x
- grpc: 1.27.x
- googletest: 1.12.x
- gcc: 10.2.1

## 编译步骤

注意：以下安装顺序不可调整

1. 通过系统源下载安装下列第三方库
```
yum install cmake \
            gflags-devel \
            glog-devel \
            libibverbs-devel \
            numactl-devel \
            gtest \
            gtest-devel \
            boost-devel \
            openssl-devel \
            protobuf-devel \
            protobuf-compiler 
```

注意：如果源没有gtest, glog, gflags, 则需要通过源码安装

```
git clone https://github.com/gflags/gflags
git clone https://github.com/google/glog
git clone https://github.com/abseil/googletest.git
```

2. 安装grpc（v1.27.x）
注意：编译时需要加上`-DgRPC_SSL_PROVIDER=package`
```
git clone https://github.com/grpc/grpc.git --depth 1 --branch v1.27.x
cd grpc/
git submodule update --init
mkdir cmake-build
cd cmake-build/
cmake .. -DBUILD_SHARED_LIBS=ON \
        -DgRPC_INSTALL=ON \
        -DgRPC_BUILD_TESTS=OFF \
        -DgRPC_BUILD_CSHARP_EXT=OFF \
        -DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_NODE_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_PHP_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=OFF \
        -DgRPC_BUILD_GRPC_RUBY_PLUGIN=OFF \
        -DgRPC_BACKWARDS_COMPATIBILITY_MODE=ON \
        -DgRPC_ZLIB_PROVIDER=package \
        -DgRPC_SSL_PROVIDER=package
make -j`nproc`
make install
```
若`git submodule update --init`失败，请检查网络


3. 安装cpprestsdk
```
 git clone https://github.com/microsoft/cpprestsdk.git
 cd cpprestsdk
 mkdir build && cd build
 cmake .. -DCPPREST_EXCLUDE_WEBSOCKETS=ON
 make -j$(nproc) && make install
```

4. 安装etcd-cpp-apiv3
```
git clone https://github.com/etcd-cpp-apiv3/etcd-cpp-apiv3.git
cd etcd-cpp-apiv3
mkdir build && cd build
cmake ..
make -j$(nproc)
make install
```
注意：如若遇到以下类似的报错: `/usr/local/bin/grpc_cpp_plugin error while loading shared libraries: libprotoc.so.3.11.2.0: cannot open shared object file: No such file or directory`

则首先需要找到`libprotoc.so.3.11.2.0`的位置，比如: `/usr/local/lib64`, 再将该目录加入到`LD_LIBRARY_PATH`，如下: 
```
echo $LD_LIBRARY_PATH
export LD_LIBRARY_PATH=/usr/local/lib/:/usr/local/lib64/
```

5. 编译mooncake
```
mkdir build
cd build
cmake .. 
make -j
```
