# Stelarith-control-qt · 教室机被控端（agent）

> 星集控 Qt 线的**教室机侧**：托盘常驻，抓屏推帧 / 远控 / 语音 / 灵动岛。
> 产物 `stelarith-agent-qt.exe`，配 NSIS 装机包走 OTA 发布。

## 一、构建

```bash
./scripts/build-agent.sh            # 增量；CMakeCache 丢了会自动补 configure
./scripts/build-agent.sh --clean    # 删 build 重配重编（换工具链才需要）
```

- 工具链：Qt **6.12.0**（`D:/QTqwq/6.12.0/msvc2022_64`）+ MSVC 14.51 + Windows SDK 10.0.26100。
  脚本自己喂 `INCLUDE` / `LIB` / `PATH`，不走 vcvarsall（本环境起不了 cmd.exe）。
- ⚠️ **防 C1060**：本机提交内存常年紧张，并行 `cl.exe` 会报「编译器堆空间不足」。
  建议串行：`CMAKE_BUILD_PARALLEL_LEVEL=1 ./scripts/build-agent.sh`。
- ⚠️ **改了 `resources/` 下的文件名，必须重跑 configure**（`cmake -S . -B build`）：
  CMakeLists 用 `file(GLOB)` 收集资源，GLOB 只在**配置阶段**求值；
  只跑 `cmake --build` 不会发现新文件或改名后的文件。

## 二、版本号

`CMakeLists.txt` 的 `set(AGENT_VERSION "0.6.34")` 是**唯一真源**。
配置阶段会读 `src/main.cpp` 的 `kAppVersion` 比对，不一致直接 `FATAL_ERROR` 拒绝编译。
**改版本只改 CMakeLists 那一行**，main.cpp 那份自动跟随。

## 三、关键文件

| 路径 | 职责 |
|---|---|
| `src/main.cpp` | 入口：托盘、云端 WS 连接、指令分发、`kAppVersion` |
| `src/island.{h,cpp}` | **灵动岛 C++ 宿主**（岛宽由 `geometryForContent` 决定，QML 的 `implicitWidth` 不直接生效） |
| `../Stelarith-shared-qml/island/` | **灵动岛 QML 视觉层** —— 与 viewer 共用的**唯一真源**（见第四节） |
| `src/island_bridge.{h,cpp}` | QML ↔ C++ 桥（QML 侧 `bridgeRef`） |
| `src/danmaku.{h,cpp}` | **弹幕全屏浮层**：双入口（通知可信源 80 字/120px/s 无过滤 vs 祝福学生源 30 字/100px/s + 敏感词 + 30s 冷却） |
| `src/whale_audio.{h,cpp}` + `resources/whale/` | 鲸鱼音效（winmm `PlaySound`，不依赖 Qt Multimedia） |
| `src/cloud_proto.h` | 出向指令信封 `makeEnvelope()`（`type='instruction'` + `payload.action`） |
| `scripts/build-agent.sh` | 构建入口 |
| `scripts/_island_qml_probe.py` | 灵动岛**离屏探针**（不启真机，注入状态后截图 + dump 断言） |

## 四、共享灵动岛（重要）

灵动岛 QML 视觉层（`IslandRoot.qml` 等 8 件）的**唯一真源在根仓库**：

`<repo-root>/Stelarith-shared-qml/island/` —— 被控端与管理端**共用同一份**。

本工程在 CMake 的 `POST_BUILD` 阶段把它 `copy_directory` 进 `build/qml/island/`，
`island.cpp` 的加载路径（`qml/island/IslandRoot.qml`）**一个字符都不用改**。
本地 `qml/island/` 已退役（`git rm`），不要再往那儿放文件。

- ⚠️ **必须整体 clone 根仓库 `Stelarith-xingjikong-qt`**。单独 clone 本子工程会缺件，
  配置阶段会 `FATAL_ERROR` 拦住 —— 否则会退化成运行期"岛 / 鲸鱼娘白屏"的哑巴故障。
- ⚠️ 改岛的外观请改**共享目录**，别改 `build/` 下的副本（下次构建会被覆盖）。
- 岛素材 `resources/whale/` 仍在本工程内（`qrc:/whale` 随 exe 走），不属共享目录。

## 五、边界（别踩）

- **不重启在线被控端**；真机验证起独立实例（假 uid），验完只杀它自己。
- `C:\Program Files\Stelarith\` 那份在线安装**不碰**——安装器 `ssInstall` 会 `Stop-Process` 掉 agent。
- 源码含中文注释，MSVC 必须 `/utf-8`（CMakeLists 已设）。
- 进程终止一律**黑名单**语义（只杀点名的）。

## 六、相关文档

- 历史交接说明：`README-工作树说明-2026-10-06.md`
- 路径变更：`AI-必读-路径变更告知书.md`
- 全局命名 / 版本 / 提交约定：仓库根 `docs/命名与版本约定.md`
