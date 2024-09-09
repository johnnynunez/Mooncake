
file(GLOB SOURCES "*.cpp")
add_subdirectory(cachelib_memory_allocator)

add_library(cache_allocator ${SOURCES})
target_link_libraries(cache_allocator PUBLIC transfer_engine glog gflags)
