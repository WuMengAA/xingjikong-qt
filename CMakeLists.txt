cmake_minimum_required(VERSION 3.21)
project(stelarith-viewer-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/utf-8)
if (MSVC)
  add_compile_options(/EHsc)
endif()

# Qt Quick 版：界面用 QML，逻辑在 ViewerBackend（C++）
# Widgets 只为拿到 QFileDialog（选本机文件）。QtQuick.Dialogs 那套 QML 插件得单独部署，
# 少一层依赖就少一处"装了 exe 却跑不起来"的地方 —— 2026-10-03 在真机上撞过一次。
#
# WebEngineWidgets（2026-10-04 加）：T-3 管理端 WebRTC 收流要一个离屏 QWebEngineView，
# 让 Chromium 里的 RTCPeerConnection 跑 answer 侧、ontrack 拿到被控端推来的视频轨。
# 刻意选 Widgets 版而不是 QML 版 WebEngineView：收流页根本不参与界面布局（它只负责
# 渲染 <video> 再由 C++ 抽帧走现有 frame 通道），用 Widget 版可以像被控端那样 show 到屏幕外，
# 不必为了一个隐藏渲染层去改 QML 布局。
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick WebSockets Widgets WebEngineWidgets WebChannel)
qt_standard_project_setup(REQUIRES 6.5)

# 界面与逻辑分开：viewerbackend.* 只管 连接/协议/数据，main.cpp 只管界面
qt_add_executable(viewer-qt
  src/main.cpp
  src/viewerbackend.cpp
  src/viewerbackend.h
)
# 界面资源：QML 模块 URI = Stelarith（main.cpp 里 loadFromModule("Stelarith","Main")）
qt_add_qml_module(viewer-qt
  URI Stelarith
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/SoftwareDialog.qml
    qml/LogDialog.qml
    qml/MediaDialog.qml
    qml/ScheduleDialog.qml
    qml/FilePushDialog.qml
)

target_link_libraries(viewer-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::WebSockets Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel)
set_target_properties(viewer-qt PROPERTIES WIN32_EXECUTABLE OFF)

# ── Qt 运行时部署（用 Qt 官方部署工具 windeployqt）──
# 【为什么必须有】2026-10-04 踩的"真机级"坑：Qt6Widgets.dll 没跟着 exe 走（链接上了、
# 但 DLL 复制规则没覆盖 Widgets 模块）—— 后果是 exe 一启动就退出，**而且一条日志都不写**：
# 因为程序第一条日志是在 QGuiApplication 之后才打的，崩溃发生得更早，
# 现场看起来跟"程序压根没起来"一模一样（典型的 fail-silent，只能靠缺 DLL 反推）。
# 【为什么用 windeployqt】手抄文件名会漏、也会随 Qt 版本变；这是 Qt 官方部署工具，
# 一次性把运行时 DLL + QML 插件补齐到 exe 同目录，顺手把 QtQuick.Dialogs 这类
# 插件缺失（2026-10-03 在真机上挂过一次 Main.qml 整个加载不出来）的风险一起收掉。
find_program(STE_WINDEPLOYQT_EXECUTABLE
             NAMES windeployqt windeployqt6
             HINTS "$ENV{QTDIR}/bin"
                   "D:/Qt/6.8.1/msvc2022_64/bin")
if(NOT STE_WINDEPLOYQT_EXECUTABLE)
  message(WARNING "[viewer] 没找到 windeployqt，Qt 运行时不会自动部署。"
                  "若 exe 启动即退出且没有任何日志，先手工在 build 目录跑一次 "
                  "windeployqt.exe viewer-qt.exe")
endif()
if(STE_WINDEPLOYQT_EXECUTABLE)
  add_custom_command(TARGET viewer-qt POST_BUILD
    COMMAND "${STE_WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:viewer-qt>"
            --no-translations
    VERBATIM)
endif()
