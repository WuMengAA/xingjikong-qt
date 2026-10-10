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
    Core Gui Quick QuickControls2 Network WebSockets QuickControls2FluentWinUI3)

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

# ── 手机端令牌注入（2026-10-10）：Android 无环境变量，靠构建宏 ──
# 打包时设 STE_MOBILE_TOKEN_BUILD=<生产 viewer token> 即注入；
# 不设则仅环境变量（桌面调试）或空（Android 上会提示未鉴权）。
if(DEFINED ENV{STE_MOBILE_TOKEN_BUILD} AND NOT "$ENV{STE_MOBILE_TOKEN_BUILD}" STREQUAL "")
    target_compile_definitions(StelarithMobile PRIVATE
        STE_MOBILE_TOKEN_CMake="$ENV{STE_MOBILE_TOKEN_BUILD}")
    message(STATUS "[mobile] 已注入编译期令牌（STE_MOBILE_TOKEN_BUILD）")
endif()
