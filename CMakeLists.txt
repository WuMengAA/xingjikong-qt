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
# 2026-10-11 +Quick/QuickWidgets：灵动岛视觉层改 QML（用户裁决），QQuickWidget 承载。
#   只加 QuickWidgets 不够 —— QML 运行时（QtQml/QtQuick 的 DLL + qml/ 插件目录）
#   必须随包走，否则装到干净机器上"岛白屏"（本机有 Qt 环境所以看不出来）。
# 注：WebEngine 的按需加载不需要额外组件 —— 不加 WebEngineQuick（它在动态构建下会死锁，见
#     src/main.cpp 的 ensureWebEngine 注释），只要不在 main 里提前 new QWebEngineView 即可。
# 2026-10-13 审计 #25：与 viewer-qt 对齐 Qt ≥ 6.12（项目两端共用一个 Qt 6.12 安装，
# 声明一致避免"配置阶段两端行为不同"——此前 viewer 声明 6.5、control 无版本声明）。
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets WebSockets Network Svg Qml Quick QuickWidgets)

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
set(AGENT_VERSION "0.6.44")
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
  # ── WebRTC 推流（2026-10-09：libdatachannel 换 other webrtc，探针全绿）──
  src/ldc_streamer.cpp
  src/ldc_streamer.h
  # ── 远控隧道（2026-10-18 · RFB over WS）：agent 第二条 WS + 本地 VNC 纯字节桥 ──
  src/tunnel_agent.cpp
  src/tunnel_agent.h
  # ── 系统主音量（2026-10-11 Core Audio/WASAPI，独立编译单元隔离 SDK 头）──
  src/system_volume.cpp
  src/system_volume.h
  # ── 小鲸鱼音效（2026-10-11 · dsh-whale-widget 移植：winmm PlaySound 播 wav）──
  src/whale_audio.cpp
  src/whale_audio.h
  # ── 版本资源（configure_file 从 src/app.rc.in 生成，见上面的版本块）──
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
target_link_libraries(stelarith-agent-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebSockets Qt6::Network Qt6::Svg)
# 2026-10-09：edge_tts（waveOut 朗读）/ microphone（waveIn 采集）走 winmm
target_link_libraries(stelarith-agent-qt PRIVATE winmm)
# 2026-10-11：灵动岛 QML（QQuickWidget 承载 qml/island/IslandRoot.qml）
target_link_libraries(stelarith-agent-qt PRIVATE Qt6::Qml Qt6::Quick Qt6::QuickWidgets)
# ── 2026-10-09 WebRTC 去 WebEngine 底层：libdatachannel 静态链（并行方案，
#    STE_USE_LDC_RTC=1 默认走 ldc，=0 回退 WebEngine）────────────────────
# 库：_probe/ldc_libs（重建产物，命名 datachannel.lib/juice.lib）+ OpenSSL
# 头：_probe/libdatachannel-master/include（rtc/rtc.hpp）
# 2026-10-13 审计 #26：OpenSSL 路径可覆盖（STE_OPENSSL_ROOT；默认 PG17 兼容现有机器）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/datachannel.lib")
  set(STE_OPENSSL_ROOT "$ENV{STE_OPENSSL_ROOT}" CACHE PATH "OpenSSL lib 目录（含 libssl.lib/libcrypto.lib）")
  if(NOT STE_OPENSSL_ROOT)
    set(STE_OPENSSL_ROOT "C:/Program Files/PostgreSQL/17/lib" CACHE PATH "" FORCE)
  endif()
  target_include_directories(stelarith-agent-qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/libdatachannel-master/include")
  target_link_libraries(stelarith-agent-qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/datachannel.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/srtp2.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/juice.lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../_probe/ldc_libs/usrsctp.lib"
    "${STE_OPENSSL_ROOT}/libssl.lib"
    "${STE_OPENSSL_ROOT}/libcrypto.lib"
    ws2_32 bcrypt crypt32 userenv secur32)
  target_compile_definitions(stelarith-agent-qt PRIVATE RTC_STATIC)
  message(STATUS "[agent] libdatachannel 已链入（RTC_STATIC）")
else()
  # ── 2026-10-18 · 库缺失时的**真保护**（用户裁决：方案 A / 可逆摘除）──────────
  # 原先这里只打一条 WARNING，但 ldc_streamer.cpp 仍在源文件列表里、也没有任何
  # #if 保护 ⇒「不链接库」并不等于「不编译它」，结果是找不到 rtc/rtc.hpp 直接编不过。
  # 这种"看着有保护、实际没有"的分支比不写更糟：会让人以为缺库能正常降级。
  #
  # 现在：不定义 RTC_STATIC ⇒ ldc_streamer.cpp 走文件末尾的 #else 空实现分支，
  #      对外接口保持一致 ⇒ main.cpp 零改动。WebRTC 推流不可用，画面走 JPEG 帧路径
  #      （JPEG 是独立路径，见 makeFrameBytes，不受影响）。
  # 还原：把 datachannel.lib / srtp2.lib / juice.lib / usrsctp.lib 放回 _probe/ldc_libs/
  #      （外加 _probe/libdatachannel-master/include），重新 configure 即自动回到
  #      RTC_STATIC 分支，**源码一个字都不用改**。
  message(WARNING
    "[agent] libdatachannel 缺失（_probe/ldc_libs/datachannel.lib 不存在）"
    " ⇒ 本次构建不含 WebRTC 推流：ldc_streamer 编译为空实现，画面监控走 JPEG 帧。"
    " 需要推流时把库放回 _probe/ldc_libs/ 并重新 configure。")
endif()

# GUI 子系统：常驻托盘程序不该挂着控制台黑窗（日志已落盘到 agent-qt.log，不靠 stdout）
set_target_properties(stelarith-agent-qt PROPERTIES WIN32_EXECUTABLE ON)

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

