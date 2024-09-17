add_executable(transfer_engine_test transfer_engine_test.cpp)
target_link_libraries(transfer_engine_test PUBLIC transfer_engine)

if (USE_CUDA)
  include_directories("/usr/local/cuda/include")
  # add_executable(gpu_test gpu_test.cpp)
  # target_link_libraries(gpu_test PUBLIC transfer_engine)
  # target_link_libraries(gpu_test PUBLIC cuda)
  # target_compile_definitions(gpu_test PUBLIC USE_CUDA)
  
  add_executable(xport_test xport_test.c)
  target_link_libraries(xport_test PUBLIC transfer_engine)

  add_executable(cufile_transport_test cufile_transport_test.c)
  target_link_libraries(cufile_transport_test PUBLIC transfer_engine)

  add_executable(unit_tests unit_tests.cpp)
  target_link_libraries(unit_tests PUBLIC transfer_engine gtest gtest_main)
endif()
  
# add_executable(endpoint_test endpoint_test.cpp)
# target_link_libraries(endpoint_test PUBLIC transfer_engine)
  
# add_executable(gpu_test_client gpu_test.cpp)
# target_link_libraries(gpu_test_client PUBLIC transfer_engine)