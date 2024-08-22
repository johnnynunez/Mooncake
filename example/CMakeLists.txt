add_executable(transfer_engine_test transfer_engine_test.cpp)
target_link_libraries(transfer_engine_test PUBLIC transfer_engine)

add_executable(allocator_test allocator_test.cpp)
target_link_libraries(allocator_test PUBLIC allocator cachelib_memory_allocator)

if (USE_CUDA)
  include_directories("/usr/local/cuda/include")
  add_executable(gpu_test gpu_test.cpp)
  target_link_libraries(gpu_test PUBLIC transfer_engine)
  target_link_libraries(gpu_test PUBLIC cuda)
  target_compile_definitions(gpu_test PUBLIC USE_CUDA)
endif()

add_executable(gpu_test_client gpu_test.cpp)
target_link_libraries(gpu_test_client PUBLIC transfer_engine)

add_executable(endpoint_test endpoint_test.cpp)
target_link_libraries(endpoint_test PUBLIC transfer_engine)

add_executable(xport_test xport_test.c)
target_link_libraries(xport_test PUBLIC transfer_engine)

add_executable(cufile_transport_test cufile_transport_test.c)
target_link_libraries(cufile_transport_test PUBLIC transfer_engine)