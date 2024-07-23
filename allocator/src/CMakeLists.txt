# 查找所有源文件
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")

# 添加库
add_library(MyLibrary ${SOURCES})

# 添加可执行文件
add_executable(MyExecutable main.cpp)

# 链接库到可执行文件
target_link_libraries(MyExecutable MyLibrary)

