cmake_minimum_required(VERSION 3.16)
project(mooncake CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-O3 -g -Wall")

include_directories(src)

add_subdirectory(src/allocator)
add_subdirectory(src/transfer_engine)

add_subdirectory(example)
