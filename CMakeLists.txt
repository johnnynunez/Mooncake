cmake_minimum_required(VERSION 3.16)
project(mooncake CXX C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Wextra")

set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_C_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_DEBUG "-O0")

set(CMAKE_BUILD_TYPE "Release")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(USE_CUDA "option for using gpu" ON)

include_directories(thirdparties/include)
link_directories(thirdparties/lib)

add_subdirectory(mooncake-transfer-engine)
#add_subdirectory(mooncake-p2p-store)
add_subdirectory(mooncake-allocator)
