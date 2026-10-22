# 加减归档 · 拆 main.cpp 第一刀（ffmpeg 纯函数层下沉）· 2026-10-22

> AGENTS.md §10 第 1 步的执行第一步。纪律：**搬家不改逻辑**。
> 提交：`Stelarith-control-qt` `7765df1`（+218 / -140）。

---

## 一、一句话

把 `main.cpp`（8142 行）里**零内部依赖的 ffmpeg 纯函数层**搬进新模块 `src/media/MediaTools`，
main.cpp 降到 **8022 行**。函数体逐字搬，未改任何逻辑。

---

## 二、为什么第一刀切这一层（而不是别的）

| 候选 | 耦合情况 | 判定 |
|---|---|---|
| **ffmpeg 纯函数层**（本刀） | **不依赖 main.cpp 任何东西** | ✅ 唯一能"纯搬家零重构"的一层 |
| 录制控制 `beginRecording/endRecording/stopMediaSession` | 依赖 `procWaitFinished`（跨 12 处调用）、`uploadRecordingAsync`、`IslandOverlay`、4 个全局 | 第二刀 |
| 相机四件套分支 | **嵌在 `executeAction` 这个 1695 行巨型函数体内**，切它=动主流程 | 第三刀 |
| OTA 域 | 全部独立函数、命名已成体系，边界也清晰 | 备选第二刀 |

选它的另一个理由：**搬完立刻有强验证**——ffmpeg 定位/设备解析/错误归类恰好是沙箱实弹测试
打的那四条指令（`camera_list` / `snapshot` / `record` / `media_session`）的共同底座。

---

## 三、新增文件

| 路径 | 行数 | 职责 |
|---|---|---|
| `Stelarith-control-qt/src/media/MediaTools.h` | 49 | 声明 + `CamErr` 结构 + 前置说明（为什么先搬这层、搬家纪律） |
| `Stelarith-control-qt/src/media/MediaTools.cpp` | 141 | 实现，**逐字搬自 main.cpp 781-905 行** |

函数体不是手打，而是**脚本按行区间原样抽取**写入新文件（`scripts/_split_media.py`，一次性脚本不入库），
只做三处机械变换：

1. 导出函数去掉 `static`（与头文件声明一致），放进 `namespace MediaTools`；
2. `cameraDevPath` 因 `camera_bind` 的错误提示要显示"写不进去的是哪个文件"而**导出**，其余不扩大接口面；
3. `CamErr` 结构定义进头文件，cpp 里删掉重复那句；**默认参数只在头文件写一次**。

注释里的实测结论（ffmpeg 9.0 设备名在首对引号里、虚拟摄像头判定、错误码是机器契约）**全部原样搬来**——那是踩坑换来的。

---

## 四、修改的既有文件

| 路径 | 改动 | 为什么 |
|---|---|---|
| `src/main.cpp` | 删 125 行定义；加 5 行"已下沉"注记 + 1 行 include；**14 处调用点**改 `MediaTools::` 前缀、**2 处** `CamErr` 类型加限定 | 定义搬走，调用点必须跟着改 |
| `CMakeLists.txt` | 源文件列表加 `src/media/MediaTools.{cpp,h}`（+4 行，带注释说明来源） | 新模块要进编译 |

**删除文件**：`src/main.cpp.bak-20261022-media1`（重构前备份）已删 —— 变更已入 git，git 可回滚，留备份反而误导后续会话。

---

## 五、接口迁移对照表

| 原（main.cpp 内 static） | 现 | 调用点 |
|---|---|---|
| `ffmpegBin()` | `MediaTools::ffmpegBin()` | 4 |
| `ffplayBin()` | `MediaTools::ffplayBin()` | 1 |
| `boundCamera()` | `MediaTools::boundCamera()` | 5 |
| `saveBoundCamera(d)` | `MediaTools::saveBoundCamera(d)` | 1 |
| `cameraDevPath()` | `MediaTools::cameraDevPath()` | 1（原先不导出，本轮为错误信息导出） |
| `parseDshowDevices(...)` | `MediaTools::parseDshowDevices(...)` | 1 |
| `classifyCamError(r)` | `MediaTools::classifyCamError(r)` | 2 |
| `CamErr`（类型） | `MediaTools::CamErr` | 2 |

合计 **14 处调用 + 2 处类型**，与脚本统计、手工 grep 清点三方一致。

---

## 六、验证证据（实跑）

| # | 验证 | 结果 |
|---|---|---|
| 1 | **构建（编译器当验证器）** | 首次构建报 **2 个错**：`MediaTools.cpp(65) C2572` 默认参数重复、`main.cpp(5349) C3861` 漏改 `cameraDevPath`。修完即过 —— **反证 14 处调用点无一遗漏**（漏一个就编译不过） |
| 2 | exe 产出 | `build/stelarith-agent-qt.exe` 23:38:39 / 6,405,120 B，晚于源码；`MediaTools.cpp.obj` 已生成 |
| 3 | **沙箱实弹**（`_test_console_fix.py`） | ffmpeg/ffplay **真实启动 4 次**，命令行完整记录，`ffmpeg.exe` 路径正常解析为 WinGet Links ⇒ **`ffmpegBin()` 行为零变化** |
| 4 | 指令结果 | `media` 产物 `test-tone.wav` 生成（播放路径成功）；`camera_list` 走完 ffmpeg 枚举 |
| 5 | **CMD 弹窗根治不回归** | 3 个 conhost **全部"无窗口"**，全局可见控制台窗 **0** |
| 6 | 规模门禁 | main.cpp 台账登记值 8142 → **8022**；门禁 PASS（白名单内且变瘦，不判红） |

---

## 七、明确未验证

1. **生产未部署** —— 本轮只在工作区构建 + 沙箱验证，**没推 OTA、没换 `C:\Program Files\Stelarith\` 的 exe**。要上生产需单独点单（走 `_deploy_agent.py` SOP）。
2. **无相机的失败路径未验到 ffmpeg 成功分支** —— 本机没有可用摄像头，实弹里 `camera_snapshot` 是"ffmpeg 真启动后失败"，成功写文件的路径未覆盖。
3. `camera_bind` 的错误分支（写绑定文件失败）**未单独构造失败场景验证**。
4. 门禁仍**未接 CI**，本轮是我手工跑的。

---

## 八、下一刀（待点单）

| 顺序 | 内容 | 备注 |
|---|---|---|
| 第二刀 | 录制控制 `beginRecording` / `endRecording` / `stopMediaSession` 下沉 | 需先处理 `procWaitFinished`（跨 12 处调用的通用进程工具）归属 |
| 第三刀 | 相机四件套分支从 `executeAction`（1695 行）里抽成独立函数 | 前两刀做完后再动主流程，风险最低 |
| 备选 | OTA 域（独立函数、命名成体系，约 750 行） | 边界同样清晰 |
