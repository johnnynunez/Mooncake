cmake_minimum_required(VERSION 3.16)
project(mooncake CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-O3 -g -Wall")

include_directories(include)

add_library(mooncake src/transfer_engine/transfer_engine.cpp
                     src/transfer_engine/transfer_metadata.cpp
                     src/transfer_engine/rdma_context.cpp
                     src/transfer_engine/rdma_endpoint.cpp)
target_link_libraries(mooncake PUBLIC ibverbs glog gflags pthread memcached jsoncpp)

add_subdirectory(example)
add_subdirectory(test)