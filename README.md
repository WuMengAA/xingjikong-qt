# Stelarith-viewer-qt · 老师机管理端（viewer）

> 星集控 Qt 线的**老师机侧**：Qt6 + QML 桌面端。设备表、实时画面、远控指令、
> 文件推送、语音、多班墙、灵动岛观感层。
> 产物 `viewer-qt.exe`，配 NSIS 装机包走 OTA 发布。

## 一、构建

```bash
python scripts/_build_viewer.py           # 增量编（脚本自带 MSVC 环境，-j4）
python scripts/_build_viewer.py --clean   # 先清目标再编（排障用）
```

- 直接跑 `ninja -C build viewer-qt` 会因缺 MSVC 的 `INCLUDE`/`LIB` 而以
  `qglobal.h: fatal C1083 无法打开 <type_traits>` 全线失败 —— 看着像 Qt 坏了，其实是环境没喂。
- ⚠️ **脚本只跑 ninja，不做 configure**。改了 `resources/` 下的文件名、
  或改了 CMakeLists，**先手动 `cmake -S . -B build` 重配**（`file(GLOB)` 只在配置阶段求值）。
- `viewerbackend.cpp` 体量大（2700+ 行），CMakeLists 已加 `/Zm1200` 抬编译堆上限。

## 二、版本号

`CMakeLists.txt` 的 `set(VIEWER_VERSION "0.6.34")` 是**唯一真源**。配置阶段做两道闸：

1. 读 `src/main.cpp` 的 `kViewerVersion` 比对，漂移直接 `FATAL_ERROR`；
2. **命名守门**：只认 `X.Y.Z` / `X.Y.Z-alpha.N` / `X.Y.Z-rc.N`（阶段号从 1 起，alpha < rc），
   四段式、裸后缀一律挡在构建外。

NSIS 安装器的版本常量由这里 `configure_file` 生成 `version.nsh`，不再手抄一份。

## 三、关键文件

| 路径 | 职责 |
|---|---|
| `src/main.cpp` | 入口；`kViewerVersion` 回落 |
| `src/viewerbackend.{h,cpp}` | 核心后端：连接 / 鉴权 / 设备表 / 帧 / 指令 / 文件推送 |
| `src/ldc_receiver.{h,cpp}`、`src/ldc_rtc_bridge.{h,cpp}` | WebRTC 收流（libdatachannel） |
| `src/audio_capture.{h,cpp}` / `audio_capture_android.cpp` | 麦克风采集（桌面 / Android 分支） |
| `src/audio_playback.{h,cpp}` | 播放 |
| `qml/Main.qml` + `qml/components/` | 界面层；`qml/components/WhaleMascot.qml`、`DynamicIsland.qml` 为灵动岛观感件 |
| `../Stelarith-shared-qml/island/` | **灵动岛 QML 视觉层唯一真源** —— 与被控端共用同一份（见第四节） |
| `src/interactbridge.{h,cpp}` | **互动审批桥**：审批数据面（云端 `/api/interact/*`）+ 共享岛桥面（对齐被控端 `IslandBridge`） |
| `qml/InteractDialog.qml` | **互动审批面板**（独立弹窗；右栏内嵌共享灵动岛预览） |
| `qml/components/md3/` | **Material 3 组件库**（全项目唯一视觉模板） |
| `installer.nsi` + `src/version.nsh.in` | NSIS 装机包 |

## 四、共享灵动岛（重要）

灵动岛 QML 视觉层（`IslandRoot.qml` 等 8 件）的**唯一真源在根仓库**：

`<repo-root>/Stelarith-shared-qml/island/` —— 管理端与被控端**共用同一份**
（用户点单原话：「管理端审批面板 UI，灵动岛使用被控端一模一样的」）。

本工程在 CMake 的 `POST_BUILD` 阶段把它 `copy_directory` 进 `build/qml/Stelarith/island/`，
QML 侧 `import "island"` 直接可用，**一个字符都不用改**。审批面板右栏那颗岛就是它。

- ⚠️ **必须整体 clone 根仓库 `Stelarith-xingjikong-qt`**。单独 clone 本子工程会缺件，
  配置阶段会 `FATAL_ERROR` 拦住 —— 否则会退化成运行期"岛 / 鲸鱼娘白屏"的哑巴故障。
- ⚠️ 改岛的外观请改**共享目录**，别改 `build/` 下的副本（下次构建会被覆盖）。
- 岛挂在上下文属性 `bridge`（`src/main.cpp` 注入 `InteractBridge`）；QML 里取它要用
  非冲突名（`readonly property var br: ...`），别把属性起名叫 `bridge`（会自遮蔽）。

## 五、边界（别踩）

- **不重启在线管理端**；改动先在独立实例上验。
- 源码含中文注释，MSVC 必须 `/utf-8`（CMakeLists 已设）。
- `deploy/` 是 windeployqt 产物，**不要手工改**（会被下次部署覆盖）。

## 六、相关文档

- 历史交接说明：`README-工作树说明-2026-10-06.md`
- 路径变更：`AI-必读-路径变更告知书.md`
- 全局命名 / 版本 / 提交约定：仓库根 `docs/命名与版本约定.md`

## 七、MD3 重构 · 监控中心（加减归档 2026-10-19/20）

用户点单：按 MD3 规范重构监控中心，彻底消除"拼凑感"。六条规格：净化边框（浮层除外，
用底色分区）、统一圆角 `rM`(12)、四 Tab 做标准下划线（**拒绝黑色实心药丸**）、
黑屏占位只留「连接中...」/「暂无画面」、顶栏「测试/抓拍」降级无边框 IconButton、
网格 `spL`(24)/`spM`(16)。⚠️ 工程内真名是 `qml/components/CameraCenter.qml`
（UiPlugins 面板 id="camera"，Popup 外壳 `qml/panels/CameraCenterDialog.qml`）。

### 7.1 加减归档

**新增文件：无。删除文件：无。** 共改 5 个既有文件：

| 文件 | 行数变化 | 改了什么 |
|---|---|---|
| `qml/components/CameraCenter.qml` | 1087 → 1421（diff 1289 行） | 全量重排：顶栏（标题+3 IconButton）/ 下划线 Tab / 四页容器 / 状态行 / 底栏；左「设备+摄像头列表+绑定」右「分段钮+画面台」；占位文案精简，长说明移诊断页「画面说明」块 |
| `qml/components/StTabBar.qml` | 重写（65 行） | 实心药丸 → MD3 下划线 Tab（选中=主色+Medium+3px 指示器；无底色无边框）。**API 不变**（theme/model/currentIndex），调用方零改动 |
| `qml/panels/CameraCenterDialog.qml` | diff 25 行 | 560×440 → **680×520**；背景改 `cSurface`+`rXL`(28) 圆角、**去边框**；内边距 12 → 24（spL） |
| `../Stelarith-shared-qml/ui/Md3.qml` | +6 行 | 新增间距刻度 `spXS=4/spS=8/spM=16/spL=24`（原令牌集缺间距，用户规格 `Theme.spacing.*` 落地） |
| `../Stelarith-shared-qml/ui/md3/Md3Btn.qml` | +13 行 | 加 `radius`（外部可覆写圆角，默认仍药丸）、`tip`（ToolTip）；补 `import QtQuick.Controls` |

删死属性 `recordingGroup`（从未被引用）。新增：`screenHasFrame`（占位判据）、
`deviceName()`、底栏四动作 `screenShot()`（整屏截图 screenshot）/ `pushFile()`（开 file 面板）/
`stopAll()`（停录+停推流收口）/ `closePanel()`。全部逻辑函数（deviceUids/camSync/
onCams/回执三件/自习/诊断）原样保留。

### 7.2 ⚠️ 踩坑记录：`ctx` 属性自遮蔽（探针抓到的真 bug，生产同中招）

 CameraCenterDialog 里对子组件的赋值 `theme: ctx.window.th` / `backend: ctx.window.backendRef` /
`ctx: ctx`——右值裸 `ctx` 在**子对象作用域优先解析到子组件自己的属性**。MD3 重构批给
CameraCenter 新增了 `property var ctx` 后，三行全部变成自引用：theme/backend/ctx 恒
undefined/null，**面板能开但全死**（无令牌、无 backend、推文件/关闭无反应）。旧版组件
没有 ctx 属性所以无事。修法：① CameraCenter 的属性改名 **`panelCtx`**；② 对话框三处
改显式 `camCenterDlg.ctx` 限定（防后来人再加 shadow 属性）。全库仅此一处 `ctx: ctx`
模式，其它面板无此险。生产 qml/ 是 10-17 旧快照（重构前代码）**未受污染**；安卓树
22:16 副本同为旧码，0.6.45 重打 APK 会吃到修好的代码。

### 7.3 接口迁移对照

| 面 | 旧 | 新 | 兼容性 |
|---|---|---|---|
| 面板转发接口（宿主→对话框） | refreshCams/onCams/onShotResult/onTestResult/onBindResult | 同左 | 不变 |
| 顶栏动作 | 「测试」「抓拍」文字钮 | 同功能 IconButton（circle-check/video 图标 + ToolTip） | 视觉降级，动作同 |
| 底栏（新增） | 无 | 截图(sendAction "screenshot") / 推文件(ctx.open "file") / 停止(stopAll) / 返回(closePanels) | 纯新增，走既有通道 |
| StTabBar | 药丸 | 下划线 | API 不变；连带 `qml/DiagFeedbackDialog.qml`（日志/诊断两档）同步换肤，且修复其潜在缺陷（旧版 delegate 宽为负时 Tab 不可见） |
| Md3Btn | 药丸固定 | 加 radius/tip | 加法，默认行为不变 |

### 7.4 验证证据

- **qmllint**：5 文件全 `EXIT=0` 零硬错（剩余 132 条均为项目通有 Unqualified access 风格警告）。
- **运行时探针**（`scripts/_ui_panels_probe_md3.qml`，offscreen，判定看退出码 10=过）：
  **bad=0，11 项全过**——14/14 面板可开可关；相机深度操作：4 Tab 轮切、onCams 回执
  （含虚拟摄像头标注）、自检/抓拍/绑定回执（含失败+重试态）、layoutMode 三连切、
  stopAll/screenShot 不崩、未知 id 拒绝。期间 fakeBackend 收到
  camera_stream_start/camera_list/camera_stream_stop 全链路 action。
- **新旧基线对比**：同一探针跑 `build/`（旧码）得 bad=0——parity 确认，7.2 的 bug 即由
  此差值法定位（首轮新版 bad=2→修复后 bad=0）。
- **二分探针**：`scripts/_probe_bisect.qml` 逐步复刻 stopAll 定位到
  `cc.backend.sendAction` 抛 TypeError；`_probe_tree_dump.qml` 转储对象树证实
  `cc2.ctx=null` 而 `cam.ctx.window` 正常；`_probe_createobj.qml` 排除
  createObject 初始属性时序嫌疑，收窄到作用域遮蔽。
- ⚠️ 教训再+1：**最小 import 测试与 qmllint 都给假安全感**（`typeof` 不编译文件），
  唯一可靠出口是运行时实例化探针。

### 7.5 未验证部分（如实声明）

- **无真机目检**：offscreen 探针看不到像素；SvgIcon/`image://frames/*` provider 在探针
  环境未注册（viewer 运行时才有），图标与画面需真机确认。
- **未编译 exe、未同步生产**（`C:\Program Files\StelarithViewer\qml\Stelarith\` 仍是
  10-17 旧快照，功能不受影响）、**未提交 git**（同一工作树有并行线，提交前先 `git log`）。
- 自习/诊断页仅冒烟（开页+喂回执），未做深度交互测试。
- 摄像头实时流（`image://frames/cam`）的 RTC 通道已于 2026-10-18 随 WebRTC 移除，
  该格当前可能无数据源——**既有问题，非本批引入**，待真机验证时一并观察。

### 7.6 对用户线框的两处偏差（已声明）

1. 「开录/停录」不在底栏（线框底栏只有 截图/推文件/停止/返回）：移入「录制回放」页
   标题行——功能不丢，且底栏「停止」已能收口推流。
2. 弹窗外壳圆角用 `rXL`(28) 而非 `rM`(12)：用户规格原文「除非是悬浮层」的豁免项。
