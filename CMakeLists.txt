cmake_minimum_required(VERSION 3.16)
project(mooncake CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-O3 -g -Wall")

include_directories(include)

add_library(mooncake src/net/transfer_engine.cpp
                     src/net/transfer_metadata.cpp
                     src/net/rdma_context.cpp
                     src/net/rdma_endpoint.cpp)
target_link_libraries(mooncake PUBLIC ibverbs glog gflags pthread memcached jsoncpp)

add_subdirectory(example)
add_subdirectory(test)