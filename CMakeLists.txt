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

# D1: Core+Gui（抓屏/JPEG）｜D2: +WebSockets（连云端发帧）｜D5: +Widgets（托盘常驻，见下）
# 2026-10-04 +Network：OOBE 绑定界面要 POST 站点消费班级接入码（/api/device/activate）
# 注：WebEngine 的按需加载不需要额外组件 —— 不加 WebEngineQuick（它在动态构建下会死锁，见
#     src/main.cpp 的 ensureWebEngine 注释），只要不在 main 里提前 new QWebEngineView 即可。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebSockets Network Svg)

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
set(AGENT_VERSION "0.6.24-rc.7")
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
  src/schedule/profile.h
  # ── 远程终端（2026-10-06 加：REPL 命令行，需确认 + 单会话）──
  src/terminal_console.cpp
  src/terminal_console.h
  # ── 星璃账号绑定（2026-10-06 加：OAuth + 设备票，零手填接入）──
  src/oauthbind.cpp
  src/oauthbind.h
  # ── 考试模式（2026-10-06 加：全屏拦截 + 白名单轮询 + 计时，设计文档 3.7 第一版）──
  src/exam_mode.cpp
  src/exam_mode.h
  # ── 语音对讲（2026-10-07 加：waveOut 播放，设计文档 3.3 第一版）──
  src/audio_player.cpp
  src/audio_player.h
  # ── 屏幕广播（2026-10-07 加：全屏显示老师屏幕，设计文档《屏幕广播》第一版）──
  src/broadcast_view.cpp
  src/broadcast_view.h
  # ── 被控端灵动岛（2026-10-07 加：活动统一经过灵动岛，形状/位置转变 + 缓动 + 深浅色 + 交互穿透）──
  src/island.cpp
  src/island.h
  # ── 通知体系新组件（2026-10-09 接线：通知规格 §6 全部独立组件，DeepSeek Harness 造）──
  src/danmaku.cpp
  src/danmaku.h
  src/loop_reminder.cpp
  src/loop_reminder.h
  src/spectrum_widget.cpp
  src/spectrum_widget.h
  src/noise_monitor.cpp
  src/noise_monitor.h
  src/notification_gate.cpp
  src/notification_gate.h
  src/edge_tts.cpp
  src/edge_tts.h
  src/marquee_text.cpp
  src/marquee_text.h
  src/island_clock.cpp
  src/island_clock.h
  src/microphone.cpp
  src/microphone.h
  # ── WebRTC 推流（2026-10-09：libdatachannel 换 other webrtc，探针全绿）──
  src/ldc_streamer.cpp
  src/ldc_streamer.h
  # ── 系统主音量（2026-10-11 Core Audio/WASAPI，独立编译单元隔离 SDK 头）──
  src/system_volume.cpp
  src/system_volume.h
  # ── 版本资源（configure_file 从 src/app.rc.in 生成，见上面的版本块）──
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
target_link_libraries(stelarith-agent-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebSockets Qt6::Network Qt6::Svg)
# 2026-10-09：edge_tts（waveOut 朗读）/ microphone（waveIn 采集）走 winmm
target_link_libraries(stelarith-agent-qt PRIVATE winmm)
# ── 2026-10-09 WebRTC 去 WebEngine 底层：libdatachannel 静态链（并行方案，
#    STE_USE_LDC_RTC=1 默认走 ldc，=0 回退 WebEngine）────────────────────
# 库：_probe/ldc_libs（重建产物，命名 datachannel.lib/juice.lib）+ PG17 OpenSSL
# 头：_probe/libdatachannel-master/include（rtc/rtc.hpp）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/datachannel.lib")
  target_include_directories(stelarith-agent-qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/libdatachannel-master/include")
  target_link_libraries(stelarith-agent-qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/datachannel.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/srtp2.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/juice.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/usrsctp.lib"
    "C:/Program Files/PostgreSQL/17/lib/libssl.lib"
    "C:/Program Files/PostgreSQL/17/lib/libcrypto.lib"
    ws2_32 bcrypt crypt32 userenv secur32)
  target_compile_definitions(stelarith-agent-qt PRIVATE RTC_STATIC)
  message(STATUS "[agent] libdatachannel 已链入（RTC_STATIC）")
else()
  message(WARNING "[agent] _probe/ldc_libs/datachannel.lib 缺失——ldc_streamer 编译会失败，请先重建 libdatachannel")
endif()

# GUI 子系统：常驻托盘程序不该挂着控制台黑窗（日志已落盘到 agent-qt.log，不靠 stdout）
set_target_properties(stelarith-agent-qt PROPERTIES WIN32_EXECUTABLE ON)

# 2026-10-08 引入 lucide（morphicons 同款 24×24 stroke 风格，ISC）SVG 图标，
# 编译进 exe（qrc 前缀 /icons），部署不带额外文件、颜色运行时按主题染。
file(GLOB AGENT_ICON_SVGS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*.svg")
qt_add_resources(stelarith-agent-qt "agenticons" PREFIX "/icons" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons" FILES ${AGENT_ICON_SVGS})
