# 星集控 · E5 空窗占位 + E4 主题三档同步 MobileMain · 加减归档

> 时间：2026-10-11 10:06 起 · 负责人：WorkBuddy
> 范围：仅 `Stelarith-viewer-qt` 两个 QML 文件（纯界面，无 C++ 改动）
> 编译验证：`build-qt-viewer.bat` EXIT=0（Qt 6.12 / Ninja 64 步）；`qmllint` 两个文件均 0 error

## 一、E5 概览顶部空窗占位（Main.qml）

**问题**：概览页顶部统计行 `在线 / 待办 / 离线` 中，`待办`=`backend.storage.pendingInstructions`、`离线`=`backend.storage.offlineDevices`。
`backend.storage` 是 `QJsonObject`（viewerbackend.h:154/221/760），首拉 `/api/storage` 前是空对象，这两个键为 `undefined`
→ QML `Text` 渲染成**空白**（storage 卡本身有 `at===undefined` 空态，但顶部这行没有，视觉上像"没数")。

**改法**：`Main.qml` 概览顶部 `Repeater` 的 model 对两个值加 `undefined` 兜底，显示「—」占位。
- 在线：`backend.onlineCount`（int 属性，恒有值，不动）
- 待办：`backend.storage.pendingInstructions !== undefined ? ... : "—"`
- 离线：`backend.storage.offlineDevices !== undefined ? ... : "—"`

**行为差异**：首拉前顶部显示 `在线 N / 待办 — / 离线 —`；数据到达后自动变成真实数字。与 storage 卡空态风格一致。

**改动量**：`qml/Main.qml` 概览顶部 model 段，+4/−2（含注释）。

## 二、E4 主题三档同步 MobileMain.qml

**背景**：E4 已在桌面 `Main.qml` 落地（默认浅色 + 三档 `themeMode` 0浅/1深/2跟随系统，QSettings 持久化，
`darkMode` 改 readonly 由 `backend.themeMode` 派生）。手机端 `MobileMain.qml` 仍是旧两档 `property bool darkMode: false`，
设置页「外观」仅浅色/深色两按钮。本次把手机端与桌面对齐。

**改动 1（根属性派生）** `MobileMain.qml:28-33`：
```qml
Theme { id: theme; dark: root.darkMode }
readonly property var th: theme
// E4 同步：darkMode 由 backend.themeMode 派生（0 浅色 / 1 深色 / 2 跟随系统取 backend.systemDark）
readonly property bool darkMode: (backend.themeMode === 1) ? true
                                      : (backend.themeMode === 2 ? backend.systemDark : false)
```
- 原 `property bool darkMode: false`（可写、固定浅色）改为 `readonly` 由 `backend.themeMode` 派生。
- `theme.dark: root.darkMode` 绑定不变 → Theme 令牌随 themeMode 自动切换；`th` 只读引用不变。
- 默认浅色（themeMode 默认 0）与桌面一致。

**改动 2（设置页三档）** `MobileMain.qml:356-373`「外观」卡：
- 原两按钮 `Row{Rectangle 浅色; Rectangle 深色}`（onClicked 写 `root.darkMode=...`，现 readonly 已不可写）删除。
- 改为三档 `Repeater`：`浅色(m:0) / 深色(m:1) / 跟随系统(m:2)`，选中块边框走反白 `root.th.inv`，
  `onClicked: backend.themeMode = modelData.m`（与桌面 Main.qml:1881-1906 同构）。
- 选中文字色 `root.th.win`（反白底上用亮色），未选 `root.th.fg3`。

**引用确认**：`backend.themeMode` / `backend.systemDark` 均为 E4 在 viewerbackend 新增的 Q_PROPERTY（h:163 附近 / cpp 构造读 QSettings）；
`MobileMain.qml` 作用域内已有 `backend`（RtcVideoSurface 接线 `backend.rtcFrameReady` 同文件可见），故可直接用。

**改动量**：`qml/MobileMain.qml` +20/−18（根属性 3 行替换 1 行；设置卡 17 行替换 17 行）。

## 三、#171 WebRTC 画面 — 接线核对 + 真机验证清单（本机未实证）

**取证避让铁律**：本机不跑被控端、不发指令、不重启服务、不动桌面 → **无法自实证真机画面**。
仅核对 E6 接线完整且正确，并给出可照做的真机验证步骤。

**接线核对（已落地，代码静态确认）**：
- 信号源：`viewerbackend.cpp` `emitLdcFrame` 在收到 ldc 帧时 `emit rtcFrameReady(QImage)`（h:504 信号声明）。
- 桌面 `Main.qml`：画面本体挂 `RtcVideoSurface { id: vidSurf; visible: backend.frameSource==='rtc' }`，
  `Connections { target: backend; onRtcFrameReady(img){ vidSurf.onFrame(img) } }`（E6 提交 `2dfb5c4`）。
- `RtcVideoSurface.onFrame` 把 `videoImg.source = img`（QImage 直作 Image.source，Qt6 支持），`framesReceived++`。
- `frameSource==='rtc'` 由 backend 在 WebRTC 链路接通时置位（链接指示已按此上色）。

**真机验证步骤（需你在真机执行）**：
1. 被控端（agent）正常上线、且走 WebRTC 推流（LdcStreamer 建 offer；此前 #171 诊断显示独立探针能出 offer，根因在运行时链路）。
2. 管理端桌面：左侧选该设备 → 右侧大画面 `frameSource` 变 `rtc` → RtcVideoSurface 显出实时画面（占位文字消失、`framesReceived` 增长）。
3. 手机端：监控页 `RtcVideoSurface` 同样接线（MobileMain.qml:494 同接法），选设备应出画面。
4. 若空白：看被控端日志有无 `rtc-offer` 到达、管理端日志 `onLocalDescription` / `rtcFrameReady` 是否触发；
   重点核对 #171 原诊断——云端日志此前零 `rtc-offer/answer/ice`，独立探针能出 offer ⇒ 生产运行时事件循环/信号投递路径待真机确认。

**未验证（明说）**：#171 真机显画未实证（取证避让）；E5 占位「—」的视觉观感、E4 三档切换真机观感、
跟随系统随 Windows 配色实时变化均未实证。

## 四、提交 / 推送

- viewer-qt：`qml/Main.qml` + `qml/MobileMain.qml` 路径限定提交 → push `outer` → push GitHub `WuMengAA/xingjikong-qt:viewer`（fast-forward）。
- 真源回写：待办清单 §E E5 补「占位已补」注；指挥台新增 10:0x 在办条；本归档入 docs。
- 未触碰 DeepSeek Harness 进行中的 `CMakeLists.txt`（Android）。
