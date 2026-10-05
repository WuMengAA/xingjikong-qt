cmake_minimum_required(VERSION 3.21)
project(stelarith-schedule-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 源码含中文注释：不加 /utf-8 时 MSVC 会按 GBK 解读
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Qml QmlWorkerScript Quick)

add_executable(stelarith-schedule-qt
    src/main.cpp
    src/profile_repository.cpp
    src/schedule_model.cpp
    src/timeslot_model.cpp
    src/subject_model.cpp
    src/cses_importer.cpp
)

target_link_libraries(stelarith-schedule-qt PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::QmlWorkerScript
    Qt6::Quick
)

# qrc 资源（传统方式：打包进主 exe，qrc:/qml/ 可直接加载）
qt_add_resources(stelarith-schedule-qt "qml_res"
    PREFIX "/qml"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/Main.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/ScheduleGrid.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/TimeAxis.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/TimeAxisBlock.qml
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/SubjectManager.qml
)

# GUI 子系统：不要控制台窗口
set_target_properties(stelarith-schedule-qt PROPERTIES WIN32_EXECUTABLE ON)
