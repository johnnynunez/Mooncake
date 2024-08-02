add_library(transfer_engine transfer_engine.cpp
                            transfer_metadata.cpp
                            rdma_context.cpp
                            rdma_endpoint.cpp
                            worker_pool.cpp
                            transfer_engine_c.cpp)
add_compile_definitions(transfer_engine PUBLIC MOONCAKE_USE_ETCD)
target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread memcached jsoncpp numa etcd-cpp-api)

add_library(transfer_engine_shared SHARED transfer_engine.cpp
                                            transfer_metadata.cpp
                                            rdma_context.cpp
                                            rdma_endpoint.cpp
                                            worker_pool.cpp
                                            transfer_engine_c.cpp)
add_compile_definitions(transfer_engine_shared PUBLIC MOONCAKE_USE_ETCD)
target_link_libraries(transfer_engine_shared PUBLIC ibverbs glog gflags pthread memcached jsoncpp numa etcd-cpp-api)