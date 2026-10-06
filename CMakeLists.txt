cmake_minimum_required(VERSION 3.21)
project(stelarith-viewer-qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/utf-8)

# ── 管理端版本号（2026-10-06 打磨）──
# 界面「关于」那张卡以前写死一个「—」，用户看版本得去翻日志 —— 这里编进二进制，
# 界面直接读 backend.version。**这是管理端版本号的唯一真源**，改版本只改这一行数。
set(VIEWER_VERSION "0.6.9")
add_compile_definitions(STELARITH_VIEWER_VERSION_STRING="${VIEWER_VERSION}")

# 构建期卡死"源码里那份版本"和这里漂移（2026-10-06 补）。
# 背景：main.cpp 原先也硬编码 kViewerVersion="0.6.2"，CMake 这边已经 0.6.3 →
# 日志首行写 0.6.2、「关于」写 0.6.3，用户一眼就看得出这软件没打磨过。
# 光靠"记得同步"是守不住的（上面刚漂移过一次），所以在配置阶段直接读源码比对，不一致就别编。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" _viewer_src)
if (_viewer_src MATCHES "kViewerVersion[ \t\r\n]*=[ \t\r\n]*\"([0-9][^\"]*)\"")
  set(_src_version "${CMAKE_MATCH_1}")
  if(NOT _src_version STREQUAL "${VIEWER_VERSION}")
    message(FATAL_ERROR
      "版本号漂移：CMakeLists.txt 是 ${VIEWER_VERSION}，src/main.cpp 里的回落是 ${_src_version}。\n"
      "改版本只改 CMakeLists.txt 的 set(VIEWER_VERSION ...) 这一行（main.cpp 那份会自动跟随，\n"
      "不一致说明手改过 main.cpp —— 要么删掉手改，要么把这里的 VIEWER_VERSION 一起改）。")
  endif()
  message(STATUS "[viewer] 版本一致性检查通过：${_src_version}")
else()
  message(FATAL_ERROR "在 src/main.cpp 里找不到 kViewerVersion 字面量，版本号校验没法做")
endif()
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
# Network（2026-10-06 加）：站点账号登录要 QNetworkAccessManager（换令牌、取云端接入票）
# 与 QTcpServer（接OAuth 回拨的 loopback 端口）。QWebSockets 会传递依赖到 Qt6::Network，
# 但这里显式列出来 —— 链接表上读得见，换掉 WebSockets 时才不会被顺手漏掉。
# QuickControls2（2026-10-06 加）：main.cpp 要 QQuickStyle::setStyle("Basic")。
# 不把这个模块挂上，QQuickStyle 头文件/链接都拿不到，默认风格那堆
# "style does not support customization" 警告也就消不掉。
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network WebSockets Widgets WebEngineWidgets WebChannel)
qt_standard_project_setup(REQUIRES 6.5)

# 界面与逻辑分开：viewerbackend.* 只管 连接/协议/数据，main.cpp 只管界面
qt_add_executable(viewer-qt
  src/main.cpp
  src/viewerbackend.cpp
  src/viewerbackend.h
  src/singleinstance.cpp
  src/singleinstance.h
  src/oauthlogin.cpp
  src/oauthlogin.h
  # ── 课表编辑器模块（2026-10-06 从 schedule-qt 并入）──
  src/schedule/profile_repository.cpp
  src/schedule/schedule_model.cpp
  src/schedule/timeslot_model.cpp
  src/schedule/subject_model.cpp
  src/schedule/cses_importer.cpp
  src/schedule/undo_manager.cpp
  # ── 大屏今日课表窗口（2026-10-06 与被控端对称）──
  src/schedule_view.cpp
  src/schedule_view.h
  # ── 概览页今日课表数据（2026-10-06 补上概览页的课表卡）──
  src/schedule_today_provider.cpp
  src/schedule_today_provider.h
  # ── 课表编辑器文件选择（2026-10-06）──
  src/file_dialogs.cpp
  src/file_dialogs.h
)
# 界面资源：QML 模块 URI = Stelarith。
# 2026-10-06：Main.qml 不再列进 QML_FILES —— 它原先是跟 exe 一起烧进二进制的，
# 于是改一个按钮文案都要重出整包。现在界面层整体走磁盘（见 main.cpp 的加载路径），
# 这是「管理端界面能单独打补丁」的前提；Dialog 等其余文件仍列在这里，它们的类型注册
# 与「同模块裸类型名自解析」依赖这条 qt_add_qml_module（实测从磁盘加载时依然成立）。
qt_add_qml_module(viewer-qt
  URI Stelarith
  VERSION 1.0
  QML_FILES
    qml/SoftwareDialog.qml
    qml/LogDialog.qml
    qml/MediaDialog.qml
    qml/ScheduleDialog.qml
    qml/FilePushDialog.qml
    qml/NotifyDialog.qml
    qml/NotifyParams.js
    qml/schedule/ScheduleEditor.qml
    qml/schedule/ScheduleGrid.qml
    qml/schedule/TimeAxis.qml
    qml/schedule/TimeAxisBlock.qml
    qml/schedule/SubjectManager.qml
)

# 2026-10-06：QML 不再编进 exe（见上面对 QML_FILES 的改动），改为构建后拷到
# exe 同目录的 qml/Stelarith/，运行期由 main.cpp 从磁盘加载。
# 放子目录而不是 qml/ 根：那里是 windeployqt 的 Qt 插件地盘（QtQuick.2/、QtQml/…），
# 我们自己的 .qml 混进去会跟它们的 qmldir 抢根目录。
# ⚠️ 少了这一步，装出来的管理端会「双击没反应」：QML 加载失败 → 只有一行日志 → 直接 return 1。
add_custom_command(TARGET viewer-qt POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith"
  VERBATIM)

target_link_libraries(viewer-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network Qt6::WebSockets Qt6::Widgets Qt6::WebEngineWidgets Qt6::WebChannel)
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
