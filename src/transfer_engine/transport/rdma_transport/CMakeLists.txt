file(GLOB RDMA_SOURCES "*.cpp")

add_library(rdma_transport STATIC ${RDMA_SOURCES})
target_link_libraries(rdma_transport PUBLIC transport)