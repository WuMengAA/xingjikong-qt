cmake_minimum_required(VERSION 3.16)
project(rtc_surface_selfcheck LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml Gui)

add_executable(rtc_surface_selfcheck main.cpp)
target_link_libraries(rtc_surface_selfcheck PRIVATE Qt6::Quick Qt6::Qml Qt6::Gui)
