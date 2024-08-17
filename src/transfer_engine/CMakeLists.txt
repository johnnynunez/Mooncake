add_library(transfer_engine transfer_engine.cpp
                            transfer_metadata.cpp
                            rdma_context.cpp
                            rdma_endpoint.cpp
                            rdma_transport.cpp
                            worker_pool.cpp
                            endpoint_store.cpp
                            transfer_engine_c.cpp
                            config.cpp
                            nvmeof_transport.cpp
                            cufile_context.cpp
                            )

add_compile_definitions(transfer_engine PUBLIC MOONCAKE_USE_ETCD)
#target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa etcd-cpp-api)
target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa etcd-cpp-api-core protobuf grpc++ grpc)

if (USE_CUDA)
  include_directories("/usr/local/cuda/include")
  add_compile_definitions(transfer_engine PUBLIC USE_CUDA)
  target_link_directories(transfer_engine PRIVATE /usr/local/cuda/lib64)
  target_link_libraries(transfer_engine PUBLIC cuda cufile cudart)
endif()

#target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa memcached)
