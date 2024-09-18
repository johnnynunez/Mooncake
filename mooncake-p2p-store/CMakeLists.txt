# Currently you have to manually execute makefile in the src subdirectory.
add_custom_target(build_p2p_store)
add_custom_command(
    TARGET build_p2p_store
    COMMAND make -f ${CMAKE_CURRENT_SOURCE_DIR}/src/Makefile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_executable(p2p_store IMPORTED build_p2p_store)
