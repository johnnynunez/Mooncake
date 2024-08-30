file(GLOB ENGINE_SOURCES "*.cpp")
add_subdirectory(transport)

add_library(transfer_engine ${ENGINE_SOURCES})

add_compile_definitions(transfer_engine PUBLIC MOONCAKE_USE_ETCD)
target_link_libraries(transfer_engine PUBLIC transport ibverbs glog gflags pthread jsoncpp numa etcd-cpp-api-core protobuf grpc++ grpc)

if (USE_CUDA)  
  add_compile_definitions(transfer_engine PUBLIC USE_CUDA)
  target_include_directories(transfer_engine PRIVATE /usr/local/cuda/include)
  target_link_libraries(transfer_engine PUBLIC cuda cufile cudart rt)
endif()

#target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa memcached)
