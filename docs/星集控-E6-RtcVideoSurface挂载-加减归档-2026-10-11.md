# 星集控 · E6 RtcVideoSurface 挂载（frameSource==='rtc'）· 加减归档

> 日期：2026-10-11 · 执行：WorkBuddy
> 对应待办：所有待办清单-2026-10-10.md § E / E6
> 关联需求：#171「WebRTC 画面在桌面管理端显出来」
> 真源指针：星集控-指挥台-唯一真源-2026-10-02.md（2026-10-11 09:41 条）

## ① 新增文件

无。E6 全部在既有 `Main.qml` 画面本体内完成，未新建文件（`RtcVideoSurface.qml` 是此前批次已落地的独立组件，本次仅挂载引用，不算新增）。

## ② 修改的既有文件

### `Stelarith-viewer-qt/qml/Main.qml`（仅画面本体 Rectangle，约 1086–1190 行）

| 位置 | 改动 | 为什么 |
| --- | --- | --- |
| `Image { id: screenImg }` | 新增 `visible: backend.frameSource !== "rtc"` | jpeg 轮询模式下才显示轮询画面；rtc 模式下隐藏，避免黑屏 Image 压在实时画面上 |
| 画面本体新增 `RtcVideoSurface { id: vidSurf; anchors.fill: parent; theme: th; visible: backend.frameSource === "rtc" }` | 挂 WebRTC 实时画面组件，仅 rtc 模式可见 | #171 关键：让 ldc_receiver 收来的帧在桌面端显出来（此前只有 MobileMain.qml 挂了，桌面端 Main.qml 没挂） |
| 同区新增 `Connections { target: backend; function onRtcFrameReady(img){ vidSurf.onFrame(img) } }` | 接 `viewerbackend::rtcFrameReady(QImage)` → `vidSurf.onFrame` | 与 MobileMain.qml:494 同一接法；`emitLdcFrame`(viewerbackend.cpp:1374) 发此信号即驱动显示 |
| 空态 `Column` 的 `visible` | `!root.frameReady` → `!root.frameReady && backend.frameSource !== "rtc"` | rtc 模式下的空态交给 RtcVideoSurface 自身的占位（"暂无实时画面"/"正在建立实时画面…"），避免双重占位 |
| "画面未变化 · 已暂停刷新" 角标 `visible` | `backend.screenStatic && root.frameReady` → `… && backend.frameSource !== "rtc"` | screenStatic 是 jpeg 轮询概念；rtc 模式不显示这行（组件自身有"实时/离线"角标） |
| 远控 `MouseArea { id: screenArea }` 的 `enabled` | `backend.frameCount > 0` → `backend.frameCount > 0 \|\| vidSurf.framesReceived > 0` | jpeg 模式靠 frameCount 放开输入；rtc 模式 frameCount 恒 0，改为靠 vidSurf 收到的 rtc 帧数放开，使实时画面也可"指哪打哪" |

**行为差异（旧 → 新）**
- 旧：桌面端选了走 WebRTC 推流的设备，`frameSource==='rtc'`，但画面本体只有轮询用的 `screenImg`（无帧→一直空态），实时画面**根本不显示**——这就是 #171 桌面端看不到 WebRTC 画面的根。
- 新：rtc 模式下 `RtcVideoSurface` 顶上显示，收到 `rtcFrameReady` 帧即刷新；轮询模式行为完全不变（screenImg/空态/角标照旧）。
- 门控全部走 `backend.frameSource`（Q_PROPERTY，NOTIFY frameSourceChanged，viewerbackend.h:163），切换设备/切换推流方式时实时增减，无残留。

## ③ 删除的文件

无。

## ④ 端点 / 接口迁移对照

| 项目 | 旧 | 新 |
| --- | --- | --- |
| 桌面端实时画面源 | 仅 `image://frames`（jpeg 轮询）| jpeg 轮询 + `RtcVideoSurface`（rtc）；按 `frameSource` 切换 |
| rtc 帧入口 | `viewerbackend::rtcFrameReady(QImage)` 信号（此前仅 MobileMain 接）| 同信号，新增桌面 Main.qml `Connections` 接 → `vidSurf.onFrame` |
| 远控输入启用条件 | `frameCount > 0`（仅 jpeg）| `frameCount>0 \|\| vidSurf.framesReceived>0`（jpeg + rtc）|

## ⑤ 验证证据

- 接法对照：`MobileMain.qml:494-495` 已上线同款 `backend.rtcFrameReady.connect(function(img){ vidSurf.onFrame(img) })`，桌面端改用等价 `Connections` 写法。
- 绑定确认：`viewerbackend.h:163` `Q_PROPERTY(QString frameSource … NOTIFY frameSourceChanged)`；`viewerbackend.h:504` `void rtcFrameReady(const QImage &frame);`；`viewerbackend.cpp:1372-1374` `emitLdcFrame` → `emit rtcFrameReady(img)`。三处均为既有、已编译通过。
- 编译验证：`build-qt-viewer.bat`（Qt 6.12 / Ninja / JOBS=2）正在后台跑——Qt 用 `qt_add_qml_module`，qmlcachegen 会在构建期编译 QML，语法错误会直接 fail。**结果待出**（本归档写于编译进行中）。

## ⑥ 没验证的部分（明说）

- **#171 真机显画未验证**：本次只是把桌面端"接收并显示 rtc 帧"的通路接通；被控端是否真推流、信令 offer/answer 是否真到 viewerbackend、首帧是否真显，需真机联调（本机取证避让铁律：未跑被控端、未发指令、未重启服务）。
- **E6 未截图 / 未离屏自测**：按取证避让，未启动被控端，也未对被控管理端 exe 做离屏截图。
- **build 结果待出**：QML 语法正确性以 build-qt-viewer 后台任务结论为准；若 fail 会修后重提。
- **浅色主题下 RtcVideoSurface 配色**：组件读 `th` 令牌（rCard/cream/stroke/fg4/ok/err）且有 fallback 默认色，未逐项在浅色模式截图核对（属 E4 窗口范畴，不在 E6）。
