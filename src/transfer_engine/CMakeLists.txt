add_library(transfer_engine transfer_engine.cpp
                            transfer_metadata.cpp
                            rdma_context.cpp
                            rdma_endpoint.cpp
                            worker_pool.cpp)
target_link_libraries(transfer_engine PUBLIC ibverbs glog gflags pthread memcached jsoncpp numa)