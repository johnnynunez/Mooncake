cmake_minimum_required(VERSION 3.16)
project(mooncake CXX C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "-O3 -g -Wall")
set(CMAKE_CXX_FLAGS "-O3 -g -Wall")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(USE_CUDA "option for using gpu" OFF)

include_directories(thirdparties/include)
link_directories(thirdparties/lib)

add_subdirectory(mooncake-transfer-engine)
#add_subdirectory(mooncake-p2p-store)
#add_subdirectory(mooncake-allocator)
