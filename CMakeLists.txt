cmake_minimum_required(VERSION 3.16)
project(mooncake CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-O3 -g -Wall --std=c++17")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(src)
include_directories(src/cachelib_memory_allocator/include)
include_directories(src/cachelib_memory_allocator/fake_include)
#include_directories(build/etcd-cpp-apiv3)
#link_directories(build/etcd-cpp-apiv3/build/src)

add_subdirectory(src/allocator)
add_subdirectory(src/cachelib_memory_allocator)
add_subdirectory(src/transfer_engine)

add_subdirectory(example)