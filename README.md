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
