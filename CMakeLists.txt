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
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets WebSockets Network Svg Qml Quick QuickControls2 QuickWidgets)

# ── QtShadcn（2026-10-29 · UI 全量替换 P0 接入）────────────────────────────
# ⚠️ 铁律变更（用户 2026-10-29 裁决）：被控端原「零 QtQuick.Controls 依赖、
#    手写 ash 组件库」铁律**废止**，改为两端全量采用 QtShadcn。
#    故上面 find_package 新增 QuickControls2 组件、下面链接 Qt6::QuickControls2。
#    代价：被控端引入 QQC 依赖（体积上升），收益：两端设计体系与组件实现统一。
# 作为子项目引入 ⇒ examples/tests 默认关闭。
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/qtshadcn ${CMAKE_BINARY_DIR}/_qtshadcn)

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
set(AGENT_VERSION "0.6.62")
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
  # 2026-10-27 第五刀：闹钟域从 main.cpp 拆出（自带 3 个 g_alarm* 变量、零隐式依赖，
  # TTS 由 init() 注入回调）。main.cpp 8432 → 8247 行。
  src/domain/alarm_domain.cpp
  src/domain/alarm_domain.h
  src/domain/notify_domain.cpp
  src/domain/notify_domain.h
  src/annotate/annotate_domain.cpp
  src/annotate/annotate_domain.h
  # 2026-10-27 H2：墨迹渲染/页模型从 annotate_domain.cpp 拆出（撞 600 行红线），
  # 拆口 = Canvas(纯 Qt) / Overlay(Win32) 之间，两边零共享状态。
  src/annotate/annotate_canvas.cpp
  # 2026-10-27 G3：时间同步域（SNTP + 人工偏移），独立文件，不进 main.cpp。
  src/domain/timesync_domain.cpp
  src/domain/settings_domain.cpp     # 2026-10-28 模块三：安全模式/存储统计/备份/重置
  src/domain/timesync_domain.h
  src/domain/panel_domain.cpp
  src/domain/panel_domain.h
  # 2026-10-27 Phase 3：定时自动化规则引擎（纯模型 + 1s 调度；Q_OBJECT 头须显式列入让 AUTOMOC 必扫）
  src/automation/automation_engine.cpp
  src/automation/automation_engine.h
  # 2026-10-27 I（第七刀）：PPT 放映控制域（Win32 COM + IDispatch，PowerPoint / WPS 演示）。
  # 只用 GetActiveObject 连已在运行的实例，绝不 CoCreateInstance 拉起程序。
  src/ppt/ppt_domain.cpp
  src/ppt/ppt_domain.h
  src/ppt/ppt_com.cpp
  src/ppt/ppt_com.h
  src/agent_ui.h      # U5-A3：QML 控制面板（header-only，Q_OBJECT 经 AUTOMOC；显式列入让 AUTOMOC 必扫）
  src/island/agent_shell_reload.cpp  # 2026-10-31 QML 热重载（单独 .cpp：agent_ui.h 已卡 707 行白名单）
  src/agent_record_ui.h  # 2026-10-31 拆分第一刀：记录/自检/插件/时段的 Q_OBJECT 基类。
                         # ⚠️ 必须显式列入（同 agent_ui.h 的理由）：AUTOMOC 对「只被别的头 include
                         # 的头文件」解析缓存不可靠，漏列 ⇒ moc 产物不生成 ⇒ 链接期 LNK2001
                         # （staticMetaObject / metaObject / infoChanged 全 undefined，2026-10-31 实测）。
  src/system/singleinstance.cpp
  src/system/singleinstance.h
  # ── 课表时钟模块（2026-10-06 加：上下课提醒）──
  src/schedule/schedule_clock.cpp
  src/schedule/schedule_clock.h
  # ── 大屏课表窗口（2026-10-06 加：今日课表展示）──
  src/schedule/schedule_view.cpp
  src/schedule/schedule_view.h
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
  # ⚠️ 2026-10-26 从 src/modules/ 移到 src/：**它不是插件模块**。
  #    BroadcastClubModule 只继承 QObject、不实现 ModuleInterface（没有 manifest()），
  #    也不走 registerModule —— 是 main.cpp 直接 new 出来挂给 AgentShell 的业务对象。
  #    放在 src/modules/ 里会被 module-registry 门禁按「插件模块」扫描，恒报
  #    「manifest 缺 id / 没 registerModule」（7 通过 3 失败的既有红）。
  src/system/broadcastclub.cpp
  src/system/broadcastclub.h
  src/schedule/profile.h
  # ── 远程终端（2026-10-06 加：REPL 命令行，需确认 + 单会话）──
  src/terminal/terminal_console.cpp
  src/terminal/terminal_console.h
  # ── 星璃账号绑定（2026-10-06 加：OAuth + 设备票，零手填接入）──
  src/oobe/oauthbind.cpp
  src/oobe/oauthbind.h
  # 2026-10-20：首次启动引导（OOBE Wizard）数据面。界面在 qml/oobe/（StackView 四步）。
  # Q_OBJECT 类必须显式列入源列表，AUTOMOC 才会为它生成 moc（只被 main.cpp include 不可靠）。
  src/oobe/oobe_wizard.cpp
  src/oobe/oobe_wizard.h
  # ── 考试模式（2026-10-06 加：全屏拦截 + 白名单轮询 + 计时，设计文档 3.7 第一版）──
  src/system/exam_mode.cpp
  src/system/exam_mode.h

  src/system/proc_guard.cpp
  src/system/proc_guard.h
  # ── 语音对讲（2026-10-07 加：waveOut 播放，设计文档 3.3 第一版）──
  src/media/audio_player.cpp
  src/media/audio_player.h
  # ── 屏幕广播（2026-10-07 加：全屏显示老师屏幕，设计文档《屏幕广播》第一版）──
  src/ui/broadcast_view.cpp
  src/ui/broadcast_view.h
  # ── 被控端灵动岛（2026-10-07 加：活动统一经过灵动岛，形状/位置转变 + 缓动 + 深浅色 + 交互穿透）──
  src/island/island.cpp
  src/island/island.h
  # ── 灵动岛 QML 桥（2026-10-11 加：视觉层改 QML，本桥把状态推给 QML / 接 QML 回调）──
  src/island/island_bridge.cpp
  src/island/island_bridge.h
  # 2026-10-27（C 批）：岛「设」面方法（系统音量 / 通知音量 / 黑名单），从 bridge 拆出守 600 行红线
  src/island/island_bridge_settings.cpp
  # 2026-11-02：历史通知域（排序/筛选/已读/删除）与批注域（岛上的笔），再切两刀守 600 行红线
  src/island/island_bridge_history.cpp
  src/island/island_bridge_annotate.cpp
  # ── SVG 图标 provider（QML Image 的 image://svgicon 协议；与 viewer 同份实现）──
  src/ui/svgiconprovider.cpp
  src/ui/svgiconprovider.h
  # ── 通知体系新组件（2026-10-09 接线：通知规格 §6 全部独立组件，DeepSeek Harness 造）──
  src/system/danmaku.cpp
  src/system/danmaku.h
  src/automation/loop_reminder.cpp
  src/automation/loop_reminder.h
  src/noise/noise_monitor.cpp
  src/noise/noise_monitor.h
  # P0-2 分层（2026-10-30）：噪音事件策略与「采集」解耦，纯逻辑可单测
  src/noise/noise_incident.h
  src/noise/noise_incident_service.cpp
  src/noise/noise_incident_service.h
  # P0-2 分层第 2/3 层：值对象契约 + 可注入抓帧接口 + 桌面实现
  src/noise/noise_sample.h
  src/noise/noise_capture_sources.h
  src/noise/noise_desktop_snapshot.cpp
  src/noise/noise_desktop_snapshot.h
  # P0-2 分层第 3 层：存证编排（依赖注入，可在无摄像头/无桌面环境单测）
  src/noise/noise_evidence_capture.cpp
  src/noise/noise_evidence_capture.h
  # P0-2 分层第 3/4 层：可注入抓帧实现（摄像头）+ 上报 + 传输接口 + 协调器 + 装配工厂
  src/noise/noise_camera_snapshot.cpp
  src/noise/noise_camera_snapshot.h
  src/noise/noise_transport.h
  src/noise/noise_cloud_transport.cpp
  src/noise/noise_cloud_transport.h
  src/noise/noise_reporter.cpp
  src/noise/noise_reporter.h
  src/noise/noise_incident_pipeline.cpp
  src/noise/noise_incident_pipeline.h
  src/noise/noise_assembly.cpp
  src/noise/noise_assembly.h
  # ── 麦克风选择器（2026-10-31）：QML 单例 Stelarith.Mic（枚举/切换/持久化）──
  src/noise/mic_selector.cpp
  src/noise/mic_selector.h
  # ── 噪音提醒环（2026-10-31）：thresholdReached → 语音+事件（补 P0-1 无人接的信号）──
  src/noise/noise_announcer.cpp
  src/noise/noise_announcer.h
  src/cloud_event_bridge.h
  src/automation/notification_gate.cpp
  src/automation/notification_gate.h
  src/system/edge_tts.cpp
  src/system/edge_tts.h
  # 2026-10-30 main 拆分第一刀：TTS 统一播报（SAPI 兜底 + 音量序列 + token 去重 + 超时兜底）
  # 从 src/main.cpp 整块迁出。main 只留 speakText() 调用点；本模块零 main 全局依赖。
  src/tts/tts_speaker.cpp
  src/tts/tts_speaker.h
  src/media/microphone.cpp
  src/media/microphone.h
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
  # ── 远控 H264 推流（A 档第②步，2026-10-31）：MediaFoundation H264 硬编 + 定时抓帧/合成帧推流 ──
  src/media/H264EncoderMf.cpp
  src/media/H264Streamer.cpp
  # ── 远控 H264 推流 · 软件编码兜底（2026-11-01）：OpenH264 软编（MF 硬件协商成功却 0 产出时接管）
  #   + PixelConvert（BGRX→I420/NV12 转换单一真源，硬编/软编共用）
  src/media/H264EncoderOh264.cpp
  src/media/PixelConvert.cpp
  # ── 录像/班级监控上传（2026-10-30 main 拆分：#2/#3 上传簇迁出，原 main.cpp 7118-7161）──
  #   record_stop 后把本地 rec-*.mp4 POST 云端 /api/recordings/upload（Bearer 设备令牌）。
  #   g_uid 沿用 MediaSession.cpp 既有的「跨 TU 共享全局」约定（extern）。
  src/upload/recording_upload.cpp
  src/upload/recording_upload.h
  # ── OTA 灰度渠道持久化（2026-10-30 main 拆分：#12 迁出，原 main.cpp 1466-1515）──
  #   channelFilePath/resolvedChannel/persistChannel；agentEnvPath() 已提升为外部链接供本模块跨 TU 引用。
  src/ota/channel_persistence.cpp
  src/ota/channel_persistence.h
  # ── OTA 状态机 + 健康快照（2026-10-30 main 拆分：#4a 迁出，原 main.cpp 2189-2287/3688-3749）──
  #   otaLoadState/otaPaused/otaNoteFailure/otaNoteSuccess/otaReportHealth + 各路径；
  #   g_latestVer/g_channel/kAppVersion 提升文件作用域外部链接供本模块 extern 复用。
  src/ota/ota_state.cpp
  src/ota/ota_state.h
  # ── OTA 自更新动作层（2026-10-31 main 拆分 #15）：compareVersion…otaRollback + OtaDeps 注入 ──
  #   与 ota_state（状态机/路径）同域分工；startSelfUpdate/otaIsBusy 经 OtaDeps 注入。
  src/ota/ota_update.cpp
  src/ota/ota_update.h
  # ── OTA 下载簇（2026-10-31 main 拆分 #16）：startSelfUpdate/otaOnFinished/otaSelfCheck/otaStartPeriodicCheck ──
  #   护栏 → 拉包（含 Range 断点续传）→ 成品校验 → 回滚三件套 → 派重启助手 → 启安装包。
  src/ota/ota_download.cpp
  src/ota/ota_download.h
  # ── OTA 渠道自动晋升 · 端侧展示（2026-11-02 新建，非拆分产物）──
  #   把云端 latest.channel/publishedAt/promotion 翻译成人话 + 注册 QML 单例 Stelarith.Ota。
  #   独立成模块的两个理由：①它只管"展示"，不参与版本比较；②agent_ui.h / agent_panel.cpp
  #   都在超线白名单内（加一行即门禁 error），故入口做成零调用点的 QML 单例。
  src/ota/ota_channel_state.cpp
  src/ota/ota_channel_state.h
  # ── 自习模式（2026-10-30 main 拆分：#5 迁出，原 main.cpp 4261-4370）──
  #   sendNoiseAlert/studyOnSample/studyTriggerRecord + 自动录制计时器；状态全局（g_study*）
  #   仍驻 main，按既有 extern 模式（g_uid/agentEnvPath 同款）复用，纯搬迁零行为变化。
  src/study_mode/study_mode.cpp
  src/study_mode/study_mode.h
  # ── 灵动岛统一入口（2026-10-30 main 拆分：#7 迁出，原 main.cpp 3541-3808）──
  #   islandShow/islandShowAt/trayNotify/notifyFromParams；通知体系全局与 sendActionReceipt
  #   提升文件作用域外部链接供本模块 extern 复用（同 g_uid/agentEnvPath 模式）。
  src/notify/notify_entry.cpp
  src/notify/notify_entry.h
  # ── 通知提供方注册表（2026-11-01 · 顶替 ClassIsland 第 2 步）──
  #   提供方→渠道两级 + QSettings 持久化 + 门控入口；设置页数据由 settingsModel() 出。
  src/notify/provider_registry.cpp
  src/notify/provider_registry.h
  #   AgentUi 的「通知提供者」方法体（声明仍在 agent_ui.h；同 island_bridge_settings 范式）
  src/notify/agent_ui_providers.cpp
  # ── P3 长期记录落盘内核（2026-10-30 main 拆分：#6a 迁出，原 main.cpp 4036-4113）──
  #   dataRoot/appendJsonl/logNoiseSample/logStudyStart/logStudyStop；零 main 全局依赖。
  src/p3log/p3_log.cpp
  src/p3log/p3_log.h
  # ── P3 长期记录查询/展示（2026-10-30 main 拆分：#6b 迁出）──
  #   readJsonlDir/openFolder/openNoiseLogDialog/openStudyLogDialog/buildNoiseLogText/
  #   buildStudyLogText；dataRoot() 复用 p3_log（#6a），openFolder 与 main 侧共用。
  src/p3log/p3_reader.cpp
  src/p3log/p3_reader.h
  # ── P3 记录页结构化视图（2026-10-31：按天/按次汇总成"两列行"，给 QML 画）──
  #   与 p3_reader 分家：reader 管"怎么读"，record_view 管"读出来给界面什么形状"。
  src/p3log/p3_record_view.cpp
  src/p3log/p3_record_view.h
  # ── 远控会话收口 + 泄漏看门狗（2026-10-30 main 拆分：#8 迁出）──
  #   stopRemoteControlSession/reapRemoteControlIfStale/saveVncAccessLocally(+vncAccessPath)；
  #   远控会话状态全局（g_currentFps/g_vncSessionStartMs/g_lastVncDownMs/g_vncMegaDone）
  #   提升文件作用域外部链接供本模块 extern 复用（同 g_uid/agentEnvPath 模式）。
  src/remote/remote_control.cpp
  src/remote/remote_control.h
  # ── QML 控制面板数据接线（2026-10-30 main 拆分：#9 迁出，原 main.cpp 6388-6955）──
  #   buildSelfCheckItems/buildPluginList/pluginRegistryUrl/s_storeEntries/
  #   agentUiFill（AgentUi 的 friend）/agentUiWire；依赖 main 匿名域函数经
  #   g_panelDeps 注入（方案 B），状态全局按既有 extern 模式复用。
  # 2026-11-02 拆「产出数据 / 接信号」二分（原单文件 646 行超红线 600）：
  #   agent_panel_data.cpp — 三个 builder + 插件商店缓存 + agentUiFill
  #   agent_panel_internal.h — 两边共享的 include/extern（同一份，防两处漂移）
  src/ui/agent_panel.cpp
  src/ui/agent_panel_data.cpp
  src/ui/agent_panel.h
  src/ui/agent_panel_internal.h
  # ── 播放时段管理/调度/软控/文件接收（2026-10-30 main 拆分：#10 迁出，原 main.cpp 3777-5715）──
  #   executeAction（1938 行单函数）；依赖 main 匿名域符号经 g_actionDeps 注入（方案 B），
  #   状态全局（g_currentFps/g_logPath/g_mediaDir/…）按既有 extern 模式复用。
  src/action/execute_action.cpp
  src/action/execute_action.h
  # ── executeAction 按指令域拆分 #1（2026-11-02 · 屎山整理）：原 2029 行单函数 → 主分发 307 行 + 6 域 ──
  #   只搬家不改逻辑：54 个 `if (action == ...)` 块体逐字保留，唯一改动是每块首行插入
  #   `handled = true;`（告诉调用方这条指令归本域）。公共 include / extern 全局 / 各域声明
  #   集中到 execute_action_shared.h，避免"漏一个 include = 某域编不过"的拆分期专属故障。
  src/action/execute_action_shared.h
  src/action/execute_device.cpp
  src/action/execute_app.cpp
  src/action/execute_schedule.cpp
  src/action/execute_notice.cpp
  src/action/execute_media.cpp
  src/action/execute_file.cpp
  # ── 协议 v1 信封打包（2026-10-31 main 拆分 #12）：makeFrameBytes/makeVncFrameBytes/makeAudioFrameBytes ──
  #   kProtocolVersion 与上行序号计数器（g_vncUpSeq/g_audioUpSeq）随函数迁出；
  #   g_uid 按既有 extern 模式复用（main.cpp 文件作用域外部链接）。
  src/wire/frame_builder.cpp
  src/wire/frame_builder.h
  # ── 指令执行结果（2026-10-23 · 第 3 刀）：跨 TU 共享，原在 main.cpp 匿名域 ──
  src/core/ExecOut.h
  # ── 本地功能开关（2026-10-23 · 第 4 刀地基）：原嵌在 main.cpp 匿名域内 ──
  src/core/AgentSettings.h
  # ── 通用进程工具（2026-10-22 · 第 2 刀）：同步等进程退出但不卡主线程（录像/OTA/远控/终端共用）──
  src/core/ProcessUtil.cpp
  src/core/ProcessUtil.h
  # ── 进程/窗口只读检查（2026-10-31 main 拆分 #13）：snapshotProcessNames/enumWindowsCb/visibleWindowsCached/windowTitleOf/memMbOf ──
  #   纯 Win32 只读快照、零 main 全局依赖；execute_action.cpp 经 g_actionDeps 函数指针访问（无需改动）。
  src/core/proc_inspect.cpp
  src/core/proc_inspect.h
  # ── 调度/软控/进程策略引擎（2026-10-31 main 拆分 #14）：saveSchedules…protectedCategory + PolicyDeps 注入 ──
  #   本域安全关键（定时关机/重启、黑名单守护、去网）；enableShutdownPriv 经 PolicyDeps 注入；
  #   execute_action.cpp 经 g_actionDeps 函数指针访问（零改动）；snapshotProcessNames 直接 include proc_inspect。
  src/policy/policy_engine.cpp
  src/policy/policy_engine.h
  # ── 发布中心卡片 → 通知参数（2026-10-24 · P1）：纯映射逻辑，渲染复用 notifyFromParams ──
  src/core/BlackboardCard.cpp
  src/core/BlackboardCard.h
  # ── 远控隧道（2026-10-18 · RFB over WS）：agent 第二条 WS + 本地 VNC 纯字节桥 ──
  src/remote/vnc_bridge.cpp
  src/remote/vnc_bridge.h
  # ── in-agent VNC Server 底层（2026-10-18）：抓屏 + 输入注入（不依赖 libvncserver，常编）──
  src/capture/screen_capture.cpp
  src/capture/screen_capture.h
  # ── DXGI Desktop Duplication 抓屏（2026-10-19）：主后端，init 失败回落 GDI ──
  src/capture/screen_capture_dxgi.cpp
  src/capture/screen_capture_dxgi.h
  src/remote/input_inject.cpp
  src/remote/input_inject.h
  # ── 系统主音量（2026-10-11 Core Audio/WASAPI，独立编译单元隔离 SDK 头）──
  src/media/system_volume.cpp
  src/media/system_volume.h
  # ── 小鲸鱼音效（2026-10-11 · dsh-whale-widget 移植：winmm PlaySound 播 wav）──
  src/media/whale_audio.cpp
  src/media/whale_audio.h
  # ── 本地 API 鉴权网关（2026-10-31 · 三条已知地基缺口之一）──
  #   服务本机 127.0.0.1:8080（仅回环），token 头鉴权 + 限流 + 失败审计。
  #   localapi_auth 只依赖 QtCore（可单测）；localapi_server 用 QTcpServer 手写最小 HTTP。
  #   装配：main.cpp 启动路径调 localApiInit() 一行，业务全在本域（main 拆分纪律）。
  src/localapi/localapi_auth.cpp
  src/localapi/localapi_auth.h
  src/localapi/localapi_server.cpp
  src/localapi/localapi_server.h
  # ── 版本资源（configure_file 从 src/app.rc.in 生成，见上面的版本块）──
  ${CMAKE_CURRENT_BINARY_DIR}/app.rc
)
# ── 共享语音算法（P1，2026-10-21）：OpusCodec 一份实现，agent/viewer 同源 ──
# Qt-free 的纯 C++（shared/audio），只依赖 libopus。放仓库根保证两端算法绝对一致。
target_sources(stelarith-agent-qt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared/audio/opus_codec.cpp")
target_include_directories(stelarith-agent-qt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared/audio")

# ── OpenH264 软件编码兜底（2026-11-01）────────────────────────────────────
# 头文件只给 H264EncoderOh264.cpp 用（第三方头不进工程头文件，遵守 §10.4 前向声明纪律）；
# dll 在**运行时**动态加载（LoadLibrary+GetProcAddress），因此这里既不链接 .lib 也不拷贝到
# 链接期——POST_BUILD 会把 dll 拷到 exe 旁边，缺 dll 时软编自动跳过、回落 VNC。
set(STE_OPENH264_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/openh264")
target_include_directories(stelarith-agent-qt PRIVATE "${STE_OPENH264_DIR}")
if(EXISTS "${STE_OPENH264_DIR}/codec_api.h")
  add_custom_command(TARGET stelarith-agent-qt POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${STE_OPENH264_DIR}/openh264-2.7.0-win64.dll"
            "$<TARGET_FILE_DIR:stelarith-agent-qt>/openh264.dll"
    COMMENT "[agent] 拷贝 OpenH264 软件编码 dll（MF 硬件 0 产出时的兜底编码器）"
    VERBATIM)
else()
  message(WARNING
    "[agent] third_party/openh264 缺失或无 codec_api.h：软件编码兜底不可用（不影响 MF 硬编路径）。")
endif()

target_link_libraries(stelarith-agent-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebSockets Qt6::Network Qt6::Concurrent Qt6::Svg opus)
# 2026-10-09：edge_tts（waveOut 朗读）/ microphone（waveIn 采集）走 winmm
target_link_libraries(stelarith-agent-qt PRIVATE winmm)
# 2026-10-18：in-agent VNC Server 底层（screen_capture 用 GDI BitBlt/CreateDIBSection；input_inject 用 SendInput）
target_link_libraries(stelarith-agent-qt PRIVATE gdi32 user32)
# 2026-10-19：DXGI Desktop Duplication 抓屏（screen_capture_dxgi.cpp 用 D3D11/DXGI/COM）
if(WIN32)
  target_link_libraries(stelarith-agent-qt PRIVATE d3d11 dxgi ole32)
  # 2026-10-27（I 批）：PPT 域用 GetActiveObject（oleaut32）+ CLSIDFromProgID（ole32 已在上面）。
  # 不链 oleaut32 会 LNK2019：unresolved external symbol __imp_GetActiveObject。
  target_link_libraries(stelarith-agent-qt PRIVATE oleaut32)
  # 2026-10-31（A 档第①步）：MediaFoundation H264 编码器（H264EncoderMf.cpp 用 IMFTransform）。
  #   mfplat=核心；mf=MFT 枚举；mfuuid=MF_* GUID/属性键（MFVideoFormat_*/MF_MT_* 的导入库）。
  target_link_libraries(stelarith-agent-qt PRIVATE mfplat mf mfuuid)
endif()
# 2026-10-22：系统媒体识别 SMTC（winrt/Windows.Media.Control.h，C++/WinRT 消费端）
#   windowsapp.lib = WinRT 的 umbrella import lib（RequestAsync / RoInitialize 等）。
#   SDK 的 cppwinrt 头目录已由 scripts/_build_agent.py 的 INCLUDE 提供；
#   用 VS 生成器时由 WindowsTargetPlatform 自动带上，不需要额外 include 路径。
if(WIN32)
  target_sources(stelarith-agent-qt PRIVATE src/system/smtc_monitor.cpp src/system/smtc_monitor.h)
  target_link_libraries(stelarith-agent-qt PRIVATE windowsapp)
  # ⚠️ 本文件必须按 C++20 编：工程级是 C++17，而 C++/WinRT 在 C++17 下会
  #    落到 <experimental/coroutine>（需要 /await），MSVC 14.51 直接硬失败
  #    （error C2338：experimental/coroutine 已被弃用，要求改 C++20 标准库）。
  #    只给这一个文件提标准：全工程提 C++20 会让 270+ 个既有编译单元一起变行为，
  #    /std 覆盖）警告。注意：源文件级 COMPILE_FEATURES cxx_std_20 在 MSVC 上
  #    **不会**真的改写命令行（实测仍留 -std:c++17），必须用 COMPILE_OPTIONS 硬给。
  set_source_files_properties(src/system/smtc_monitor.cpp PROPERTIES COMPILE_OPTIONS "/std:c++20")
endif()
# 2026-10-11：灵动岛 QML（QQuickWidget 承载 qml/island/IslandRoot.qml）
target_link_libraries(stelarith-agent-qt PRIVATE Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickWidgets QtShadcn)

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
  target_sources(stelarith-agent-qt PRIVATE src/remote/vnc_server.cpp) # 依赖 rfb.h，仅开启时编入
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
# 2026-11-02：判据从 `theme/Md3.qml` 改为 `theme/{Theme.qml,qmldir}` 这一对。
# Md3.qml 已退场（全库对 `Md3.` 的引用实测 0 处；qmldir 只声明 `singleton Theme 1.0`）。
# 旧判据盯的是已删除文件 ⇒ 目录齐全也 FATAL_ERROR，门禁先于代码把构建卡死。
if(NOT EXISTS "${STE_SHARED_UI_DIR}/theme/Theme.qml" OR NOT EXISTS "${STE_SHARED_UI_DIR}/theme/qmldir")
  message(FATAL_ERROR
    "[agent] 共享 UI 基座目录缺失：${STE_SHARED_UI_DIR}\n"
    "  主题真源在 Stelarith-shared-qml/ui/theme/（pragma Singleton，qmldir 声明 Theme）。\n"
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

