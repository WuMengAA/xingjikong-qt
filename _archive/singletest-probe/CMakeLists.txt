cmake_minimum_required(VERSION 3.21)
project(singletest-probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
  add_compile_options(/utf-8 /EHsc)
endif()

# 刻意直接编译真实源码 src/singleinstance.cpp（不是复制一份改过的）：
# 被压的就是发布用的那份实现，改了源码这个探针立刻能反映出来。
find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
qt_standard_project_setup(REQUIRES 6.5)

add_executable(singletest-probe
  probe.cpp
  ../../src/singleinstance.cpp
)
target_include_directories(singletest-probe PRIVATE ../../src)
target_link_libraries(singletest-probe PRIVATE Qt6::Core)
