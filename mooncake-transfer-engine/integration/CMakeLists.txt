add_library(vllm_adaptor vllm/vllm_adaptor.cpp)
target_link_libraries(vllm_adaptor PUBLIC transfer_engine)
