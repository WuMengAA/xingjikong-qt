cmake_minimum_required(VERSION 3.21)
# RC：桌面要编 app.rc 需 RC 语言；Android toolchain 无 RC 处理器。
# ⚠️ project() 调用前 toolchain 未加载，ANDROID 变量不可用 → project 固定 CXX，
#    桌面在 project 之后（toolchain 已生效）用 enable_language(RC) 补 RC。
project(stelarith-viewer-qt LANGUAGES CXX)

# 2026-10-11：Android 判定用 CMAKE_SYSTEM_NAME（NDK toolchain 不设裸 ANDROID 变量，
# 只有 ANDROID_ABI 等带前缀的）。统一用它做桌面/Android 分支。
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(STE_IS_ANDROID ON)
else()
  set(STE_IS_ANDROID OFF)
  enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# /utf-8 是 MSVC 专属（clang 不认）；Android 交编译用 clang，跳过
if(MSVC)
  add_compile_options(/utf-8)
endif()
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
set(VIEWER_VERSION "0.6.65")
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

# ── Android 版本真源（2026-10-22 修）──────────────────────────────────────
# 血的教训：不设这两个变量时，androiddeployqt 会用 Qt 默认模板把 AndroidManifest 写成
#   versionCode=1 / versionName=1.0 —— 装到手机上根本看不出是哪一版，且**每次重新
#   configure/部署都会把手工改过的 manifest 冲回默认值**（我今天就手改了一次，
#   下一次构建又被冲掉）。Qt 6 认 QT_ANDROID_VERSION_NAME / QT_ANDROID_VERSION_CODE，
#   在这里从同一个 VIEWER_VERSION 派生，才算真正单一真源。
if (STE_IS_ANDROID)
  set(QT_ANDROID_VERSION_NAME "${VIEWER_VERSION}")
  # versionCode 必须是单调递增的整数（安卓用它判"能不能覆盖安装"）。
  # 规则：major*1e6 + minor*1e4 + patch*100 + 阶段序号
  #   0.6.47-rc.2 → 604702；0.6.47 正式版 → 604700。
  if (VIEWER_STAGE_N STREQUAL "")
    set(_ste_stage_n 0)
  else()
    set(_ste_stage_n "${VIEWER_STAGE_N}")
  endif()
  math(EXPR _ste_vc
       "${_viewer_v0} * 1000000 + ${_viewer_v1} * 10000 + ${_viewer_v2} * 100 + ${_ste_stage_n}")
  set(QT_ANDROID_VERSION_CODE "${_ste_vc}")
  message(STATUS "[viewer] Android versionName=${QT_ANDROID_VERSION_NAME} versionCode=${QT_ANDROID_VERSION_CODE}")
endif()

configure_file(src/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
# ⚠️ rc.exe 的工作目录是构建目录，.rc 里的 ICON "app.ico" 是相对名 ⇒ 图标先备到这里。
configure_file(src/app.ico "${CMAKE_CURRENT_BINARY_DIR}/app.ico" COPYONLY)

if (MSVC)
  add_compile_options(/EHsc)
endif()

# 2026-10-18 远控改 VNC（libvncclient）：本工程**不再**依赖 WebEngine / libdatachannel，
# 画面与远控全部走自绘 QML + 主 WS 二进制帧。Widgets 只为拿 QFileDialog（选本机文件）——
# QtQuick.Dialogs 那套 QML 插件得单独部署，少一层依赖就少一处"装了 exe 却跑不起来"。
# Network（2026-10-06 加）：站点账号登录要 QNetworkAccessManager（换令牌、取云端接入票）
# 与 QTcpServer（接OAuth 回拨的 loopback 端口）。QWebSockets 会传递依赖到 Qt6::Network，
# 但这里显式列出来 —— 链接表上读得见，换掉 WebSockets 时才不会被顺手漏掉。
# QuickControls2（2026-10-06 加）：main.cpp 要 QQuickStyle::setStyle("Basic")。
# 不把这个模块挂上，QQuickStyle 头文件/链接都拿不到，默认风格那堆
# "style does not support customization" 警告也就消不掉。
# ⚠️ 2026-10-13 审计 #25：Qt 最低版本 6.5 → 6.12 —— FluentWinUI3 风格是 6.12 才支持的
# （此前声明 6.5 而代码用 FluentWinUI3，落在 [6.5,6.12) 的构建环境会静默回落默认风格）。
# 项目实际固定 D:\QTqwq\6.12.0，声明对齐到实际，杜绝"配置能过、运行时风格不对"。
# 2026-10-26 P4：监控回放小窗需要真视频解码 ⇒ 引入 Multimedia。
# 历史注释（10-11）说"两 kit 都没装" —— 现已用 MaintenanceTool 装上
# qt.qt6.6120.addons.qtmultimedia（含 WMF 与 **ffmpeg** 两个后端插件，
# mp4/H.264 兼容性取 ffmpeg 更稳）。桌面 kit 与 Android kit 同步具备。
find_package(Qt6 6.12 REQUIRED COMPONENTS Core CorePrivate Gui Quick QuickControls2 Network WebSockets Widgets Svg Multimedia)
qt_standard_project_setup(REQUIRES 6.12)

# ── libopus（P1 语音对讲，静态链接）2026-10-21 ────────────────────────────
# 源码 vendoring 在仓库根 third_party/opus（BSD）。关 shared/tests/programs，
# 只编静态库 opus 供本目标链接（Windows/Android 同源）。
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/opus ${CMAKE_BINARY_DIR}/_opus)

# ── QtShadcn（2026-10-29 · UI 全量替换 P0 接入）────────────────────────────
# shadcn/ui 风格 Qt6/QML 组件库（MIT，vendoring 在仓库根 third_party/qtshadcn）。
# 用户裁决：两端全量采用。作为子项目引入 ⇒ examples/tests 默认关闭。
# ⚠️ 消费方必须 QQuickStyle=Basic（见 src/main.cpp）——QtShadcn 组件是靠覆写
#    QQC 的 background/contentItem 实现的，宿主风格（Fluent/Windows）会吃掉这些
#    覆写，表现为"组件渲染出来了但样式完全不对"这类静默故障。
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/qtshadcn ${CMAKE_BINARY_DIR}/_qtshadcn)

# ── QtShadcn 的 QML plugin 必须显式构建（2026-11-01 · 生产事故修复）──────
# 【事故现象】viewer 装完双击打不开，窗口不出现；日志只有一行
#   "module \"QtShadcn\" plugin \"QtShadcnplugin\" not found"
#   → MainWindow.qml 加载失败 → "FAIL QML 未能加载 qml/Main.qml（致命）"。
# 【根因】qt_add_qml_module 会自动生成两个 target：
#   · QtShadcn      → 类型库（build/QtShadcn.dll）
#   · QtShadcnplugin→ **QML plugin dll**（_qtshadcn/src/QtShadcn/QtShadcnplugin.dll）
#   但 **QtShadcnplugin 默认不在构建图里**（没有 target 依赖它就不编），
#   而 qmldir 里写的是 "optional plugin QtShadcnplugin" ⇒ 运行时找不到 plugin。
#   另：模块目录（qmldir + qml/）本身也**从没被打包**（安装清单只带 build/qml）。
# 【修法】① 显式依赖 plugin target（让它进构建图）；
#         ② 把模块目录整个拷到 qml/Stelarith/QtShadcn/（import path 内，见下面的 sync 规则）；
#         ③ 类型库 QtShadcn.dll 拷到 exe 旁（安装清单显式带，见 make-installer.nsi）。
if(TARGET QtShadcnplugin)
  # ⚠️ 依赖要挂在 viewer-qt 定义**之后**（下面 add_executable 之后才有这个 target）。
  # 这里只做存在性校验，依赖关系见 viewer-qt 定义之后的 add_dependencies。
else()
  message(FATAL_ERROR
    "[viewer] 找不到 QtShadcnplugin target：QtShadcn 的 QML plugin 无法部署，\n"
    "  viewer 会以 \"module QtShadcn plugin QtShadcnplugin not found\" 启动失败。\n"
    "  检查 third_party/qtshadcn/src/CMakeLists.txt 的 qt_add_qml_module 是否被改动。")
endif()
set(STE_QTSHADCN_MODULE_DIR "${CMAKE_BINARY_DIR}/_qtshadcn/src/QtShadcn")

# 界面与逻辑分开：viewerbackend.* 只管 连接/协议/数据，main.cpp 只管界面
# 2026-10-11：Android target 手机 App 并入 —— 桌面临时专属件（app.rc / winmm 采集 /
# 单实例守卫）条件化。桌面照旧，Android 用 stub/跳过。
if(STE_IS_ANDROID)
  set(STE_APP_RC "")                     # Android 不编 .rc 版本资源
  set(STE_AUDIO_CAPTURE_SRC src/audio_capture_android.cpp)
  set(STE_AUDIO_CAPTURE_HDR src/audio_capture.h)
  set(STE_SINGLEINSTANCE_SRC "")         # Android 多开无害，跳过单实例守卫
  # ⚠️ 2026-10-11：必须在 qt_add_executable 之前设 **CACHE** 变量，androiddeployqt
  # 生成部署 json 时才读到（普通 set 不会进 json → manifest 不合并、包名变默认）
  set(QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android" CACHE STRING "Android package source dir" FORCE)
else()
  set(STE_APP_RC "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
  set(STE_AUDIO_CAPTURE_SRC src/audio_capture.cpp)
  set(STE_AUDIO_CAPTURE_HDR src/audio_capture.h)
  set(STE_SINGLEINSTANCE_SRC src/singleinstance.cpp src/singleinstance.h)
endif()
qt_add_executable(viewer-qt
  src/main.cpp
  src/viewerbackend.cpp
  src/viewerbackend.h
  # 2026-10-18：远控 = libvncclient（VNC over WebSocket）。
  # vnc_client 负责 RFB 协议与帧缓冲解码；vnc_image_provider 把 QImage 交给 QML
  # 的 image://vnc/live（禁 QWidget 画图）。两者默认常编：未链 libvncclient 时
  # vnc_client.cpp 退化为安全桩（不引 rfb 头），整个工程仍可干净构建。
  src/vnc_client.cpp
  src/vnc_client.h
  # 2026-10-26 P4：回放本机回环代理（补 Bearer 头 + 转发 Range，见文件头注释）
  src/playback_proxy.cpp
  src/playback_proxy.h
  src/vnc_image_provider.cpp
  src/vnc_image_provider.h
  # 2026-10-31 A 档第④步：远控 H264 画面 → QML image://h264/live（MfxH264Decoder 兜底解码）
  src/H264ImageProvider.cpp
  src/H264ImageProvider.h
  # 2026-10-15 组件6 配套：互动审批桥（审批数据面 + 共享灵动岛的桥面）。
  # 独立编译单元（不给 2700 行的 viewerbackend.cpp 再加负担）。
  src/interactbridge.cpp
  src/interactbridge.h
  # 2026-10-13 架构拆分（审计 2.1）：文件推送状态机独立编译单元，给 viewerbackend.cpp 减负
  src/viewerbackend_file.cpp
  # 2026-10-13 架构拆分：语音对讲 + 屏幕广播子域独立编译单元
  src/viewerbackend_voice.cpp
  # 2026-10-13 架构拆分：站点账号域（OAuth + 接入票 + 凭据）独立编译单元
  src/viewerbackend_account.cpp
  # ── 2026-11-02 屎山整理 #6：viewerbackend.cpp 2995→2327，按叶子域拆五个编译单元 ──
  # 挑的都是「不参与设备表、不参与下发指令」的独立链：C++ 允许同一类成员函数
  # 分布在不同 TU（private 照访，无需 friend / 无需改头文件可见性）。
  # 跨 TU 共享助手（frameFingerprint / makeVncFrame / viewerDesktopLnk 的实现，
  # 拆域时由内部链接升为外部链接；只有一份真源，不各域各抄一份）
  src/viewerbackend_shared.cpp
  src/viewerbackend_shared.h
  src/viewerbackend_vnc.cpp        # 远控 VNC：建立/鉴权/帧注入/断开
  src/viewerbackend_terminal.cpp   # 远程终端：开/输/关 + 状态机
  src/viewerbackend_qmlpatch.cpp   # QML 热更：检查 + store 模式 zip 解压（纯文件操作）
  src/viewerbackend_proc.cpp       # 系统集成：进程黑名单 / 注册表自启 / 快捷方式
  src/viewerbackend_diag.cpp       # 诊断与反馈：日志尾部 / 系统信息 / 事件与排行拉取
  # ── 班级域（2026-10-23 #460 · viewerbackend P1 拆分第一刀 ClassService）──
  # 班级工作台数据面：ClassService + typed Models（设备网格/Timeline）+ 缩略图 provider。
  # viewerbackend_class.cpp 只放入站分发；协议状态与模型全在 src/class/。
  src/viewerbackend_class.cpp
  src/class/class_device_model.cpp
  src/class/class_device_model.h
  src/class/class_event_model.cpp
  src/class/class_event_model.h
  src/class/class_service.cpp
  src/class/class_service.h
  # 班级管理写操作（2026-10-24 方案 B）：HTTP /api/class/*，只发不存状态
  src/class/class_admin.cpp
  src/class/class_admin_import.cpp
  src/class/class_admin.h
  # 2026-10-28 B0：班级中心控制器（承载原先写在 QML 里的全部计算/配色/状态，
  # 落实「QML 不写逻辑、全部交给 C++」）。Q_OBJECT 头须显式列入让 AUTOMOC 必扫。
  src/class/class_center_controller.cpp
  src/class/class_center_controller.h
  src/class/class_thumb_provider.cpp
  src/class/class_thumb_provider.h
  ${STE_SINGLEINSTANCE_SRC}
  src/oauthlogin.cpp
  src/oauthlogin.h
  # ── RBAC（2026-10-20）：前端权限镜像层（镜像云端 perms.js，含字段脱敏 + 组件级鉴权）──
  src/permission_manager.h
  src/permission_manager.cpp
  # ── 自更新（2026-10-08）：检查 / 下载 / sha256 校验 / 解压 / 退出后替换重启 ──
  src/updater.cpp
  src/updater.h
  # ── 语音对讲（2026-10-07）：管理端 winmm waveIn 采集 ──
  # 2026-10-11：Android 无 winmm → 编 stub（audio_capture_android.cpp）
  ${STE_AUDIO_CAPTURE_SRC}
  ${STE_AUDIO_CAPTURE_HDR}
  # ── 语音对讲下行（B6 · 2026-10-11）：管理端 winmm waveOut 播放学生回传的 audio/pcm ──
  # 桌面用 waveOut（audio_playback.cpp 内 #pragma comment(lib,"winmm.lib") 拉库）；
  # Android 无 waveOut → 同文件内 Q_OS_ANDROID 分支留空桩（不破 Android 链接）。
  src/audio_playback.cpp
  src/audio_playback.h
  # ── Android 系统通知（2026-10-11）：notifyPref 补通知栏横幅；桌面空实现──
  src/android_notify.cpp
  src/android_notify.h
  # ── 课表编辑器模块（2026-10-06 从 schedule-qt 并入）──
  src/schedule/profile_repository.cpp
  src/schedule/schedule_model.cpp
  src/schedule/timeslot_model.cpp
  src/schedule/subject_model.cpp
  src/schedule/cses_importer.cpp
  src/schedule/undo_manager.cpp
  # ── 今日课表：强类型模型（2026-11-02 取代 schedule_view 的 QWidget 手绘窗口）──
  # 原 src/schedule_view.{h,cpp} 是 `class ScheduleViewWindow : public QWidget`，
  # 在 paintEvent() 里 QPainter 手绘整屏 —— 违反 2026-11-01 裁决
  # 「qtshadcn 唯一图形库，不许 QWidget CPU 渲染」，已删除（渲染改到
  # qml/panels/TodayScheduleWindow.qml + TodayScheduleRow.qml）。
  # 本模型只算数、不认识 QML（AGENTS §10 分层），可离屏单测。
  src/schedule/TodayScheduleModel.cpp
  src/schedule/TodayScheduleModel.h
  # 2026-11-02：`src/schedule_view.{h,cpp}`（QWidget + QPainter 手绘大屏）与
  # `src/schedule_today_provider.{h,cpp}`（QJsonArray 直喂 QML，违反 §10.7）已删除，
  # 由上面 TodayScheduleModel（QAbstractListModel）+ qml/panels/TodaySchedule* 取代。
  # ── 课表编辑器文件选择（2026-10-06）──
  src/file_dialogs.cpp
  src/file_dialogs.h
  # ── SVG 图标提供器（2026-10-08：image://svgicon/，lucide/morphicons 同款）──
  src/svgiconprovider.cpp
  src/svgiconprovider.h
  # ── H264 MF 解码兜底（2026-10-16：viewer 无 ffmpeg 时的备选解码通道）──
  src/mfx_h264_decode.cpp
  src/mfx_h264_decode.h
  # ── 版本资源（configure_file 从 src/app.rc.in 生成，见上面的版本块；Android 不编）──
  ${STE_APP_RC}
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
    qml/InteractDialog.qml
    qml/NotifyParams.js
    qml/schedule/ScheduleEditor.qml
    qml/schedule/ScheduleGrid.qml
    qml/schedule/TimeAxis.qml
    qml/schedule/TimeAxisBlock.qml
    qml/schedule/SubjectManager.qml
)

# 2026-10-08 引入 lucide（morphicons 同款 24×24 stroke 风格，ISC）SVG 图标，
# 编译进 exe（qrc 前缀 /icons），部署不带额外文件、颜色运行时按主题染。
# ⚠️ CONFIGURE_DEPENDS 不可省（2026-11-01 踩坑）：普通 GLOB 只在 configure 期求值一次，
#   之后增删 svg 不会触发重新 configure ⇒ ninja 认为 exe 无变化、图标进不了 exe
#   （实测：资源目录被恢复后 build 出的 exe 尺寸不变，运行时报
#   "SvgIcon: Failed to get image from provider: image://svgicon/xxx"）。
#   加 CONFIGURE_DEPENDS 后，文件集变化会自动触发 re-configure。
file(GLOB VIEWER_ICON_SVGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.svg")
qt_add_resources(viewer-qt "viewericons" PREFIX "/icons" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons" FILES ${VIEWER_ICON_SVGS})

# 2026-10-11（F4）：鲸鱼娘素材编进 exe（qrc 前缀 /whale）。
# 素材来自 dsh-whale-widget 原型包（DSniang1.png / rua.gif），cp 拷入不动源。
# 编进 qrc 而非散放磁盘：与 icons 同思路，部署不带额外文件、跨机器路径稳定。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/whale")
  file(GLOB VIEWER_WHALE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/whale/*")
  qt_add_resources(viewer-qt "viewerwhale" PREFIX "/whale" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/whale" FILES ${VIEWER_WHALE_FILES})
endif()

# 2026-10-06：QML 不再编进 exe（见上面对 QML_FILES 的改动），改为构建后拷到
# exe 同目录的 qml/Stelarith/，运行期由 main.cpp 从磁盘加载。
# 放子目录而不是 qml/ 根：那里是 windeployqt 的 Qt 插件地盘（QtQuick.2/、QtQml/…），
# 我们自己的 .qml 混进去会跟它们的 qmldir 抢根目录。
# ⚠️ 少了这一步，装出来的管理端会「双击没反应」：QML 加载失败 → 只有一行日志 → 直接 return 1。
#
# 2026-10-15（用户点单「抽共享目录」）：灵动岛视觉层**唯一真源**在根仓库
#   <repo-root>/Stelarith-shared-qml/island/，被控端与管理端共用一份。
#   这里把它拷到 qml/Stelarith/island/ —— 管理端审批面板要嵌一颗与被控端
#   **一模一样**的灵动岛（用户原话），走的就是这份文件，不再另写一份。
# ⚠️ 共享件属根仓库 ⇒ **必须整体 clone 根仓库**；单独 clone viewer 会缺件。
set(STE_SHARED_ISLAND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Stelarith-shared-qml/island")
if(NOT EXISTS "${STE_SHARED_ISLAND_DIR}/IslandRoot.qml")
  message(FATAL_ERROR
    "[viewer] 共享灵动岛目录缺失：${STE_SHARED_ISLAND_DIR}\n"
    "  灵动岛 QML 真源在 Stelarith-shared-qml/island/（两端共用）。\n"
    "  请整体 clone 根仓库 Stelarith-xingjikong-qt，不要单独 clone 本子工程。")
endif()

# 2026-10-16（U1 共享 UI 基座 · 用户点单「插件化思维，统一界面结构」）：
#   Md3 设计系统（Md3.qml 令牌 + md3/ 12 组件）此前在两端**各存一份逐字副本**
#   （13 件，仅 Md3Dialog 差一行 import "."）—— 正是「改一处不生效」的病灶。
#   真源收到根仓库 Stelarith-shared-qml/ui/，构建期拷回 qml/Stelarith/components/。
#   ⚠️ 落点必须与既有相对导入逐字对齐（本工程用 `import "components"` 与
#   `import "../components/md3"`）⇒ 消费方 QML **零改动**，与岛同一套路。
set(STE_SHARED_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Stelarith-shared-qml/ui")
# 2026-11-02：判据从 `theme/Md3.qml` 改为 `theme/Theme.qml`。
# Md3.qml 已退场（全库 QML/JS 对 `Md3.` 的引用实测 0 处，主题真源只剩 Theme 单例，
# qmldir 也只声明 `singleton Theme 1.0 Theme.qml`）。旧判据指向一个已删除文件，
# 结果目录明明齐全、构建却 FATAL_ERROR —— 门禁比现实落后一轮，先于代码把构建卡死。
# 判据应盯「构建真正需要的东西」：copy_directory 拷的是整个 ui/ 目录，而消费方
# 解析 `import "../components/theme"` 依赖 qmldir + Theme.qml 这一对，故校验这一对。
if(NOT EXISTS "${STE_SHARED_UI_DIR}/theme/Theme.qml" OR NOT EXISTS "${STE_SHARED_UI_DIR}/theme/qmldir")
  message(FATAL_ERROR
    "[viewer] 共享 UI 基座目录缺失：${STE_SHARED_UI_DIR}\n"
    "  主题真源在 Stelarith-shared-qml/ui/theme/（两端共用，pragma Singleton，qmldir 声明 Theme）。\n"
    "  请整体 clone 根仓库 Stelarith-xingjikong-qt，不要单独 clone 本子工程。")
endif()

add_custom_command(TARGET viewer-qt POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${STE_SHARED_ISLAND_DIR}" "$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith/island"
  # 2026-10-16（island 系统性升级 · C 档）：拷完立刻逐文件校验副本与真源一致。
  # copy_directory 是「复制」语义 —— 产物里残留旧件、或有人手改副本，构建都**不会**报错，
  # 但运行期跑的就是另一份岛（日志正常、界面不对，最难查）。不一致就让构建 FAIL。
  COMMAND "${CMAKE_COMMAND}"
          "-DSRC=${STE_SHARED_ISLAND_DIR}"
          "-DDST=$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith/island"
          "-DLABEL=island"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/../Stelarith-shared-qml/scripts/check-island-parity.cmake"
  # 共享 UI 基座落回 components/（Md3.qml + md3/），消费方相对导入逐字不变。
  # ⚠️ 用 SUBSET 模式：components/ 与各端自有组件**混放**（Btn/Card/AccountMenu…），
  #    那里多出来的件是本工程自己的，不是残留 —— 严格双向会误报。
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
          "${STE_SHARED_UI_DIR}" "$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith/components"
  COMMAND "${CMAKE_COMMAND}"
          "-DSRC=${STE_SHARED_UI_DIR}"
          "-DDST=$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith/components"
          "-DLABEL=ui"
          "-DSUBSET=ON"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/../Stelarith-shared-qml/scripts/check-island-parity.cmake"
  VERBATIM)

# ── QML 同步（每次构建都跑 · 2026-10-20 补）─────────────────────────────
# 【为什么需要】上面那条是 POST_BUILD：**只有链接时才执行**。于是「只改 QML、没改 C++」
#   的构建（ninja: no work to do）会让 build/qml 停在旧版本 —— 探针测到旧件、
#   出包打旧件，两者都静默。2026-10-20 P1 骨架实测撞到：新增 AdminDashboard /
#   ClassDetailPage 后 ninja 报 no work to do，build/qml/Stelarith/pages 里只有
#   旧三页，靠手动 cp + sha256 绕过 —— 这类"改了不生效"最容易在出包那天才被发现。
# 【与 control 端的关系】stelarith-agent-qt 那边的 agent_qml_sync 是同一套路的
#   先例（2026-10-20 补），这里照搬，保持两端构建行为一致：sync 只负责同步，
#   parity 校验仍留在 POST_BUILD（只在链接后跑，够且便宜）。
# 【代价】copy_directory 是 copy-if-different 语义，几十个小文件，忽略不计。
add_custom_target(viewer_qml_sync ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${STE_SHARED_ISLAND_DIR}" "$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith/island"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${STE_SHARED_UI_DIR}" "$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith/components"
  # ── QtShadcn 模块（2026-11-01 · 生产事故修复）────────────────────────
  # 整个模块目录（qmldir + qml/ + qmltypes + **QtShadcnplugin.dll**）拷进
  # import path（qml/Stelarith/）下的 QtShadcn/ 子目录 —— QML 引擎按
  # 「import path/<模块名>/qmldir」找模块，插件 dll 与 qmldir 同目录。
  # ⚠️ 用 copy_directory 而不是逐文件：模块目录里 plugin dll 是**构建产物**，
  #    名字随 Qt 版本变（QtShadcnplugin.dll），逐文件点名迟早漏。
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${STE_QTSHADCN_MODULE_DIR}" "$<TARGET_FILE_DIR:viewer-qt>/qml/Stelarith/QtShadcn"
  # 类型库 QtShadcn.dll（C++ 侧注册的类型）放 exe 旁。
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_BINARY_DIR}/QtShadcn.dll" "$<TARGET_FILE_DIR:viewer-qt>/QtShadcn.dll"
  COMMENT "[viewer] 同步 qml/ + 共享 island/ + 共享 ui/ + QtShadcn 模块（每次构建，防「改了不生效」）"
  VERBATIM)
# 让 exe 依赖它：保证任何一次构建（包括 ninja 默认目标）都先同步过 QML。
add_dependencies(viewer-qt viewer_qml_sync)
# QtShadcn 的 QML plugin dll 必须进构建图（2026-11-01 · 生产事故修复）：
# qt_add_qml_module 生成的 QtShadcnplugin target **默认不被任何 target 依赖**，
# 于是它从没被编译过，而 qmldir 里写着 "optional plugin QtShadcnplugin"
# ⇒ 运行时找不到 plugin → MainWindow 加载失败 → viewer 打不开。
add_dependencies(viewer-qt QtShadcnplugin)

# ── 共享语音算法（P1，2026-10-21）：OpusCodec 一份实现，agent/viewer 同源 ──
# Qt-free 的纯 C++（shared/audio），Windows 与 Android 同一份代码。
target_sources(viewer-qt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared/audio/opus_codec.cpp")
target_include_directories(viewer-qt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared/audio")

target_link_libraries(viewer-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::WebSockets Qt6::Widgets Qt6::Svg Qt6::Multimedia opus QtShadcn)

# ── OpenSSL 运行时 DLL 拷进 build/（2026-10-20 恢复 720bd8d 的规则）──────────
# 历史：0.6.33 的 `720bd8d` 加过这条规则（作者当时的实证：干净重建后 makensis 报
#   "File ..\build\libssl-3-x64.dll -> no files found"）。2026-10-20 排查发现它**整段不见了**
#   —— 远控 VNC 线重写本文件时被带走 ⇒ 0.6.45 出包现场又回到"手工拷 DLL 才出得来"。
# 规律（值得记）：凡是**打包清单显式 File 引用、而 windeployqt 不认识**的非 Qt 运行时件
#   （OpenSSL 是典型：Qt6Network 的 openssl TLS 后端按需 load，不是静态导入），
#   都必须由构建自己拷；否则"干净构建一次缺一次"，只有出包那一步才会炸。
# 来源：STE_OPENSSL_ROOT（env 可覆盖，取 <root>/bin 或 <root>/../bin）+ PostgreSQL 默认安装。
if(NOT STE_IS_ANDROID)
  set(_ste_ossl_hints "")
  if(DEFINED ENV{STE_OPENSSL_ROOT} AND NOT "$ENV{STE_OPENSSL_ROOT}" STREQUAL "")
    list(APPEND _ste_ossl_hints
         "$ENV{STE_OPENSSL_ROOT}/bin" "$ENV{STE_OPENSSL_ROOT}/../bin")
  endif()
  if(DEFINED STE_OPENSSL_ROOT AND NOT "${STE_OPENSSL_ROOT}" STREQUAL "")
    list(APPEND _ste_ossl_hints
         "${STE_OPENSSL_ROOT}/bin" "${STE_OPENSSL_ROOT}/../bin")
  endif()
  list(APPEND _ste_ossl_hints
       "C:/Program Files/PostgreSQL/17/bin"
       "C:/Program Files/PostgreSQL/16/bin")
  find_file(STE_LIBSSL_DLL NAMES libssl-3-x64.dll HINTS ${_ste_ossl_hints})
  find_file(STE_LIBCRYPTO_DLL NAMES libcrypto-3-x64.dll HINTS ${_ste_ossl_hints})
  if(STE_LIBSSL_DLL AND STE_LIBCRYPTO_DLL)
    foreach(_ossl "${STE_LIBSSL_DLL}" "${STE_LIBCRYPTO_DLL}")
      get_filename_component(_ossl_name "${_ossl}" NAME)
      add_custom_command(TARGET viewer-qt POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_ossl}" "$<TARGET_FILE_DIR:viewer-qt>"
        COMMENT "[viewer] 拷贝 ${_ossl_name} → 构建输出目录（OpenSSL 运行时）" VERBATIM)
    endforeach()
    message(STATUS "[viewer] OpenSSL 运行时将随构建部署：${STE_LIBSSL_DLL}")
  else()
    message(WARNING
      "[viewer] 没找到 libssl-3-x64.dll / libcrypto-3-x64.dll —— makensis 打 viewer 包会失败。\n"
      "  处置：设 STE_OPENSSL_ROOT=<PostgreSQL 安装根>/lib 后重新 configure，或手工把两个 DLL 拷进 build/。")
  endif()
endif()

# 2026-10-11：Android 麦克风用 NDK AAudio（纯 C++、无 Multimedia 依赖——两 kit 都没装）；
# sysroot 默认 API 21 库目录没有 libaaudio.so，显式找 API 26+（AAudio 最低要求）的系统库。
# 桌面仍走 winmm waveIn（无需额外链接）。
if(STE_IS_ANDROID)
  # AAudio 库：sysroot 默认 API 21 目录没有；用 API 34（真机 Android 14 全 symbol；
  # setInputPreset 之类 API 28+ 的符号在 26 缺）。链接 26 会产生 undefined symbol。
  find_library(STE_AAUDIO_LIB aaudio
    PATHS "$ENV{ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/34"
    "$ENV{ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/28"
    "$ENV{ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/26"
    NO_DEFAULT_PATH)
  if(NOT STE_AAUDIO_LIB)
    message(FATAL_ERROR "[viewer] Android AAudio 库找不到（NDK sysroot API34/28/26 无 libaaudio.so）")
  endif()
  target_link_libraries(viewer-qt PRIVATE ${STE_AAUDIO_LIB})
  # RECORD_AUDIO 权限请求走 QtAndroidPrivate（QtCore 私有头）→ 链 QtCorePrivate 拿到 include 路径
  target_link_libraries(viewer-qt PRIVATE Qt6::CorePrivate)
endif()
# 2026-10-12 修（审计）：管理端是老师机**双击运行**的桌面程序，OFF = 控制台子系统 ⇒
# 双击时弹一个黑窗口（用户观感"这软件怎么带个 DOS 框"）。日志现已落盘（main.cpp 的
# ensureLogOpen → viewer-run.log），不再依赖控制台看输出 ⇒ 改 ON（GUI 子系统，不弹窗口）。
set_target_properties(viewer-qt PROPERTIES WIN32_EXECUTABLE ON)

# 2026-10-09：viewerbackend.cpp 2700+ 行单编译单元峰值超内存（C1060）。
# 只此文件降优化（/Od），其余文件保持 Release 优化——编译内存峰值降下来，功能零影响。
# 2026-10-11：/Od 是 MSVC flag，Android 交编译（clang）跳过。
if(MSVC)
  set_source_files_properties(src/viewerbackend.cpp PROPERTIES COMPILE_FLAGS "/Od /Ob0")
endif()

# ── 2026-10-18 远控：libvncclient（VNC over WebSocket）──────────────────────
# 默认 OFF：未提供预编译库时 vnc_client.cpp 走安全桩，工程仍可干净构建。
# 打开方式：把 LibVNCClient.lib（及依赖）放 third_party/libvncclient/lib/、
#           把 rfb/rfbclient.h 等头文件放 third_party/libvncclient/include/，
#           然后 -DSTE_USE_LIBVNCCLIENT=ON 重 configure。
# ⚠️ 链接方式须与 agent 一致（x64 / msvc2022 / 与本工程相同的 CRT）：
#    静态库用 /MD 或 /MT 必须与 lib 编译时一致，否则 LNK2038 运行库不匹配。
option(STE_USE_LIBVNCCLIENT "启用 libvncclient 远控（需预编译库）" OFF)
set(STE_LIBVNCCLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvncclient"
    CACHE PATH "libvncclient 预编译库根目录（含 include/ 与 lib/）")
if(STE_USE_LIBVNCCLIENT)
  if(NOT EXISTS "${STE_LIBVNCCLIENT_DIR}/include/rfb/rfbclient.h")
    message(FATAL_ERROR
      "[viewer] STE_USE_LIBVNCCLIENT=ON 但找不到 ${STE_LIBVNCCLIENT_DIR}/include/rfb/rfbclient.h\n"
      "  请把 LibVNC 头文件放到 third_party/libvncclient/include/rfb/")
  endif()
  target_include_directories(viewer-qt PRIVATE "${STE_LIBVNCCLIENT_DIR}/include")
  file(GLOB STE_LIBVNCCLIENT_LIBS "${STE_LIBVNCCLIENT_DIR}/lib/*.lib")
  if(NOT STE_LIBVNCCLIENT_LIBS)
    message(FATAL_ERROR
      "[viewer] ${STE_LIBVNCCLIENT_DIR}/lib/ 下没有 .lib（LibVNCClient.lib 及其依赖）")
  endif()
  target_link_libraries(viewer-qt PRIVATE ${STE_LIBVNCCLIENT_LIBS} ws2_32)
  target_compile_definitions(viewer-qt PRIVATE STE_USE_LIBVNCCLIENT)
  message(STATUS "[viewer] libvncclient 已启用：${STE_LIBVNCCLIENT_DIR}")
else()
  message(STATUS "[viewer] libvncclient 未启用（STE_USE_LIBVNCCLIENT=OFF）——远控为安全桩")
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
                   "D:/QTqwq/6.12.0/msvc2022_64/bin")
if(NOT STE_WINDEPLOYQT_EXECUTABLE)
  message(WARNING "[viewer] 没找到 windeployqt，Qt 运行时不会自动部署。"
                  "若 exe 启动即退出且没有任何日志，先手工在 build 目录跑一次 "
                  "windeployqt.exe viewer-qt.exe")
endif()
# ... Android 不用 windeployqt（androiddeployqt 管）；且它读 Android .so 会报 DOS header
# 2026-10-11：加桌面条件
if(NOT STE_IS_ANDROID AND STE_WINDEPLOYQT_EXECUTABLE)
  # 2026-10-26 P4：加 --qmldir —— windeployqt 默认只扫 exe 的 PE 导入表，
  #   而 QML 的 `import QtMultimedia` 不产生任何 C++ 符号（MSVC 会把未引用的
  #   Qt6Multimedia.dll 导入整个丢掉），结果是"链接成功了、装完却放不出来"：
  #   Qt6Multimedia.dll 与 plugins/multimedia/*.dll 都不会被部署。
  #   给 --qmldir 让它扫 QML import，才能把 Multimedia 运行时 + 媒体后端插件带上。
  add_custom_command(TARGET viewer-qt POST_BUILD
    COMMAND "${STE_WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:viewer-qt>"
            --no-translations
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    VERBATIM)
endif()

# ═══ 2026-10-11：Android target —— 手机 App 并入管理端（用户裁决）═══
# 与桌面并行：桌面全部逻辑不动，ANDROID 时额外配置。QML 在 Android 用 qrc 嵌入
#（APK 无磁盘 build 目录；桌面保留磁盘热更）。
if(STE_IS_ANDROID)
  # ① QML 整树嵌入 qrc（递归 GLOB，杜绝手写漏项；PREFIX "/" + BASE qml → qrc:/Main.qml）
  # 2026-10-11：`qml/*.qml` 只匹配根层 → mobile/ 子目录漏打包（手机壳加载不到）。
  # 改 `qml/**/*.qml`（跨目录）+ 根层兜底。
  file(GLOB_RECURSE STE_VIEWER_QML_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/**/*.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/**/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.js")
  qt_add_resources(viewer-qt "viewerqml" PREFIX "/" BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    FILES ${STE_VIEWER_QML_FILES})
  # 2026-10-16（island 系统性升级 · C 档）：岛**不在**源 qml/ 树里（真源见
  #   STE_SHARED_ISLAND_DIR），上面那个 GLOB 收不到它 ⇒ APK 首次安装（qml-live 尚未
  #   建）时走 qrc 分支，InteractDialog 的 `import "island"` 解析失败。
  #   单独挂一个资源组：PREFIX /island + BASE 真源目录 ⇒ 岛落在 qrc:/island/，
  #   与相对导入 `import "island"` 的解析位置一致。
  file(GLOB_RECURSE STE_SHARED_ISLAND_FILES CONFIGURE_DEPENDS
    "${STE_SHARED_ISLAND_DIR}/*.qml")
  qt_add_resources(viewer-qt "viewerisland" PREFIX "/island"
    BASE "${STE_SHARED_ISLAND_DIR}" FILES ${STE_SHARED_ISLAND_FILES})
  # 2026-10-16（U1）：Md3 基座同理**不在**源 qml/ 树里 ⇒ 上面那个 GLOB 收不到它。
  #   Main.qml 在 qrc:/Main.qml 上做 `import "components"`，解析到 qrc:/components/，
  #   故资源组 PREFIX /components + BASE 共享 ui 目录 ⇒ Md3.qml 落 qrc:/components/Md3.qml、
  #   md3/* 落 qrc:/components/md3/*，与相对导入位置一致。
  # ⚠️ 2026-10-16（U2）：必须**同时收 .js** —— UiRegistry.qml 依赖 UiContract.js（纯函数契约）。
  #   只 GLOB *.qml 的话 APK 里契约缺失，注册表整块解析失败（桌面走 copy_directory 不受影响，
  #   所以只有手机端会中招 —— 正是最不容易发现的那类）。
  # ⚠️ 2026-10-25（Md3 改 pragma Singleton）：**必须同时收 theme/qmldir**。
  #   Md3.qml 移入 ui/theme/ 后靠 qmldir 声明 `singleton Md3 1.0 Md3.qml`；
  #   qrc 里少了这个文件 ⇒ `import "components/theme"` 解析不到 singleton
  #   ⇒ 手机端启动即白屏（桌面走 copy_directory 整目录拷贝，不受影响 ——
  #   又一类"只有手机端会中招"的坑，与上面 UiContract.js 同源）。
  file(GLOB_RECURSE STE_SHARED_UI_FILES CONFIGURE_DEPENDS
    "${STE_SHARED_UI_DIR}/*.qml"
    "${STE_SHARED_UI_DIR}/*.js"
    "${STE_SHARED_UI_DIR}/*/qmldir")
  qt_add_resources(viewer-qt "viewerui" PREFIX "/components"
    BASE "${STE_SHARED_UI_DIR}" FILES ${STE_SHARED_UI_FILES})
  # ② 2026-10-18：Android 不再链 libdatachannel（WebRTC 已移除）。
  #    远控走 libvncclient（VNC over WS），与桌面同一套代码路径；Android 侧
  #    只有在 STE_USE_LIBVNCCLIENT=ON 且提供 Android 版 .a/.so 时才真正启用，
  #    否则 vnc_client.cpp 的安全桩照常编译（手机上远控暂不可用，其余功能不受影响）。
  # 2026-10-11：Android 令牌编译期注入（mobile 同款：打包前设 STE_VIEWER_TOKEN_BUILD）
  if(DEFINED ENV{STE_VIEWER_TOKEN_BUILD} AND NOT "$ENV{STE_VIEWER_TOKEN_BUILD}" STREQUAL "")
    target_compile_definitions(viewer-qt PRIVATE
      "STE_VIEWER_TOKEN_CMake=\"$ENV{STE_VIEWER_TOKEN_BUILD}\"")
    message(STATUS "[viewer] Android 令牌已从 STE_VIEWER_TOKEN_BUILD 注入")
  else()
    message(WARNING "[viewer] 未设 STE_VIEWER_TOKEN_BUILD——Android 无静态令牌，未登录时云端会拒")
  endif()
  # 2026-10-11：Android 云端地址覆盖（联调/隔离实例用；默认仍生产 wss）
  if(DEFINED ENV{STE_VIEWER_URL_BUILD} AND NOT "$ENV{STE_VIEWER_URL_BUILD}" STREQUAL "")
    target_compile_definitions(viewer-qt PRIVATE
      "STE_VIEWER_URL_CMake=\"$ENV{STE_VIEWER_URL_BUILD}\"")
    message(STATUS "[viewer] Android 云端地址已覆盖为 $ENV{STE_VIEWER_URL_BUILD}")
  endif()
  # 2026-10-11 诊断：桌面 Main.qml 手机 GPU SIGABRT → 软件渲染测试（STE_SOFT_RENDER_BUILD=1）
  if(DEFINED ENV{STE_SOFT_RENDER_BUILD} AND "$ENV{STE_SOFT_RENDER_BUILD}" STREQUAL "1")
    target_compile_definitions(viewer-qt PRIVATE STE_SOFT_RENDER_BUILD)
    message(STATUS "[viewer] Android 软件渲染已开启（QT_QUICK_BACKEND=software）")
  endif()
  # ③ Android 打包目录（QT_ANDROID_PACKAGE_SOURCE_DIR 已在 qt_add_executable 前设置）
else()
  # 桌面：QML 磁盘拷贝保留（见上面的 add_custom_command post-build）
endif()

# ── RBAC（2026-10-20）：PermissionManager 独立单测 ──
# 默认关闭，不污染主构建；需要时 `cmake -DSTE_BUILD_PERM_TEST=ON` 后编 perm_test 目标。
# 也可直接用 scripts/_build_perm_test.py 走本工程既有 MSVC/Qt 工具链一键编跑。
option(STE_BUILD_PERM_TEST "Build PermissionManager standalone unit test" OFF)
if(STE_BUILD_PERM_TEST)
  add_executable(perm_test
    src/permission_manager_test.cpp
    src/permission_manager.cpp)
  target_link_libraries(perm_test PRIVATE Qt6::Core)
endif()
