# 加减归档 · 拆 main.cpp 第二刀（录制/播放控制下沉）· 2026-10-22

> 提交：`Stelarith-control-qt` `bc857d1`（+274 / -163）。main.cpp **8022 → 7935 行**。
> 纪律同第一刀：**搬家不改逻辑**。

---

## 一、搬了什么

| 新模块 | 行数 | 内容 |
|---|---|---|
| `src/core/ProcessUtil.{h,cpp}` | 17 / 30 | `procWaitFinished` → `ProcessUtil::waitFinished` |
| `src/media/MediaSession.{h,cpp}` | 33 / 112 | `beginRecording` / `endRecording` / `stopMediaSession`（+ `isStopping`） |

**为什么 `ProcessUtil` 单独成模块**：它不属于任何业务域——录像、OTA、远控、终端都在用（12 处调用）。
塞进 media 会让"录依赖进程工具"这种反向依赖固化；放 `core/` 是给后面几刀（OTA / 远控）预先备好的地基。

---

## 二、有意保留的过渡形态：只搬行为，暂不搬状态

`g_recProc` / `g_playProc` / `g_mediaTick` 这些全局，除了这三个函数在用，
**`executeAction` 内部的相机分支也在直接访问**（5502-5530 / 5677-5753），而那片是第三刀的地盘。

若这一刀连状态一起搬 = 提前进 `executeAction` 改 20+ 处 = **两刀并成一刀**，风险不可控。
所以模块内用 `extern` 引用这些状态，并在 `MediaSession.cpp` 顶部写明：
> 第三刀把相机分支抽成函数时，状态随之收进本模块，届时删掉 extern 段。

**这不是偷懒，是顺序**：先搬行为（可独立验证），再搬状态（依赖调用面收敛）。

---

## 三、修改的既有文件

| 路径 | 改动 |
|---|---|
| `src/main.cpp` | 删 3 个函数块（95 行）+ 1 处前向声明；去 `static` 3 处；调用点 16 处改前缀；**8 个符号移出匿名 namespace** |
| `CMakeLists.txt` | 源文件列表加 4 个新文件（+6 行，各带来源注释） |

**删除**：`main.cpp.bak-20261022-media2` / `-hoist` 两份过程备份已删（变更已入 git）。

---

## 四、接口迁移对照表

| 原 | 现 | 处数 |
|---|---|---|
| `procWaitFinished(p, ms)` | `ProcessUtil::waitFinished(p, ms)` | 12（其中 3 处在搬走的块内，实改 9） |
| `beginRecording(d, s)` | `MediaSession::beginRecording(d, s)` | 2 |
| `endRecording(...)` | `MediaSession::endRecording(...)` | 2 |
| `stopMediaSession()` | `MediaSession::stopMediaSession()` | 2（另删 1 处前向声明） |
| `static QTimer *g_mediaTick` 等 3 条 | 去掉 `static`（给模块 extern 引用） | 3 |
| 7 个状态变量 + `uploadRecordingAsync` | 移出匿名 namespace（内部链接 → 外部链接） | 8 |

---

## 五、这一刀挖出的真坑：匿名 namespace 的内部链接

编译全过后，链接期报 **8 个 LNK2019**：`g_recProc` / `g_playProc` / `g_mediaTick` /
`g_mediaAutoStop` / `g_mediaStopping` / `g_recOutPath` / `g_shotDir` / `uploadRecordingAsync`。

**根因**：main.cpp 238 行的 `namespace {` 一直延伸到 **6670 行**，上面那些符号**全在里面**——
匿名域符号是内部链接，**跨不了 TU**，所以 `MediaSession.cpp` 的 `extern` 取不到。
（第一刀搬的是零依赖纯函数，没碰状态，所以没暴露。）

**修法**（`scripts/_hoist_globals.py`）：
- 7 个状态变量 → 移到 `namespace {` **之前**（定义必须在所有使用点之前）；
- `uploadRecordingAsync` → 移到 `} // namespace` **之后**（它引用 `g_uid`，而 `g_uid` 在匿名域内，须排在其后）。

**这不是搬家引入的新问题**——项目里 `makeEnvelope` 早就踩过同一个坑，
文件 449-451 行注释写着："原先它就写在下面那段匿名 namespace 里，匿名域符号跨不了 TU ——
terminal_console.cpp 一调用就 LNK2019"。**同一个坑，第二次踩，这次记下来。**

---

## 六、验证证据

| # | 阶段 | 结果 |
|---|---|---|
| 1 | 编译（3 轮） | ① `MediaSession.h` 漏 `#include <QString>`（C4430/C2146）→ 补；② 跨目录 include `ProcessUtil.h`/`island.h` 找不到（C1083）→ 改相对路径；③ **第一刀已改过前缀，本刀脚本又替换一次，生成 `MediaTools::MediaTools::ffmpegBin()` 双重前缀**（C3083）→ 修正。**编译器当验证器：漏一处就过不去** |
| 2 | 链接 | 8 个 LNK2019（匿名 namespace）→ 符号移出后**链接通过**，exe 产出 |
| 3 | **沙箱实弹** | ffmpeg/ffplay **真实启动 4 次**；`camera_record_start/stop`、`media_session_start/stop` 四条路径（本刀搬走的代码）**全部真跑通**；`media` 产物 `test-tone.wav` 生成 |
| 4 | CMD 根治不回归 | 3 个 conhost **全部"无窗口"**，全局可见控制台窗 **0** |
| 5 | 规模门禁 | 台账 `main.cpp` 8022 → **7935**，PASS |

---

## 七、明确未验证

1. **生产未部署** —— 未推 OTA、`C:\Program Files\Stelarith\` 的 exe 仍是旧的；
2. 本机无摄像头，**录像成功写出 mp4 文件**的路径没覆盖到（实弹里是"ffmpeg 真启动后无设备"）；
3. `endRecording` 里"ffmpeg 因 `-t` 到点自己结束"的分支（自习模式常走）**未构造场景验证**；
4. 门禁仍未接 CI。

---

## 八、第三刀（待点单）

| 选项 | 内容 | 备注 |
|---|---|---|
| **推荐** | 相机四件套分支从 `executeAction`（1695 行）抽出成独立函数 | 抽出后 `g_recProc` 等的最后一批直接访问消失 ⇒ **状态可整体收进 MediaSession，删掉 extern 段** |
| 备选 | OTA 域（约 750 行独立函数） | 边界同样清晰，且 `ProcessUtil` 已就位 |
