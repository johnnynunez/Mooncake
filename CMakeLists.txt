cmake_minimum_required(VERSION 3.16)
project(mooncake CXX C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "-O3 -g -Wall")
set(CMAKE_CXX_FLAGS "-O3 -g -Wall")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(USE_CUDA "option for using gpu" OFF)

include_directories(src)
include_directories(src/cachelib_memory_allocator/include)
include_directories(src/cachelib_memory_allocator/fake_include)
include_directories(thirdparties/include)
link_directories(thirdparties/lib)

add_subdirectory(src/allocator)
add_subdirectory(src/cachelib_memory_allocator)
add_subdirectory(src/transfer_engine)

add_subdirectory(example)