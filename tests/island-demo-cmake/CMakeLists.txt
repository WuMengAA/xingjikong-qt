cmake_minimum_required(VERSION 3.21)
project(island_demo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
add_executable(island_demo
  D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-control-qt/src/island.cpp
  D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-control-qt/src/island.h
  D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-control-qt/tests/island_demo.cpp
)
target_include_directories(island_demo PRIVATE
  D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-control-qt/src)
target_link_libraries(island_demo PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(island_demo PROPERTIES WIN32_EXECUTABLE OFF)