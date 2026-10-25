cmake_minimum_required(VERSION 3.21)
# RC：让 CMake 认得 .rc（版本资源）。不加这一项，下面把 app.rc 列进源文件会报
# "RC language not enabled"。
project(stelarith-agent-qt LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 源码含中文注释：不加 /utf-8 时 MSVC 会按 GBK 解读，偶发乱码/报错
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ── in-agent VNC Server（libvncserver，2026-10-18）──
# 默认 OFF：未放置第三方预编译库时不影响现有构建。开启需 third_party/libvncserver/{include,lib}。
# 链接方式假设静态 .lib + /MD（与 Qt 6.12 msvc2022 同 CRT）；若你的库是 /MT 或动态 .dll，
# 见下方链接块注释调整。
option(STE_USE_LIBVNCSERVER "启用被控端 in-agent VNC Server（需 third_party/libvncserver 预编译库）" OFF)

# D1: Core+Gui（抓屏/JPEG）｜D2: +WebSockets（连云端发帧）｜D5: +Widgets（托盘常驻，见下）
# 2026-10-04 +Network：OOBE 绑定界面要 POST 站点消费班级接入码（/api/device/activate）
# 2026-10-11 +Quick/QuickWidgets：灵动岛视觉层改 QML（用户裁决），QQuickWidget 承载。
#   只加 QuickWidgets 不够 —— QML 运行时（QtQml/QtQuick 的 DLL + qml/ 插件目录）
#   必须随包走，否则装到干净机器上"岛白屏"（本机有 Qt 环境所以看不出来）。
# 注：WebEngine 已彻底移除（2026-10-18 清道夫），不再依赖 QWebEngineView / WebEngineQuick。
#   远程桌面改走 in-agent VNC Server（libvncserver，见 src/vnc_server.*），不引入任何浏览器引擎。
# 2026-10-13 审计 #25：与 viewer-qt 对齐 Qt ≥ 6.12（项目两端共用一个 Qt 6.12 安装，
# 声明一致避免"配置阶段两端行为不同"——此前 viewer 声明 6.5、control 无版本声明）。
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets WebSockets Network Svg Qml Quick QuickWidgets)

# ── libopus（P1 语音对讲，静态链接）2026-10-21 ────────────────────────────
# 源码 vendoring 在仓库根 third_party/opus（BSD）。关 shared/tests/programs，
# 只编静态库 opus 供本目标链接，避免拖慢构建。
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/opus ${CMAKE_BINARY_DIR}/_opus)

# ── 版本资源 + 应用图标（2026-10-07 补）────────────────────────────────────
# 补之前 exe 是「无版本资源的裸 PE」：任务管理器细节页空白、右键属性没有描述、
# 控制面板卸载项里的版本位显示 0.0.0.0。
#
# 版本号**不手写第二份**：从 src/main.cpp 的 kAppVersion 读出来跟这里核一遍再灌进 .rc。
# 手写两份的下场管理端已经示范过一次（CMakeLists 0.6.3 / main.cpp 0.6.2，日志与
# 「关于」互相打脸），所以不一致直接 FATAL_ERROR，别留给人去发现。
# 命名同为 DeepSeek Harness 同款：正式 X.Y.Z / 内测 X.Y.Z-alpha.N / 候选 X.Y.Z-rc.N
# （阶段序 alpha < rc < 正式；号同核心递增、换核心归零、转正即剥离后缀）。
# 成熟度标记不进版本号，归 ota.json 的 mandatory / notes 管。完整规则见 ../../docs/版本号命名规范-2026-10-07.md。
set(AGENT_VERSION "0.6.48")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" _agent_src)
if(_agent_src MATCHES "kAppVersion[ \t\r\n]*=[ \t\r\n]*\"([^\"]+)\"")
  set(_src_agent_version "${CMAKE_MATCH_1}")
else()
  message(FATAL_ERROR "在 src/main.cpp 里找不到 kAppVersion 字面量，版本资源没法生成")
endif()
# 命名守门（2026-10-07）：只认三种形态 —— 正式 X.Y.Z / 内测 X.Y.Z-alpha.N / 候选 X.Y.Z-rc.N
# ⚠️ CMake 的正则引擎不吃非捕获组 (?:...)，所以上面只做宽松抽取，格式校验放这里单独做；
#    （阶段号从 1 起，alpha 早于 rc）；顺带把 alpha.0、裸后缀、四段式挡在构建外。
if(NOT _src_agent_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-(alpha|rc)\\.[1-9][0-9]*)?$")
  message(FATAL_ERROR
    "版本号 ${_src_agent_version} 不合命名规范：只认 0.6.23（正式）、0.6.23-alpha.1（内测）或" " 0.6.23-rc.1（候选）这种形态；\n"
    "阶段号从 1 起、不接受裸后缀、不接受第四段数字。详见 ../../docs/版本号命名规范-2026-10-07.md")
endif()
if(NOT _src_agent_version STREQUAL "${AGENT_VERSION}")
  message(FATAL_ERROR
    "版本号漂移：CMakeLists.txt 是 ${AGENT_VERSION}，src/main.cpp 的 kAppVersion 是 ${_src_agent_version}。\n"
    "改版本只改 CMakeLists.txt 的 set(AGENT_VERSION ...) 这一行（kAppVersion 那份会自动跟随）。")
endif()

# 版本串可以带 -rc.N（显示用：.rc 字符串字段 FileVersion 会写 "0.6.23-rc.1"），
# 但 VS 的 FILEVERSION 是**二进制字段，只认四段纯数字** —— 直接把整个串按 "." 拆开，
# 来个 "0.6.23-rc.1" 会拆出 "22-rc" 这种段。所以先剥出核心三段 X.Y.Z，第四段固定 0。
string(REGEX REPLACE "^(.+)-(alpha|rc)\\.([0-9]+)$" "\\1" AGENT_CORE "${AGENT_VERSION}")
if (AGENT_VERSION STREQUAL "${AGENT_CORE}")
  set(AGENT_STAGE "")                  # 正式版：没有阶段后缀
  set(AGENT_STAGE_N "")
else()
  string(REGEX REPLACE "^.*-(alpha|rc)\\.([0-9]+)$" "\\1" AGENT_STAGE "${AGENT_VERSION}")
  string(REGEX REPLACE "^.*-(alpha|rc)\\.([0-9]+)$" "\\2" AGENT_STAGE_N "${AGENT_VERSION}")
endif()
message(STATUS "[agent] 版本 ${AGENT_VERSION}" "（核心 ${AGENT_CORE}，阶段 ${AGENT_STAGE} ${AGENT_STAGE_N}）")
string(REPLACE "." ";" _agent_v "${AGENT_CORE}")
list(LENGTH _agent_v _agent_vn)
while(_agent_vn LESS 4)
  list(APPEND _agent_v "0")
  list(LENGTH _agent_v _agent_vn)
endwhile()
list(GET _agent_v 0 _agent_v0)
list(GET _agent_v 1 _agent_v1)
list(GET _agent_v 2 _agent_v2)
list(GET _agent_v 3 _agent_v3)
set(RC_VERSION_QUAD "${_agent_v0},${_agent_v1},${_agent_v2},${_agent_v3}")

configure_file(src/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
# ⚠️ 必须在 rc 之前就位：rc.exe 的工作目录是构建目录，.rc 里的 ICON "app.ico" 是相对名。
configure_file(src/app.ico "${CMAKE_CURRENT_BINARY_DIR}/app.ico" COPYONLY)

# installer.nsi 的版本常量也由这里出（2026-10-07）：NSIS 那侧不再手抄一份版本号，
# 用 !include "build\version.nsh" 吃这个生成文件 —— 从源头掐掉"exe 0.6.5 / 安装器 0.6.5"
# 靠人记着同步的老问题。NSIS 的 !include 相对路径是相对"脚本所在目录"（不是当前工作目录），
# 而 installer.nsi 和 build/ 都在工程根下，所以写相对名就够；绝对路径反而会踩转义。
configure_file(src/version.nsh.in "${CMAKE_CURRENT_BINARY_DIR}/version.nsh" @ONLY)

add_executable(stelarith-agent-qt
  src/main.cpp
  src/agent_ui.h      # U5-A3：QML 控制面板（header-only，Q_OBJECT 经 AUTOMOC；显式列入让 AUTOMOC 必扫）
  src/singleinstance.cpp
  src/singleinstance.h
  # ── 课表时钟模块（2026-10-06 加：上下课提醒）──
  src/schedule_clock.cpp
  src/schedule_clock.h
  # ── 大屏课表窗口（2026-10-06 加：今日课表展示）──
  src/schedule_view.cpp
  src/schedule_view.h
  src/schedule/profile_repository.cpp
  src/schedule/profile_repository.h
  # ── 插件宿主（P2 · 2026-10-14：进程外插件 ABI）──
  src/plugin/pluginhost.cpp
  src/plugin/pluginhost.h
  # ── 内置模块层（2026-10-16「模块化插件」）：编译进 agent 的官方模块
  #    （U盘 / VoiceHub 联动 / 灵动岛多界面…），与进程外插件共用形状与门控。
  #    ⚠️ AUTOMOC：含 Q_OBJECT 的头**必须显式列入**，否则不生成 moc 产物 ⇒ LNK2001。
  src/modules/modulehost.cpp
  src/modules/modulehost.h
  src/modules/usbmodule.cpp
  src/modules/usbmodule.h
  # ── VoiceHub 联动（2026-10-17）：只读展示 VoiceHub 权威广播状态 ──
  src/modules/voicehubmodule.cpp
  src/modules/voicehubmodule.h
  # ── 广播社成员（2026-10-23 #广播站移交）：独立身份登录 + 排期代理 ──
  src/modules/broadcastclubmodule.cpp
  src/modules/broadcastclubmodule.h
  src/schedule/profile.h
  # ── 远程终端（2026-10-06 加：REPL 命令行，需确认 + 单会话）──
  src/terminal_console.cpp
  src/terminal_console.h
  # ── 星璃账号绑定（2026-10-06 加：OAuth + 设备票，零手填接入）──
  src/oauthbind.cpp
  src/oauthbind.h
  # 2026-10-20：首次启动引导（OOBE Wizard）数据面。界面在 qml/oobe/（StackView 四步）。
  # Q_OBJECT 类必须显式列入源列表，AUTOMOC 才会为它生成 moc（只被 main.cpp include 不可靠）。
  src/oobe_wizard.cpp
  src/oobe_wizard.h
  # ── 考试模式（2026-10-06 加：全屏拦截 + 白名单轮询 + 计时，设计文档 3.7 第一版）──
  src/exam_mode.cpp
  src/exam_mode.h

  src/proc_guard.cpp
  src/proc_guard.h
  # ── 语音对讲（2026-10-07 加：waveOut 播放，设计文档 3.3 第一版）──
  src/audio_player.cpp
  src/audio_player.h
  # ── 屏幕广播（2026-10-07 加：全屏显示老师屏幕，设计文档《屏幕广播》第一版）──
  src/broadcast_view.cpp
  src/broadcast_view.h
  # ── 被控端灵动岛（2026-10-07 加：活动统一经过灵动岛，形状/位置转变 + 缓动 + 深浅色 + 交互穿透）──
  src/island.cpp
  src/island.h
  # ── 灵动岛 QML 桥（2026-10-11 加：视觉层改 QML，本桥把状态推给 QML / 接 QML 回调）──
  src/island_bridge.cpp
  src/island_bridge.h
  # ── SVG 图标 provider（QML Image 的 image://svgicon 协议；与 viewer 同份实现）──
  src/svgiconprovider.cpp
  src/svgiconprovider.h
  # ── 通知体系新组件（2026-10-09 接线：通知规格 §6 全部独立组件，DeepSeek Harness 造）──
  src/danmaku.cpp
  src/danmaku.h
  src/loop_reminder.cpp
  src/loop_reminder.h
  src/noise_monitor.cpp
  src/noise_monitor.h
  src/notification_gate.cpp
  src/notification_gate.h
  src/edge_tts.cpp
  src/edge_tts.h
  src/microphone.cpp
  src/microphone.h
  # ── 媒体工具层（2026-10-22 · AGENTS.md §10 第 1 刀）：ffmpeg 定位 / 摄像头绑定 / 设备解析 / 错误归类 ──
  src/media/MediaTools.cpp
  src/media/MediaTools.h
  # ── 媒体会话控制（2026-10-22 · 第 2 刀）：录像启停 / 播放停止 ──
  src/media/MediaSession.cpp
  src/media/MediaSession.h
  # ── 相机指令（2026-10-23 · 第 3 刀）：executeAction 的相机六件套分支体 ──
  src/media/CameraActions.cpp
  src/media/CameraActions.h
  # ── 班级域缩略图上报（2026-10-23 · #459）：定时抓小图 → class-event{kind:thumbnail} ──
  src/media/ThumbnailReporter.cpp
  src/media/ThumbnailReporter.h
  # ── 指令执行结果（2026-10-23 · 第 3 刀）：跨 TU 共享，原在 main.cpp 匿名域 ──
  src/core/ExecOut.h
  # ── 本地功能开关（2026-10-23 · 第 4 刀地基）：原嵌在 main.cpp 匿名域内 ──
  src/core/AgentSettings.h
  # ── 通用进程工具（2026-10-22 · 第 2 刀）：同步等进程退出但不卡主线程（录像/OTA/远控/终端共用）──
  src/core/ProcessUtil.cpp
  src/core/ProcessUtil.h
  # ── 发布中心卡片 → 通知参数（2026-10-24 · P1）：纯映射逻辑，渲染复用 notifyFromParams ──
  src/core/BlackboardCard.cpp
  src/core/BlackboardCard.h
  # ── 远控隧道（2026-10-18 · RFB over WS）：agent 第二条 WS + 本地 VNC 纯字节桥 ──
  src/vnc_bridge.cpp
  src/vnc_bridge.h
  # ── in-agent VNC Server 底层（2026-10-18）：抓屏 + 输入注入（不依赖 libvncserver，常编）──
  src/screen_capture.cpp
  src/screen_capture.h
  # ── DXGI Desktop Duplication 抓屏（2026-10-19）：主后端，init 失败回落 GDI ──
  src/screen_capture_dxgi.cpp
  src/screen_capture_dxgi.h
  src/input_inject.cpp
  src/input_inject.h
  # ── 系统主音量（2026-10-11 Core Audio/WASAPI，独立编译单元隔离 SDK 头）──
  src/system_volume.cpp
  src/system_volume.h
  # ── 小鲸鱼音效（2026-10-11 · dsh-whale-widget 移植：winmm PlaySound 播 wav）──
  src/whale_audio.cpp
  src/whale_audio.h
  # ── 版本资源（configure_file 从 src/app.rc.in 生成，见上面的版本块）──
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
# ── 共享语音算法（P1，2026-10-21）：OpusCodec 一份实现，agent/viewer 同源 ──
# Qt-free 的纯 C++（shared/audio），只依赖 libopus。放仓库根保证两端算法绝对一致。
target_sources(stelarith-agent-qt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared/audio/opus_codec.cpp")
target_include_directories(stelarith-agent-qt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared/audio")

target_link_libraries(stelarith-agent-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebSockets Qt6::Network Qt6::Svg opus)
# 2026-10-09：edge_tts（waveOut 朗读）/ microphone（waveIn 采集）走 winmm
target_link_libraries(stelarith-agent-qt PRIVATE winmm)
# 2026-10-18：in-agent VNC Server 底层（screen_capture 用 GDI BitBlt/CreateDIBSection；input_inject 用 SendInput）
target_link_libraries(stelarith-agent-qt PRIVATE gdi32 user32)
# 2026-10-19：DXGI Desktop Duplication 抓屏（screen_capture_dxgi.cpp 用 D3D11/DXGI/COM）
if(WIN32)
  target_link_libraries(stelarith-agent-qt PRIVATE d3d11 dxgi ole32)
endif()
# 2026-10-22：系统媒体识别 SMTC（winrt/Windows.Media.Control.h，C++/WinRT 消费端）
#   windowsapp.lib = WinRT 的 umbrella import lib（RequestAsync / RoInitialize 等）。
#   SDK 的 cppwinrt 头目录已由 scripts/_build_agent.py 的 INCLUDE 提供；
#   用 VS 生成器时由 WindowsTargetPlatform 自动带上，不需要额外 include 路径。
if(WIN32)
  target_sources(stelarith-agent-qt PRIVATE src/smtc_monitor.cpp src/smtc_monitor.h)
  target_link_libraries(stelarith-agent-qt PRIVATE windowsapp)
  # ⚠️ 本文件必须按 C++20 编：工程级是 C++17，而 C++/WinRT 在 C++17 下会
  #    落到 <experimental/coroutine>（需要 /await），MSVC 14.51 直接硬失败
  #    （error C2338：experimental/coroutine 已被弃用，要求改 C++20 标准库）。
  #    只给这一个文件提标准：全工程提 C++20 会让 270+ 个既有编译单元一起变行为，
  #    /std 覆盖）警告。注意：源文件级 COMPILE_FEATURES cxx_std_20 在 MSVC 上
  #    **不会**真的改写命令行（实测仍留 -std:c++17），必须用 COMPILE_OPTIONS 硬给。
  set_source_files_properties(src/smtc_monitor.cpp PROPERTIES COMPILE_OPTIONS "/std:c++20")
endif()
# 2026-10-11：灵动岛 QML（QQuickWidget 承载 qml/island/IslandRoot.qml）
target_link_libraries(stelarith-agent-qt PRIVATE Qt6::Qml Qt6::Quick Qt6::QuickWidgets)

# GUI 子系统：常驻托盘程序不该挂着控制台黑窗（日志已落盘到 agent-qt.log，不靠 stdout）
set_target_properties(stelarith-agent-qt PROPERTIES WIN32_EXECUTABLE ON)

# ── in-agent VNC Server 链接（2026-10-18）────────────────────────────────────
# 仅 STE_USE_LIBVNCSERVER=ON 时介入。要求 third_party/libvncserver/{include/rfb/rfb.h, lib/LibVNCServer.lib}。
# 依赖假设（与 Qt 6.12 msvc2022_64 同 ABI）：x64 / msvc2022 / /MD（动态 CRT）/ 静态 .lib。
#   · 若你的库是 /MT：把下方 target_link_libraries 的库换成对应 /MT 构建的 .lib 即可（CRT 须一致，否则 LNK2038）。
#   · 若你的库带 zlib/lzo/jpeg 等依赖未静态编入：在此追加对应 .lib（如 zlib.lib）。
#   · 若为动态库（.dll）：链接 LibVNCServer.lib（导入库），并把 .dll 随安装包部署到 exe 旁。
if(STE_USE_LIBVNCSERVER)
  set(_lvncs "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libvncserver")
  if(NOT EXISTS "${_lvncs}/include/rfb/rfb.h")
    message(FATAL_ERROR "STE_USE_LIBVNCSERVER=ON 但找不到 ${_lvncs}/include/rfb/rfb.h，请先放置预编译 libvncserver")
  endif()
  target_include_directories(stelarith-agent-qt PRIVATE "${_lvncs}/include")
  # ws2_32：libvncserver 直接用 winsock（socket/bind/select）。Qt6::Network 虽会间接带，
  # 但库是独立静态件，依赖不靠 Qt 的传递性保证 —— 显式链上，避免 LNK2019。
  target_link_libraries(stelarith-agent-qt PRIVATE "${_lvncs}/lib/LibVNCServer.lib" ws2_32)
  target_compile_definitions(stelarith-agent-qt PRIVATE STE_USE_LIBVNCSERVER)
  target_sources(stelarith-agent-qt PRIVATE src/vnc_server.cpp) # 依赖 rfb.h，仅开启时编入
endif()

# 2026-10-08 引入 lucide（morphicons 同款 24×24 stroke 风格，ISC）SVG 图标，
# 编译进 exe（qrc 前缀 /icons），部署不带额外文件、颜色运行时按主题染。
file(GLOB AGENT_ICON_SVGS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.svg")
qt_add_resources(stelarith-agent-qt "agenticons" PREFIX "/icons" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons" FILES ${AGENT_ICON_SVGS})

# ── 2026-10-11 · 灵动岛 QML 资源 ────────────────────────────────────────
# 鲸鱼娘素材（清单元数据 #1）：cp 自 Stelarith-viewer-qt/resources/whale/（源未动）。
# 编进 exe（qrc 前缀 /whale）—— QML 里写 qrc:/whale/dsniang1.png。
# ⚠️ 不能走磁盘：这两张是装饰图，跟着 exe 走最省心（装机不用带额外文件）。
file(GLOB AGENT_WHALE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/whale/*")
if(AGENT_WHALE_FILES)
  qt_add_resources(stelarith-agent-qt "agentwhale" PREFIX "/whale" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/whale" FILES ${AGENT_WHALE_FILES})
  message(STATUS "[agent] 鲸鱼娘素材已编入（${AGENT_WHALE_FILES}）")
else()
  message(WARNING "[agent] resources/whale/ 为空——灵动岛展开态的鲸鱼娘会是空白")
endif()

# ── 灵动岛 QML：磁盘加载 + 构建时拷贝 ───────────────────────────────────
# ⚠️ 走**磁盘**而不是 qrc：改外观不用重编译（与 viewer 同一个约定，项目笔记 §3）。
#    代价是 build/ 下必须有 qml/ 目录，且打包要带上（NSIS 那边已按目录整包带）。
#    POST_BUILD 覆盖拷贝：源码目录改了 qml，重编译即生效；改副本可热更（重启 exe）。
#
# 2026-10-15（用户点单「抽共享目录」）：**灵动岛视觉层唯一真源**
#   在根仓库 <repo-root>/Stelarith-shared-qml/island/，被控端与管理端共用同一份 .qml。
#   这里在拷完 qml/ 之后**再拷一遍共享 island 覆盖/补齐** qml/island/ ——
#   island.cpp 的加载路径（qml/island/IslandRoot.qml）一个字符都不用改。
#   好处：改一次外观两端同时生效，不会再出现两份实现各自漂移（这正是抽共享的目的）。
# ⚠️ 共享件是根仓库的东西 ⇒ **必须整体 clone 根仓库**；单独 clone 本子工程会缺件，
#    下面显式 FATAL_ERROR 拦住，别让它变成一个运行期"鲸鱼娘/岛白屏"的哑巴故障。
set(STE_SHARED_ISLAND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Stelarith-shared-qml/island")
if(NOT EXISTS "${STE_SHARED_ISLAND_DIR}/IslandRoot.qml")
  message(FATAL_ERROR
    "[agent] 共享灵动岛目录缺失：${STE_SHARED_ISLAND_DIR}\n"
    "  灵动岛 QML 真源在 Stelarith-shared-qml/island/（两端共用）。\n"
    "  请整体 clone 根仓库 Stelarith-xingjikong-qt，不要单独 clone 本子工程。")
endif()

# 2026-10-16（U1 共享 UI 基座 · 用户点单「插件化思维，统一界面结构」）：
#   Md3 设计系统（Md3.qml 令牌 + md3/ 12 组件）真源收到根仓库 Stelarith-shared-qml/ui/。
#   本工程当前界面＝灵动岛本身（C++ QQuickWidget 加载 qml/island/IslandRoot.qml），
#   尚不直接消费 Md3；但 Md3 是**两端唯一视觉模板**（2026-10-13 用户裁决），
#   存量界面后续迁移要 import 它 ⇒ 仍按同模式拷回 qml/components/，让 `import "components"`
#   / `import "../components/md3"` 之类的既有写法在将来搬过来时不需要改路径。
set(STE_SHARED_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Stelarith-shared-qml/ui")
if(NOT EXISTS "${STE_SHARED_UI_DIR}/Md3.qml")
  message(FATAL_ERROR
    "[agent] 共享 UI 基座目录缺失：${STE_SHARED_UI_DIR}\n"
    "  Md3 设计系统真源在 Stelarith-shared-qml/ui/（两端共用）。\n"
    "  请整体 clone 根仓库 Stelarith-xingjikong-qt，不要单独 clone 本子工程。")
endif()

add_custom_command(TARGET stelarith-agent-qt POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/qml"
          "$<TARGET_FILE_DIR:stelarith-agent-qt>/qml"
  # 共享 island 覆盖：唯一真源，本地 qml/island/ 若残留也被这份盖掉。
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${STE_SHARED_ISLAND_DIR}"
          "$<TARGET_FILE_DIR:stelarith-agent-qt>/qml/island"
  # 2026-10-16（island 系统性升级 · C 档）：拷完立刻逐文件校验副本与真源一致。
  # copy_directory 不会因残留件/手改副本而报错，但运行期跑的就是另一份岛 ⇒ 不一致直接 FAIL。
  COMMAND ${CMAKE_COMMAND}
          "-DSRC=${STE_SHARED_ISLAND_DIR}"
          "-DDST=$<TARGET_FILE_DIR:stelarith-agent-qt>/qml/island"
          "-DLABEL=island"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/../Stelarith-shared-qml/scripts/check-island-parity.cmake"
  # 共享 UI 基座（Md3）落回 qml/components/。
  # ⚠️ SUBSET 模式：components/ 允许存放本工程自有件，多出来的不算残留。
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${STE_SHARED_UI_DIR}"
          "$<TARGET_FILE_DIR:stelarith-agent-qt>/qml/components"
  COMMAND ${CMAKE_COMMAND}
          "-DSRC=${STE_SHARED_UI_DIR}"
          "-DDST=$<TARGET_FILE_DIR:stelarith-agent-qt>/qml/components"
          "-DLABEL=ui"
          "-DSUBSET=ON"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/../Stelarith-shared-qml/scripts/check-island-parity.cmake"
  COMMENT "[agent] 拷贝 qml/ + 共享 island/ + 共享 ui/ → 构建输出目录（灵动岛视觉层）")

# ── QML 同步（每次构建都跑，2026-10-20 补）─────────────────────────────
# 【为什么需要】上面那条是 POST_BUILD：**只有链接时才执行**。于是「只改 QML、没改 C++」
#   的构建（ninja: no work to do）会让 build/qml 停在旧版本 —— 探针测到旧件、出包打旧件，
#   两者都静默（2026-10-20 立项时实测：改了 oobe 的 QML，ninja 说 no work to do，
#   build/qml 里还是老文件）。这类"改了不生效"最容易在出包那天才被发现。
# 【代价】copy_directory 走 copy-if-different 语义，几十个小文件，忽略不计。
add_custom_target(agent_qml_sync ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/qml"
          "$<TARGET_FILE_DIR:stelarith-agent-qt>/qml"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${STE_SHARED_ISLAND_DIR}"
          "$<TARGET_FILE_DIR:stelarith-agent-qt>/qml/island"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${STE_SHARED_UI_DIR}"
          "$<TARGET_FILE_DIR:stelarith-agent-qt>/qml/components"
  COMMENT "[agent] 同步 qml/ + 共享 island/ + 共享 ui/（每次构建，防「改了 QML 不生效」）"
  VERBATIM)
# 让 exe 依赖它：保证任何一次构建（哪怕只是 ninja 全量默认目标）都先同步过 QML。
add_dependencies(stelarith-agent-qt agent_qml_sync)

# ── Qt 运行时部署（windeployqt + 部署完整性断言，2026-10-20 补）───────────────
# 【为什么必须有】出包现场实证：本工程从无 windeployqt 规则 ⇒ build/qml 下**只有**我们自己
#   拷的 app QML；Qt 的 QML 模块目录要么没有、要么是空目录（`build/qml/QtQuick/Controls`
#   实测 0 文件），exe 旁也没有 Qt6QuickControls2.dll。而共享 Md3Btn 自 2026-10-19 起
#   `import QtQuick.Controls`，被 5 个设置页 import ⇒ 换 qml 到生产后，面板一打开就
#   `qtquickcontrols2plugin.dll 找不到指定的模块`。声明合法、编译零警告、主日志只剩一行，
#   与 2026-10-16 真机事故同一类 —— 这类缺件必须由构建自己兜住。
# 【为什么 --qmldir 不可省】windeployqt 靠**扫 QML 源码的 import** 决定部署哪些模块；
#   不传 --qmldir 时它只能看到 exe，扫不到 qml/ 里的模块依赖 —— 这正是
#   "QtQuick/Controls 目录存在但空洞"的来源（目录是别的模块顺带建的）。
#   这里把 app qml + 共享 island + 共享 ui 三处源码目录都指上。
# 【为什么还要断言】windeployqt 静默失败（工具版本/路径不对）时构建照样 exit 0；
#   于是加一道部署后校验：源码 import 的每个 Qt 模块必须真有实现，缺一个就 FAIL。
find_program(STE_WINDEPLOYQT_EXECUTABLE
             NAMES windeployqt windeployqt6
             HINTS "$ENV{QTDIR}/bin"
                   "D:/QTqwq/6.12.0/msvc2022_64/bin")
if(NOT STE_WINDEPLOYQT_EXECUTABLE)
  message(WARNING
    "[agent] 没找到 windeployqt：Qt 运行时与 QML 模块不会自动部署。\n"
    "  后果：装到干净机器上会「岛白屏 / 面板 is not a type」。\n"
    "  手工补：windeployqt.exe --no-translations --qmldir qml stelarith-agent-qt.exe")
else()
  # 扫「源码里实际 import 的 Qt 模块」——写成配置期计算、构建期校验，随 QML 变化自动跟随。
  file(GLOB_RECURSE STE_AGENT_QML_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml"
       "${STE_SHARED_ISLAND_DIR}/*.qml"
       "${STE_SHARED_UI_DIR}/*.qml")
  set(STE_REQUIRED_QML_MODULES "")
  foreach(_qml ${STE_AGENT_QML_SOURCES})
    file(STRINGS "${_qml}" _imports REGEX "^[ \t]*import[ \t]+Qt")
    foreach(_line ${_imports})
      string(REGEX REPLACE "^[ \t]*import[ \t]+([A-Za-z0-9_.]+).*$" "\\1" _mod "${_line}")
      # 只收带点的（QtQuick.Controls / QtQuick.Layouts…）；裸 QtQuick、QtQml 是核心模块，
      # windeployqt 必部署，且个别版本在 qml/ 下无独立目录，不纳入断言以免误报。
      if(_mod MATCHES "^Qt[A-Za-z0-9_]+\\.[A-Za-z0-9_.]+$")
        list(APPEND STE_REQUIRED_QML_MODULES "${_mod}")
      endif()
    endforeach()
  endforeach()
  if(STE_REQUIRED_QML_MODULES)
    list(REMOVE_DUPLICATES STE_REQUIRED_QML_MODULES)
    list(SORT STE_REQUIRED_QML_MODULES)
  endif()
  message(STATUS "[agent] QML 依赖模块（构建后校验部署完整性）：${STE_REQUIRED_QML_MODULES}")

  add_custom_command(TARGET stelarith-agent-qt POST_BUILD
    COMMAND "${STE_WINDEPLOYQT_EXECUTABLE}" "$<TARGET_FILE:stelarith-agent-qt>"
            --no-translations
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
            --qmldir "${STE_SHARED_ISLAND_DIR}"
            --qmldir "${STE_SHARED_UI_DIR}"
    COMMENT "[agent] windeployqt：部署 Qt 运行时 + QML 模块（qml/ 下）"
    VERBATIM)
  add_custom_command(TARGET stelarith-agent-qt POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DDEPLOY_QML=$<TARGET_FILE_DIR:stelarith-agent-qt>/qml"
            "-DREQUIRED_MODULES=${STE_REQUIRED_QML_MODULES}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-qml-modules-deployed.cmake"
    COMMENT "[agent] 校验部署的 QML 模块完整性（缺件即 FAIL）"
    VERBATIM)
endif()

