add_library(transfer_engine transfer_engine.cpp
                            transfer_metadata.cpp
                            rdma_context.cpp
                            rdma_endpoint.cpp
                            worker_pool.cpp
                            endpoint_store.cpp
                            transfer_engine_c.cpp
                            config.cpp)
add_compile_definitions(transfer_engine PUBLIC MOONCAKE_USE_ETCD)
#target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa etcd-cpp-api)
target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa etcd-cpp-api-core protobuf grpc++ grpc)
#target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread jsoncpp numa memcached)
