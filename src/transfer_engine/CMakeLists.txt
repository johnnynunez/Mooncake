add_library(transfer_engine transfer_engine.cpp
                            transfer_metadata.cpp
                            transport.cpp
                            rdma_context.cpp
                            rdma_endpoint.cpp
                            rdma_transport.cpp
                            worker_pool.cpp
                            endpoint_store.cpp
                            transfer_engine_c.cpp
                            config.cpp
                            old_transfer_engine.cpp
                            )

add_compile_definitions(transfer_engine PUBLIC MOONCAKE_USE_ETCD)
#target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa etcd-cpp-api)
target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa etcd-cpp-api-core protobuf grpc++ grpc)

if (USE_CUDA)  
  add_compile_definitions(transfer_engine PUBLIC USE_CUDA)
  target_sources(transfer_engine PUBLIC nvmeof_transport.cpp
                              cufile_desc_pool.cpp
                              cufile_context.cpp)

  include_directories("/usr/local/cuda/include")
  target_link_directories(transfer_engine PRIVATE /usr/local/cuda/lib64)
  target_link_libraries(transfer_engine PUBLIC cufile cudart rt)
endif()

#target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa memcached)
