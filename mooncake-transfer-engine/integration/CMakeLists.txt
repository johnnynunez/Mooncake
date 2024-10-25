add_library(vllm_adaptor vllm/vllm_adaptor.cpp)
target_link_libraries(vllm_adaptor PUBLIC transfer_engine)

set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED) 
pybind11_add_module(vllm_adaptor_py ${SOURCES} vllm_adaptor_py.cpp)
target_link_libraries(vllm_adaptor_py PUBLIC transfer_engine)
install(TARGETS vllm_adaptor_py DESTINATION .)