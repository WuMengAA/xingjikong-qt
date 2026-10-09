# WebRTC 去 WebEngine 底层 · 接线方案（2026-10-09 更新）

> 目标：把两端 WebRTC 从 QWebEngine（Chromium）底层切到 libdatachannel。
> 现状：被控端 g_rtcView（离屏 QWebEngineView 跑 JS 采集页，29 处）、
> 管理端 m_rtcView（离屏收流，27 处）。替换组件 ldc_streamer/ldc_receiver
> 已造好、探针全绿（见 _probe/WebRTC引擎替换-技术验证总结-2026-10-08.md）。
> 状态：**待窗口执行**（main.cpp/viewerbackend.cpp 处于 WorkBuddy 未提交 M 状态）。
> 依据：grep blob 精确定位（不信提交信息）。

---

## 0. 前置（已就绪/进行中）

| 项 | 状态 |
|----|------|
| libdatachannel 静态库 | 🔶 重建中（datachannel-static.lib 误删，后台 job pwsh-309）|
| ldc_libs 备份 | ✅ juice-static/srtp2/usrsctp 已备份 `_probe\ldc_libs` |
| 组件源码 | ✅ ldc_streamer/ldc_receiver 已提交两端 src/ |
| 链接配方 | ✅ RTC_STATIC + datachannel-static + srtp2 + juice + usrsctp + libssl + libcrypto + ws2_32/bcrypt/crypt32/userenv/secur32（MSVC）|

## 1. 被控端接线（control-qt/main.cpp）

### 替换关系（grep blob 定位）
| 现实现（WebEngine）| 替换为（libdatachannel）|
|------|------|
| L183 `g_rtcView` + L197 `g_webEngineReady` | `LdcStreamer g_streamer`（offer 发起方）|
| L4380 `ensureWebEngine()`（按需拉 Chromium）| 删除——ldc 无 Chromium，start 即推 |
| L4412-4433 离屏 view + setHtml(kRtcPage) + loadFinished | `g_streamer.start(captureMs=200)` |
| L4235-4279 JS 信令注入（__startStream/__forceReoffer/__drain）| ldc_streamer 信号 signalOffer/signalCandidate 直接发云端 |
| L4322-4341 rtcTick 自愈/销毁 | ldc_streamer 内部自愈（negotiationNeeded 自动）|

### 信令兼容（关键：云端/管理端协议不变）
```
云端 rtc-offer/rtc-answer/rtc-ice 转发协议原样保留
被控端：signalOffer(sdp) → makeEnvelope("rtc-offer") → g_ws
        收 rtc-answer → g_streamer.onSignal("answer", sdp)
        收 rtc-ice   → g_streamer.onSignal("candidate", ...)
```

### CMake
```cmake
# 去掉 WebEngineWidgets（省 125MB 绿包）
# 加 libdatachannel 链接（见 §0 配方）
target_link_libraries(stelarith-agent-qt PRIVATE ... datachannel-static srtp2 juice-static usrsctp
    "C:/Program Files/PostgreSQL/17/lib/libssl.lib" "C:/Program Files/PostgreSQL/17/lib/libcrypto.lib"
    ws2_32 bcrypt crypt32 userenv secur32)
target_compile_definitions(stelarith-agent-qt PRIVATE RTC_STATIC)
# src/ldc_streamer.cpp 加入 add_executable（之前已注释，解除）
```

### 注意
- ⚠️ 跨线程：ldc 回调线程 emit 的 connect 必须带 context 转 Queued（探针教训）
- ⚠️ 附 DataChannel + singleShot(0) 发 offer（negotiationNeeded 就绪）
- ⚠️ 采集帧：ldc_streamer 用 QScreen::grabWindow→JPEG→RTP（无 Chromium canvas）

## 2. 管理端接线（viewer-qt/viewerbackend.cpp）

### 替换关系
| 现实现（WebEngine）| 替换为（libdatachannel）|
|------|------|
| L1070 `m_rtcView = new QWebEngineView()` | `LdcReceiver g_receiver`（answer 侧）|
| L1109-1128 离屏 page + loadFinished | `g_receiver.onSignal("offer", sdp)` |
| L1740+ 被控端 offer 灌进离屏 view | ldc_receiver 自动 answer |
| 收流解帧（原 page 内 JS 处理）| `g_receiver.frameReady(QImage)` → QML Image |

### 管理端 QML 显示
```qml
// RtcVideoSurface（已造）：frameSource==='rtc' 时显示
RtcVideoSurface {
    visible: backend.frameSource === "rtc"
    onFrameReady: /* viewerbackend 把 ldc_receiver.frameReady 转这里 */
}
```

### CMake
- 去掉 WebEngineWidgets + WebChannel（管理端 125MB→瘦）
- 加 ldc_receiver + libdatachannel 链接（同 §0 配方）

## 3. 验证步骤

1. 被控端/管理端各自编译 exit 0（无 WebEngine 引用）
2. 隔离云端 8095 + 假两端联调（信令 offer/answer/candidate 通 + track 建立）
3. 真机：教室机推流 → 管理端收帧显示
4. 绿包对比：改造前后 exe/安装包大小（预期 125MB → ~40MB）

## 4. 回退方案（万一接线崩）

- 保留 g_rtcView/m_rtcView 代码（用 #ifdef 或版本开关）——第一版不建议删干净，可先并行
- 或：ldc 失败时回滚 git（组件已提交，接线 commit 独立可回退）

---

*接线方案 v1 · 待窗口执行；ldc 库重建完成即可开始。*
