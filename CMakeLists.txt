cmake_minimum_required(VERSION 3.16)
project(mooncake CXX C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -Wextra -Wno-unused-parameter -fPIC")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Wextra -Wno-unused-parameter -fPIC")

set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(CMAKE_C_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_DEBUG "-O0")

set(CMAKE_BUILD_TYPE "Release")

#set(CMAKE_C_FLAGS "-O0 -g -Wall")
#set(CMAKE_CXX_FLAGS "-O0 -g -Wall")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(USE_CUDA "option for using gpu direct" ON)
option(USE_CXL "option for using cxl protocol" ON)

option(WITH_P2P_STORE "build p2p store library and sample code" OFF)
option(WITH_ALLOCATOR "build allocator, component of managed store" OFF)
option(WITH_RUST_EXAMPLE "build the Rust interface and sample code for the transfer engine" OFF)

if (USE_CUDA)
  add_compile_definitions(USE_CUDA)
  message(STATUS "CUDA support is enabled")
endif()

add_subdirectory(mooncake-transfer-engine)

if (WITH_P2P_STORE)
  add_subdirectory(mooncake-p2p-store)
endif()

if (WITH_ALLOCATOR)
  add_subdirectory(mooncake-allocator)
endif()
