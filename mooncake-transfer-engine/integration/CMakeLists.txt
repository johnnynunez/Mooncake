file(GLOB SOURCES "*.cpp")
# add_library(vllm_adaptor vllm/vllm_adaptor.cpp)
# target_link_libraries(vllm_adaptor PUBLIC transfer_engine glog gflags)

set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED) 
pybind11_add_module(mooncake_vllm_adaptor ${SOURCES} vllm/vllm_adaptor.cpp)
target_link_libraries(mooncake_vllm_adaptor PUBLIC transfer_engine glog gflags)
install(TARGETS mooncake_vllm_adaptor DESTINATION /root/miniconda3/lib/python3.8/site-packages)