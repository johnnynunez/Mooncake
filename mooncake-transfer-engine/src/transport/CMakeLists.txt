file(GLOB XPORT_SOURCES "*.cpp")

add_subdirectory(rdma_transport)

add_library(transport OBJECT ${XPORT_SOURCES} $<TARGET_OBJECTS:rdma_transport>)

if (USE_CUDA)
  add_subdirectory(nvmeof_transport)
  target_sources(transport PUBLIC $<TARGET_OBJECTS:nvmeof_transport>)
endif()