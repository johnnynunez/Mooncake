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

#set(CMAKE_C_FLAGS "-O0 -g -Wall")
#set(CMAKE_CXX_FLAGS "-O0 -g -Wall")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(USE_CUDA "option for using gpu" OFF)
option(USE_STATIC_ETCD_CPP_API "option for using statically-linked etcd cpp api" ON)

option(BUILD_TRANSFER_ENGINE "build transfer engine" ON)
option(BUILD_P2P_STORE "build p2p store" ON)
option(BUILD_ALLOCATOR "build allocator" ON)

if (USE_STATIC_ETCD_CPP_API)
  include_directories(thirdparties/include)
  link_directories(thirdparties/lib)
endif()

if (BUILD_TRANSFER_ENGINE)
  add_subdirectory(mooncake-transfer-engine)
endif()

if (BUILD_P2P_STORE)
  add_subdirectory(mooncake-p2p-store)
endif()

if (BUILD_ALLOCATOR)
  add_subdirectory(mooncake-allocator)
endif()
