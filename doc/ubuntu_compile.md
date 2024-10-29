# Ubuntu编译

1. 通过系统源下载安装下列第三方库
   ```bash
   apt-get install -y build-essential \
                      cmake \
                      libibverbs-dev \
                      libgoogle-glog-dev \
                      libgtest-dev \
                      libjsoncpp-dev \
                      libnuma-dev \
   ```

2. 安装 etcd-cpp-apiv3 库（ https://github.com/etcd-cpp-apiv3/etcd-cpp-apiv3 ），请参阅 `Build and install` 一节的说明，并确保 `make install` 执行成功

3. 进入项目根目录，运行
   ```bash
   mkdir build
   cd build
   cmake .. # 可打开根目录下的 CMakeLists.txt 文件更改编译选项，如只编译部分组件
   make -j
   ```
