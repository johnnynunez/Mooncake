# vLLM Integration


## vLLM安装

### 编译步骤

1. 通过系统源下载安装依赖，并从官方镜像下载并编译安装 `etcd-cpp-apiv3`
   ```bash
   bash dependencies.sh
   ```
   > 如果编译没有成功，建议参考报错信息及【非 Ubuntu 系统】部分的描述分析定位原因。

2. 进入项目根目录，运行下列命令进行编译
   ```bash
   mkdir build
   cd build
   cmake ..
   make -j
   ```
