# 手机端 · 管理端伴侣（2026-10-09 设计文档）
# 独立 Qt Quick 工程：纯 QML + WebSockets，不含 WebEngine/Widgets/winmm。
# Android 打包前置：Qt android_arm64_v8a 套件 + NDK r26/r27 + JDK 17。
cmake_minimum_required(VERSION 3.21)
project(StelarithMobile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Quick QuickControls2 Network WebSockets)

# Qt 6.12 标准项目配置（Android 打包所需的 gradle/androiddeployqt 集成）
qt_standard_project_setup()

qt_add_executable(StelarithMobile
    src/main.cpp
    src/mobilebackend.cpp
    src/mobilebackend.h
    qml/Main.qml
    qml/Theme.qml
    qml/components/MobileCard.qml
    qml/components/StatusDot.qml
)
# Android 打包：用项目自定义 AndroidManifest（包名 com.stelarith.mobile、label 星集控、网络权限）
if(ANDROID)
    set(QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
endif()
qt_add_qml_module(StelarithMobile
    URI StelarithMobile
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/MobileCard.qml
        qml/components/StatusDot.qml
)

target_include_directories(StelarithMobile PRIVATE src)
target_link_libraries(StelarithMobile PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::WebSockets)
