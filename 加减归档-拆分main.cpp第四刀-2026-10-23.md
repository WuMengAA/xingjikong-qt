# 加减归档 · 拆分 main.cpp 第四刀（媒体会话分支 + 收状态）

**日期**：2026-10-23 00:4x–01:0x
**工程**：`Stelarith-control-qt`（被控端 agent）
**性质**：AGENTS.md §10 第 4 刀 · **抽分支 + 收状态一起做**
**提交**：control-qt 待提交 ｜ meta 待提交

---

## 一、这一刀与前三刀的区别

前三刀都只搬"行为"，状态留在 main.cpp 用 extern 引用（第二、三刀是**有意的过渡形态**）。
第四刀是第一次**把状态一起收走**，因为条件成熟了：

> 相机分支第三刀抽走、媒体会话分支第四刀抽走之后，那 6 个状态在 main.cpp 里
> **只剩定义行**（外加 `g_recOutPath` 一处在状态回执里的读）——调用面已收敛。

所以这一刀包含四类改动，按可独立验证的顺序分步执行，**每步构建一次**：

| 步 | 内容 | 构建 |
|---|---|---|
| 地基 1 | `AgentSettings` 下沉到 `src/core/AgentSettings.h` | ✓ exit 0 |
| 地基 2 | 播放时段判定（`isPlayAllowedNow`/`remainingMsInWindow`）搬入 MediaSession | ✓ exit 0 |
| 主刀 | `media_session_start`/`media_session_stop` 分支体抽出 | ✓（2 个错，修后过） |
| 收状态 | 6 个全局收归 MediaSession，删 extern | ✓ exit 0 |

---

## 二、为什么需要地基（两个硬障碍）

抽分支前先查依赖的链接性，摸出四个**内部链接**符号——不解决，分支搬过去根本编译不了：

| 符号 | 位置 | 链接性 | 处理 |
|---|---|---|---|
| `AgentSettings`（25 个函数） | 409–437，**嵌套在匿名 namespace 内**且全带 `static` | ✗ 内部 | 整体下沉到 `core/AgentSettings.h`（`static`→`inline`） |
| `isPlayAllowedNow` | 4350，`static`，匿名域内 | ✗ 内部 | 搬入 MediaSession（本就属播放时段域） |
| `remainingMsInWindow` | 4368，`static`，匿名域内 | ✗ 内部 | 同上 |
| `g_mediaDir` | 338，匿名域内 | ✗ 内部 | 移出到全局区（与 `g_shotDir` 同级） |

**`AgentSettings` 是这轮最值钱的发现**：它 409 行开始、看着像个横跨 6000 行的巨物
（`} // namespace` 在 6405），实测**437 行就闭合了**——6405 那个 `}` 是匿名 namespace 的。
真实形态是 29 行、25 个纯 `QSettings` getter/setter。所以能整体干净下沉，
`static` → `inline` 是唯一变换（头文件定义的标准要求），**键名一个字符都没动**
（键名是 `test-plugin-island-gate.mjs` 的门禁契约）。

---

## 三、新增文件

### `src/core/AgentSettings.h`（41 行）

25 个功能开关，全部 `inline`，含两段重要注释原样搬走：

- P0-0 插件推岛治理（**默认开**的理由：默认关会让已装插件一夜之间静默失效，那是行为回归不是治理）
- P3 播放时段管理（默认关闭 = 随时可播）

---

## 四、修改的既有文件

### `src/media/MediaSession.h`

- 加 `#include <QJsonObject>` + `#include "../core/ExecOut.h"`（本模块开始直接产出回执）
- 加 `class QProcess;` **前向声明**（§10.4：只用到指针就绝不 include 完整定义）
- 新声明 6 个：
  - `bool isPlayAllowedNow()` / `qint64 remainingMsInWindow()`（时段判定）
  - `ExecOut startMediaSession(params)` / `ExecOut stopMediaSessionAction()`
  - `QProcess *recProc()` / `void setRecProc(QProcess*)` / `QString recOutPath()` / `setRecOutPath()`（状态访问器）
- 头部"过渡形态"注释更新为"第四刀已收归"

### `src/media/MediaSession.cpp`

- **extern 段 → 文件内 static 定义**，6 个状态**变量名保持不变**：

```cpp
static QProcess *g_recProc = nullptr;
static QString   g_recOutPath;
static QProcess *g_playProc = nullptr;
static QTimer   *g_mediaTick = nullptr;
static QTimer   *g_mediaAutoStop = nullptr;
static bool      g_mediaStopping = false;
```

  ⚠️ **这是本刀的关键设计**：变量名刻意不改，于是第二、三刀逐字搬来的所有函数体
  **一个字都不用动** —— 收状态这件事不该连带改动业务逻辑。

- 加 `startMediaSession` / `stopMediaSessionAction`（118 行分支体）
- 加 `isPlayAllowedNow` / `remainingMsInWindow`（32 行时段判定）
- 加 4 个状态访问器
- 补 include：`core/AgentSettings.h`、`<QTime>`、`<QStringList>`

### `src/media/CameraActions.cpp`

- 删 2 行 extern（`g_recProc`/`g_recOutPath`）
- 8 处调用改走访问器，最要紧的是这处精细逻辑：

```cpp
// 原
delete g_recProc; g_recProc = nullptr;
g_recOutPath.clear();
// 现
MediaSession::setRecProc(nullptr);      // 等价于 delete 旧对象 + 置空
MediaSession::setRecOutPath(QString());
```

  `setRecProc` 的实现保证语义等价：`if (g_recProc) delete g_recProc; g_recProc = p;`

### `src/main.cpp`（7708 → 7525，**−183**）

- 地基 1：删 `AgentSettings` 29 行 + 加注记 5 行 + 加 include（−23）
- 地基 2：删两个时段函数 38 行 + 删 2 行前置声明；5 处调用改 `MediaSession::` 前缀（−38）
- 主刀：两处分支体 → 两行调用（−120）
- 收状态：删 6 个全局定义；1 处 `g_recOutPath` 读改 `MediaSession::recOutPath()`（−2）

### `CMakeLists.txt`

登记 `src/core/AgentSettings.h`。

---

## 五、extern 清理结果（第三刀报告里承诺的那件事）

| 位置 | 第四刀前 | 第四刀后 | 说明 |
|---|---|---|---|
| `MediaSession.cpp` | 8 行跨 TU 引用段 | **3 行** | 删 6 个状态 extern；剩 `g_shotDir`/`g_mediaDir`（存储域）+ `uploadRecordingAsync`（上传域） |
| `CameraActions.cpp` | 3 行 extern | **1 行** | 删 2 行；剩 `g_shotDir` |
| **合计** | **11 行** | **4 行** | 剩下 4 行全属**存储域 / 上传域**，不是媒体状态 |

**为什么还剩 4 行而不是 0**（诚实表述）：

- `g_shotDir`（main 9 处 + MediaSession 1 + CameraActions 3）：`screenshot` 动作、
  抓屏、相机自检三处共用 ⇒ 属**存储/路径域**，收进 media 会固化反向依赖。
- `g_mediaDir`（main 4 处 + MediaSession 1）：媒体库目录，同样属存储域。
- `uploadRecordingAsync`：定义在 main.cpp 的**云端上传域**函数，不是状态变量。

即：**媒体会话域的 extern 已清零**，剩下的是存储域那一刀的课题。

---

## 六、验证证据

| 项 | 方法 | 结果 |
|---|---|---|
| 锚点复核 | `_split_media4_ground.py` / `_split_media4_main.py` 彩排 | 定义行、前置声明、5 处调用点、两处分支 **全部命中** |
| 分步构建 | `_build_show_errors.py` ×4 | 地基 1/2 一次过；主刀 2 个错（已修）；收状态一次过 |
| **逐字保真** | `_verify_media4_fidelity.py` | **150 行全部逐字一致** ✓ |
| 沙箱实弹 | `_test_console_fix.py` | **PASS** |
| 规模门禁 | `test-file-size.mjs` | PASS，扫描 269 文件，台账 7708 → **7525** |

### 保真明细

```
✓ startMediaSession       99 行逐字一致
✓ stopMediaSessionAction  19 行逐字一致
✓ isPlayAllowedNow        14 行逐字一致
✓ remainingMsInWindow     18 行逐字一致
合计 150 行 → 全部逐字一致
```

### 实弹明细

ffmpeg/ffplay **真实启动 4 次**（`camera_list`/`camera_snapshot`/`camera_record_start`/
`media_session_start`），其中 **`media_session_start` 正是本刀搬走的路径**，ffplay 真起播、
`test-tone.wav` 产物生成 ⇒ 播放链路行为零变化。3 个 conhost 全部"无窗口"、
全局可见控制台窗 **0 个** ⇒ CMD 弹窗根治未回归。

`T5 play_schedule_set`（放开时段）之后 `T6a media_session_start` 成功 ⇒
**搬走的 `isPlayAllowedNow()` 时段判定工作正常**（不是"碰巧没开限制所以没走到")。

---

## 七、这一轮编译器抓到的两个我的错误（值得记）

1. **`MediaSession.h` 漏 `#include "../core/ExecOut.h"`** —— 第三刀 `CameraActions.h` 加了、
   这个没加，新声明的 `ExecOut startMediaSession(...)` 直接 C4430。
2. **`g_mediaDir` extern 声明漏插** —— 脚本里 `if not any("g_mediaDir" in ln for ln in new_cpp)`
   这个防重判断，因为**刚插入的函数体里已经含有 `g_mediaDir` 字样**而误判为"已存在"，
   跳过了插入。编译器报 C2065 才发现。

两处都是**编译器当验证器**抓出来的，不是我读代码发现的 —— 这正是"每步构建一次"的意义。

另外校验脚本 _verify_media4_fidelity.py 自身也有一个切法 bug（函数与分支的体起点差一行、
`endline` 填了闭合后一行），跑出 15 vs 14 的假差异，修正后 150 行全一致。
**校验脚本自己也得能被证伪。**

---

## 八、明确未验证

1. **未上生产** —— `C:\Program Files\Stelarith\` 跑的还是第二刀那份。四刀都是结构性改动，
   攒一起上一次生产是用户选定的"最后一批"。
2. **未发 OTA** —— 教室机仍是 rc.3。
3. **本机无摄像头** —— ffmpeg"成功写出文件"路径仍未覆盖（实弹里都是"真启动后失败"）。
4. **播放成功路径只验到"ffplay 真起来"** —— 本机无可用音频输出设备，ffplay 会秒退；
   "完整播完 + 岛底计时器推进 + 超窗口自动截断"这条链路**没有端到端验证**。
5. `stopMediaSessionAction` 的 `wasRunning=false` 分支（播放器先自行退出）未单独构造场景。
6. 门禁仍未接 CI，是手工跑的。

---

## 八点五、生产部署验证（2026-10-23 01:04 · 四刀一并上生产）

四刀都是结构性改动，攒一起上一次生产（SOP：停任务含 guard 防巡检抢件 → 杀进程 →
改名/拷贝备份 → sha256 逐件校验 → 起任务 → 观察）。

| 项 | 结果 |
|---|---|
| 备份 | `stelarith-agent-qt.exe.bak-20261023-010449` + `qml-bak-20261023-010449/`（零删除） |
| exe 字节级 | 生产 `5b8aef5b90c5fae5…` == 构建件 ✔；版本串 rc.3×1 / rc.2×0 |
| qml | 5 件逐件 sha256 一致 ✔ |
| 新进程 | **pid 25008**，01:04:49 起，生产路径 |
| 注册 | `已确认注册 v1（uid=N7-20091211，server=stelarith-cloud-ws/0.4.0）` |
| 心跳 / 抓屏 | 心跳 #5、帧 #25（新进程重新计数），持续推进 |
| OTA 自洽 | `升级检查：已是最新（云端 v0.6.47-rc.3 / 本机 v0.6.47-rc.3）` |
| 控制台窗 | 部署后 20 秒观察 **0 个** ✔ |

### 部署时做的一个切割（重要）

`qml/shell/BroadcastScheduleSheet.qml` 在**构建产物**里被回退到 git HEAD 版（716 行），
**生产刻意不含**今天新做的 VoiceHub「排期按日隔离」功能（780 行版）。

理由：该项目实证——**qmllint 对"属性不存在/整页崩"是 0 Error**，语法层过了不等于能跑。
这个改动只过了 qmllint，**没跑运行时探针**。第三刀部署时就用同样的理由把它切在
外面过（当时记的是"生产跑 16:44 快照，有意为之"），这次保持一致。

源文件（`qml/` 下 780 行）**一个字节都没动**，仍在工作区等你点单；
构建产物里留了可还原副本 `BroadcastScheduleSheet.qml.new799`。

### 部署后仍红着的门禁

规模门禁 exit 1，唯一红灯就是上面这个文件（登记 715 → 现在 780 行，
触发"烂地基不许再叠"）。**第四刀涉及的 5 个 C++ 文件全部合规**（② 只此 1 项、
③④ 无新增）。三个处置选项留给你点单：
① 跑运行时探针验证后提交 + 更新台账；② 拆到 200 行红线内；③ 你确认后我更新台账。

---

## 九、下一刀候选（待点单）

| 候选 | 预估 | 说明 |
|---|---|---|
| 媒体库四件套（`media_list`/`media_delete`/`media_push`…） | −150 ~ 200 | 与存储域耦合，抽之前最好先定 `g_mediaDir`/`g_shotDir` 归谁 |
| **存储域独立成模块** | −? | 把 `g_shotDir`/`g_mediaDir` 收进 `core/Storage`，届时 extern 归零 |
| OTA 域 | −250 | 独立函数、命名成体系，边界清晰 |

`main.cpp` 四刀累计 **8142 → 7525（−617）**，离 600 行红线仍远。

---

## 十、脚本清单（探针类，**不入库**）

| 脚本 | 职责 |
|---|---|
| `scripts/_split_media4_ground.py` | 地基 2：时段判定搬入 MediaSession（彩排/落地） |
| `scripts/_split_media4_main.py` | 主刀：media_session 两分支体抽取 + `g_mediaDir` 外移 |
| `scripts/_verify_media4_fidelity.py` | 第四刀逐字保真校验（4 组 150 行） |
| `scripts/_verify_media3_fidelity.py` | 第三刀保真（222 行，仍可用） |
| `scripts/_build_show_errors.py` | 构建 + GBK 错误解码（本轮的"验证器"） |
| `scripts/_test_console_fix.py` | 沙箱实弹（conhost 铁证判据） |
