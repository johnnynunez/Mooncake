file(GLOB XPORT_SOURCES "*.cpp")

add_subdirectory(rdma_transport)

add_library(transport STATIC ${XPORT_SOURCES})

target_link_libraries(transport PUBLIC rdma_transport)

if (USE_CUDA)
  add_subdirectory(nvmeof_transport)
  target_link_libraries(transport PUBLIC nvmeof_transport)
endif()