cmake_minimum_required(VERSION 3.21)
project(stelarith-agent-qt LANGUAGES CXX)

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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebSockets WebEngineWidgets Network)

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
)
target_link_libraries(stelarith-agent-qt PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::WebSockets Qt6::WebEngineWidgets Qt6::Network)

# GUI 子系统：常驻托盘程序不该挂着控制台黑窗（日志已落盘到 agent-qt.log，不靠 stdout）
set_target_properties(stelarith-agent-qt PROPERTIES WIN32_EXECUTABLE ON)