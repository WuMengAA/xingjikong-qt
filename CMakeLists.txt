cmake_minimum_required(VERSION 3.21)
# RC：让 CMake 认得 .rc（版本资源）。不加，下面列 app.rc 会报 RC language not enabled。
project(stelarith-viewer-qt LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/utf-8)
# 2026-10-09：viewerbackend.cpp 大（2700+ 行）+ ldc 头 → 编译堆空间不足 C1060
if(MSVC)
  add_compile_options(/Zm1200)
endif()

# ── 管理端版本号（2026-10-06 打磨；2026-10-07 改 DeepSeek Harness 同款）──
# 命名只有两种形态（2026-10-07 起同 DeepSeek Harness）：正式版 X.Y.Z（0.6.23）、
# 预发布 X.Y.Z-alpha.N（0.6.23-alpha.1，内测）与 X.Y.Z-rc.N（0.6.23-rc.1，候选），
# 阶段序 alpha < rc < 正式；号同核心递增、换核心归零、转正即剥离后缀。
# 成熟度标记（内测/公测/强制升级）不进版本号，归 ota.json 的 mandatory / notes 管。
# 完整标准见 docs/版本号命名规范-2026-10-07.md。
# 界面「关于」那张卡以前写死一个「—」，用户看版本得去翻日志 —— 这里编进二进制，
# 界面直接读 backend.version。**这是管理端版本号的唯一真源**，改版本只改这一行数。
set(VIEWER_VERSION "0.6.24-rc.5")
add_compile_definitions(STELARITH_VIEWER_VERSION_STRING="${VIEWER_VERSION}")

# 构建期卡死"源码里那份版本"和这里漂移（2026-10-06 补；2026-10-07 正则收严到 X.Y.Z[-rc.N]）。
# 背景：main.cpp 原先也硬编码 kViewerVersion="0.6.2"，CMake 这边已经 0.6.3 →
# 日志首行写 0.6.2、「关于」写 0.6.3，用户一眼就看得出这软件没打磨过。
# 光靠"记得同步"是守不住的（上面刚漂移过一次），所以在配置阶段直接读源码比对，不一致就别编。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" _viewer_src)
if (_viewer_src MATCHES "kViewerVersion[ \t\r\n]*=[ \t\r\n]*\"([^\"]+)\"")
  set(_src_version "${CMAKE_MATCH_1}")
else()
  message(FATAL_ERROR "在 src/main.cpp 里找不到 kViewerVersion 字面量，版本号校验没法做")
endif()
# 命名守门（2026-10-07）：只认三种形态 —— 正式 X.Y.Z / 内测 X.Y.Z-alpha.N / 候选 X.Y.Z-rc.N
# （阶段号从 1 起，alpha < rc）。顺带把 alpha.0、裸 "-rc"、四段式这些一律挡在构建外。
if (NOT _src_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-(alpha|rc)\\.[1-9][0-9]*)?$")
  message(FATAL_ERROR
    "版本号 ${_src_version} 不合命名规范：只认 0.6.23（正式）、0.6.23-alpha.1（内测）或 "
    "0.6.23-rc.1（候选）这种形态；\n"
    "阶段号从 1 起、不接受裸后缀、不接受第四段数字。详见 docs/版本号命名规范-2026-10-07.md")
endif()
if (NOT _src_version STREQUAL "${VIEWER_VERSION}")
  message(FATAL_ERROR
    "版本号漂移：CMakeLists.txt 是 ${VIEWER_VERSION}，src/main.cpp 里的回落是 ${_src_version}。\n"
    "改版本只改 CMakeLists.txt 的 set(VIEWER_VERSION ...) 这一行（main.cpp 那份会自动跟随，\n"
    "不一致说明手改过 main.cpp —— 要么删掉手改，要么把这里的 VIEWER_VERSION 一起改）。")
endif()
message(STATUS "[viewer] 版本一致性检查通过：${_src_version}")

# installer.nsi（NSIS 安装器）的版本常量也由这里出（2026-10-08 立）。
# 理由与被控端 version.nsh 完全一致：NSIS 那侧不再手抄一份版本号，用
#   !include "..\build\version.nsh"
# 吃这个生成文件 —— 从源头掐掉「安装器 0.6.5 / exe 0.6.23」靠人记着同步的老问题。
# ⚠️ 命名与被控端不同：那边叫 VER，这边叫 VERSION_RAW（管理端 nsi 里是这个名字，
#    保持一致，免得两个工程的脚本文案互相误导）。
configure_file(src/version.nsh.in "${CMAKE_CURRENT_BINARY_DIR}/version.nsh" @ONLY)

# ── 版本资源 + 应用图标（2026-10-07 补）──────────────────────────────────
# 补之前 exe 是「无版本资源的裸 PE」：任务管理器细节页空白、右键属性没有描述。
# 这里**不另写版本号**——直接吃上面校验过的 VIEWER_VERSION，从源头杜绝两份漂移。
# ⚠️ 版本串可以带 -rc.N（显示用，.rc 的字符串字段吃得下：FileVersion "0.6.23-rc.1"），
#    但 FILEVERSION / PRODUCTVERSION 是**二进制字段，只认四段纯数字** —— 直接把整个串按
#    "." 拆开，来个 "0.6.23-rc.1" 会拆出 "22-rc" 这种段，成品 exe 的版本资源就废了。
#    所以先剥出核心三段 X.Y.Z，第四段固定补 0（Windows 版本资源的规矩）。
string(REGEX REPLACE "^(.+)-(alpha|rc)\\.([0-9]+)$" "\\1" VIEWER_CORE "${VIEWER_VERSION}")
if (VIEWER_VERSION STREQUAL "${VIEWER_CORE}")
  set(VIEWER_STAGE "")              # 正式版：没有阶段后缀
  set(VIEWER_STAGE_N "")
else()
  string(REGEX REPLACE "^.*-(alpha|rc)\\.([0-9]+)$" "\\1" VIEWER_STAGE "${VIEWER_VERSION}")
  string(REGEX REPLACE "^.*-(alpha|rc)\\.([0-9]+)$" "\\2" VIEWER_STAGE_N "${VIEWER_VERSION}")
endif()
message(STATUS "[viewer] 版本 ${VIEWER_VERSION}"
  "（核心 ${VIEWER_CORE}，阶段 ${VIEWER_STAGE} ${VIEWER_STAGE_N}）")
string(REPLACE "." ";" _viewer_v "${VIEWER_CORE}")
list(LENGTH _viewer_v _viewer_vn)
while(_viewer_vn LESS 4)
  list(APPEND _viewer_v "0")
  list(LENGTH _viewer_v _viewer_vn)
endwhile()
list(GET _viewer_v 0 _viewer_v0)
list(GET _viewer_v 1 _viewer_v1)
list(GET _viewer_v 2 _viewer_v2)
list(GET _viewer_v 3 _viewer_v3)
set(RC_VERSION_QUAD "${_viewer_v0},${_viewer_v1},${_viewer_v2},${_viewer_v3}")

configure_file(src/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
# ⚠️ rc.exe 的工作目录是构建目录，.rc 里的 ICON "app.ico" 是相对名 ⇒ 图标先备到这里。
configure_file(src/app.ico "${CMAKE_CURRENT_BINARY_DIR}/app.ico" COPYONLY)

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
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network WebSockets Widgets Svg)
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
  # ── 自更新（2026-10-08）：检查 / 下载 / sha256 校验 / 解压 / 退出后替换重启 ──
  src/updater.cpp
  src/updater.h
  # ── 语音对讲（2026-10-07）：管理端 winmm waveIn 采集 ──
  src/audio_capture.cpp
  src/audio_capture.h
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
  # ── SVG 图标提供器（2026-10-08：image://svgicon/，lucide/morphicons 同款）──
  src/svgiconprovider.cpp
  src/svgiconprovider.h
  # ── WebRTC 收流（2026-10-09：libdatachannel 替换 QWebEngine 离屏，探针全绿）──
  src/ldc_receiver.cpp
  src/ldc_receiver.h
  # ── WebRTC ldc 桥接（2026-10-09：独立编译单元，viewerbackend.cpp 大文件不膨胀）──
  src/ldc_rtc_bridge.cpp
  src/ldc_rtc_bridge.h
  # ── 版本资源（configure_file 从 src/app.rc.in 生成，见上面的版本块）──
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
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

# 2026-10-08 引入 lucide（morphicons 同款 24×24 stroke 风格，ISC）SVG 图标，
# 编译进 exe（qrc 前缀 /icons），部署不带额外文件、颜色运行时按主题染。
file(GLOB VIEWER_ICON_SVGS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.svg")
qt_add_resources(viewer-qt "viewericons" PREFIX "/icons" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons" FILES ${VIEWER_ICON_SVGS})

# 2026-10-06：QML 不再编进 exe（见上面对 QML_FILES 的改动），改为构建后拷到
# exe 同目录的 qml/Stelarith/，运行期由 main.cpp 从磁盘加载。
# 放子目录而不是 qml/ 根：那里是 windeployqt 的 Qt 插件地盘（QtQuick.2/、QtQml/…），
# 我们自己的 .qml 混进去会跟它们的 qmldir 抢根目录。
# ⚠️ 少了这一步，装出来的管理端会「双击没反应」：QML 加载失败 → 只有一行日志 → 直接 return 1。
add_custom_command(TARGET viewer-qt POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith"
  VERBATIM)

target_link_libraries(viewer-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network Qt6::WebSockets Qt6::Widgets Qt6::Svg)
set_target_properties(viewer-qt PROPERTIES WIN32_EXECUTABLE OFF)

# 2026-10-09：viewerbackend.cpp 2700+ 行单编译单元峰值超内存（C1060）。
# 只此文件降优化（/Od），其余文件保持 Release 优化——编译内存峰值降下来，功能零影响。
set_source_files_properties(src/viewerbackend.cpp PROPERTIES COMPILE_FLAGS "/Od /Ob0")

# ── 2026-10-09 WebRTC 去 WebEngine 底层：libdatachannel 静态链（并行方案，
#    STE_USE_LDC_RTC=1 默认走 ldc，=0 回退 WebEngine）────────────────────
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/datachannel.lib")
  target_include_directories(viewer-qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/libdatachannel-master/include")
  target_link_libraries(viewer-qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/datachannel.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/srtp2.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/juice.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/usrsctp.lib"
    "C:/Program Files/PostgreSQL/17/lib/libssl.lib"
    "C:/Program Files/PostgreSQL/17/lib/libcrypto.lib"
    ws2_32 bcrypt crypt32 userenv secur32)
  target_compile_definitions(viewer-qt PRIVATE RTC_STATIC)
  message(STATUS "[viewer] libdatachannel 已链入（RTC_STATIC）")
else()
  message(WARNING "[viewer] _probe/ldc_libs/datachannel.lib 缺失——ldc_receiver 编译会失败，请先重建 libdatachannel")
endif()

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
