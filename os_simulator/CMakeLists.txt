cmake_minimum_required(VERSION 3.14)
project(OS_Simulator VERSION 1.0 LANGUAGES CXX)

# 使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 pthread (Linux 需要)
find_package(Threads REQUIRED)

# 公共 include 目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 收集所有源文件
file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
)

# 生成可执行文件
add_executable(os_simulator ${SOURCES})

# 链接线程库
target_link_libraries(os_simulator PRIVATE Threads::Threads)

# 平台特定设置
if(WIN32)
    # Windows 下将 data 目录复制到输出目录
    add_custom_command(TARGET os_simulator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:os_simulator>/data
        COMMAND ${CMAKE_COMMAND} -E echo "Build completed."
    )
else()
    # Linux/Mac 同样处理
    add_custom_command(TARGET os_simulator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:os_simulator>/data
    )
endif()
