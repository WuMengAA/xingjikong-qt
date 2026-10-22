# 加减归档 · 拆分 main.cpp 第三刀（相机六件套出 executeAction）

**日期**：2026-10-23 00:2x
**工程**：`Stelarith-control-qt`（被控端 agent）
**性质**：AGENTS.md §10 第 3 刀 · 结构性重构，**只搬不改**
**提交**：control-qt `c7d8466`（+342/−241，已 push outer）｜meta 待提交

---

## 一、为什么是这一刀

第二刀结束时 main.cpp 7935 行。摊开耦合度后，剩下能切的有三片：

| 候选 | 耦合情况 | 判定 |
|---|---|---|
| **相机六件套分支体** | 嵌在 `executeAction`（1695 行巨型函数）内，但**每个分支体自成一体**：入参 `params`、出参 `ExecOut out`、内部只用 `MediaTools::`/`ProcessUtil::`/三个全局 | ✅ 本刀 |
| 媒体四件套（`media_session_start` 等） | 分支体内部**直接操作 4 个播放状态全局**（19+7+7+3 处），抽走就得同时收状态 | 第四刀 |
| `executeAction` 本身的分派结构 | 要改指令分派机制 | 更晚 |

**这是第一刀切进主流程。** 前两刀搬的都是 `executeAction` 之外的独立函数，
这一刀要动那个 1695 行的巨型函数体 —— 所以纪律收得更紧：**函数体一个字都不改**。

---

## 二、新增文件

### 1. `src/core/ExecOut.h`（16 行）

```cpp
struct ExecOut { QString result; QString error; QJsonObject data; };
```

**为什么要新建这个头文件（本刀的硬障碍）**：`ExecOut` 原定义在 main.cpp **699 行**，
而 main.cpp 的匿名 namespace 是 **251–6632** —— **它落在匿名域内部**。匿名域符号是
内部链接，**跨不了 TU**。不提出来，`CameraActions.cpp` 根本拿不到这个类型，连编译都过不去。

这与第二刀的 LNK2019 是**同一个坑**（第二刀是变量，这次是类型）。区别是这次在动手前
就查了链接性，没靠链接器报错来发现。

main.cpp 699 行原位留下沉注记，说明它去哪了、为什么走。

### 2. `src/media/CameraActions.h`（27 行）

六个函数声明，头文件零实现：

```cpp
namespace CameraActions {
ExecOut listDevices();                          // camera_list
ExecOut bindDevice(const QJsonObject &params);  // camera_bind
ExecOut selfTest(const QJsonObject &params);    // camera_test
ExecOut snapshot(const QJsonObject &params);    // camera_snapshot
ExecOut recordStart(const QJsonObject &params); // camera_record_start
ExecOut recordStop();                           // camera_record_stop
}
```

⚠️ 命名说明：**动作名是 `camera_test`，函数名是 `selfTest`** —— 因为 `test` 在 C++ 里
撞 `std::test` 语义、`isTest` 又读着像谓词。函数名≠动作名这一处是有意的不一致，
映射关系写在头文件每行注释里。

### 3. `src/media/CameraActions.cpp`（280 行）

六个函数体 + extern 段 + include 区。

**extern 段（3 行）**：
```cpp
extern QString   g_shotDir;     // 截图/存图目录
extern QProcess *g_recProc;     // 正在录制的 ffmpeg 进程
extern QString   g_recOutPath;  // 正在录制的输出 mp4 路径
```

---

## 三、修改的既有文件

### `src/main.cpp`（7935 → 7708 行，**−227**）

1. **六处分支体 → 一行调用**（5169/5212/5230/5298/5354/5392）：

```cpp
// ── 相机六件套（走 ffmpeg DirectShow 输入）──
if (action == QStringLiteral("camera_list"))         return CameraActions::listDevices();
if (action == QStringLiteral("camera_bind"))         return CameraActions::bindDevice(params);
if (action == QStringLiteral("camera_test"))         return CameraActions::selfTest(params);
if (action == QStringLiteral("camera_snapshot"))     return CameraActions::snapshot(params);
if (action == QStringLiteral("camera_record_start")) return CameraActions::recordStart(params);
if (action == QStringLiteral("camera_record_stop"))  return CameraActions::recordStop();
```

**分支判断和指令语义注释全部保留在 main.cpp**（"绑定摄像头：管理端「监控中心」选定一枚后
落在这里"这类说明属于指令契约，跟着指令走，不跟着实现走）。

2. **699 行** `struct ExecOut` → 下沉注记（4 行）。
3. **顶部**加 `#include "core/ExecOut.h"` + `#include "media/CameraActions.h"`。

### `CMakeLists.txt`

登记 `src/media/CameraActions.{cpp,h}` 与 `src/core/ExecOut.h`。

---

## 四、删除的文件

本轮**零删除**。重构前备份 `main.cpp.bak-20261023-media3` 在提交后已删
（变更已入 git，可回滚；留着反而会误导后续会话以为有未完成状态）。

---

## 五、为什么分支体能"逐字搬"（本刀的关键设计）

每个分支体的原始形态就是：

```cpp
if (action == QStringLiteral("camera_list")) {
    ExecOut out;                       // ← 实际是 executeAction 的局部变量
    out.result = ...;
    out.data.insert(...);
    return out;
}
```

把 `if` 外壳换成函数壳、把 `ExecOut out;` 移进函数体，剩下的**一个字都不用改**：

```cpp
ExecOut CameraActions::listDevices()
{
    ExecOut out;
    <原文逐字>
}
```

所以脚本做的事只有三件机械变换：抽区间、去一级缩进、加函数壳。
**踩坑注释全部原样搬走**，包括：

- `-list_devices` 必须带布尔值 `true`（不带会把 `-f` 当值吃掉，一条设备都不打印，2026-10-09 实测真因）
- dshow 输入串是 `video=<设备名>` 而非 `video_device=`（后者 ffmpeg 直接报 Malformed，旧实现对相机永远打不开）
- ffmpeg 原生 stderr **不上界面**：`out.error` 只给人话、`errorCode` 给界面决定重试、`errorRaw` 留给诊断页
- `camera_record_start` 必须真等 2 秒看文件有没有长出来（`waitForStarted()` 对"ffmpeg 立刻退出"照样返回 true ⇒ 假成功）

---

## 六、验证证据

| 项 | 命令/方法 | 结果 |
|---|---|---|
| 锚点复核 | `_split_media3.py` 彩排 | 6 个 if 行 + ExecOut 行 **全部命中**，体区间 38/13/64/52/34/21 |
| 构建 | `_build_show_errors.py` | **exit 0**；CameraActions.obj 127585 B 已编入；exe 00:28:31 比全部源文件新 |
| 逐字保真 | `_verify_media3_fidelity.py` | **222 行全部逐字一致** ✓（逐段：38/13/64/52/34/21） |
| 沙箱实弹 | `_test_console_fix.py` | **PASS** |
| 规模门禁 | `test-file-size.mjs` | PASS，扫描 268 文件，台账 7935 → **7708** |

### 实弹细节（第三刀搬走的三条路径全在里面）

```
ffmpeg.exe -hide_banner -list_devices true -f dshow -i dummy      → camera_list
ffmpeg.exe -hide_banner -loglevel error -y -f dshow -i video=…    → camera_snapshot
ffmpeg.exe …                                                       → camera_record_start
ffplay.exe  -nodisp -autoexit …                                    → media_session_start
```

- ffmpeg/ffplay **真实启动 4 次**，`ffmpeg.exe` 路径解析正常
- media 产物 `test-tone.wav` 生成
- 4 个 conhost **全部"无窗口"**，全局可见控制台窗 **0 个** ⇒ CMD 弹窗根治未回归

### 关于构建器当验证器

漏改一处调用点就编译不过（`CameraActions::xxx` 未声明）。这一轮构建一次通过，
等价于**六处调用点无一遗漏**的编译期证明。

---

## 七、与预期不符的地方（必须说清）

第二刀收尾时我说过"第三刀抽完就能删掉那 8 行 extern"。**实测下来只能降 8→6 行，
不是全删。** 实测调用面：

| 状态 | main.cpp | media 模块 | 归属 | 本轮能否收 |
|---|---|---|---|---|
| `g_recProc` | 1（仅定义行） | 22 | 录像会话 | main 侧已无使用，但收它要改 recordStart 的 delete/nullptr 逻辑 |
| `g_recOutPath` | 2（定义 + island 显示读） | 14 | 录像会话 | 同上 |
| `g_playProc` | **19** | 5 | **播放会话** | ✗ **还在 `media_session_start` 分支里** |
| `g_mediaTick` | **7** | 2 | 播放会话 | ✗ 同上 |
| `g_mediaAutoStop` | **7** | 2 | 播放会话 | ✗ 同上 |
| `g_mediaStopping` | 3 | 4 | 播放会话 | ✗ 同上 |
| `g_shotDir` | 9 | 9 | **存储/路径域** | ✗ 跨 media/screenshot/抓屏 三域，收进 media 会固化反向依赖 |

**根因**：第三刀只搬了相机六件套，**媒体四件套（`media_session_start/stop`）的分支体
还在 `executeAction` 里**，那 4 个播放状态正是被它直接操作的（5318–5375 行）。
那片是**第四刀**的目标。

### 为什么本轮不顺手把 `g_recProc`/`g_recOutPath` 收掉

技术上能收（main.cpp 侧只剩 1 处读）。**没做，是有意的**：

1. 收状态要改 `recordStart` 里 `delete g_recProc; g_recProc = nullptr;` 这类精细逻辑，
   本刀"只搬不改"的性质立刻失效；
2. 一旦改了，`_verify_media3_fidelity.py` 的 **222 行逐字一致**这条最硬的证据就作废了；
3. 收 2/8 的意义有限，等第四刀抽完媒体分支，6 行能一次性删干净。

**即：把"能验证的干净切口"和"要动状态的重构"分开做，不要用一次提交混掉两种风险。**

---

## 八、明确未验证

1. **未上生产** —— 只在工作区构建 + 沙箱验；`C:\Program Files\Stelarith\` 的 exe 还是
   第二刀部署的那份（pid 26260）。第三刀是主流程结构性改动，部署待点单。
2. **未发 OTA** —— 教室机跑的仍是 rc.3（第二刀前的版本，第三刀更没进去）。
3. **本机无摄像头** —— ffmpeg 的"成功写出文件"路径没覆盖到；实弹里是"真启动后失败"
   （设备不存在），验证的是**启动路径与错误归类**，不是成功路径。
4. `camera_bind` 的写失败分支、`camera_test` 的超时分支未单独构造场景。
5. `_verify_media3_fidelity.py` 只比对了函数体；**头文件、extern 段、include 区不在比对范围内**
   （那些是脚本生成的，靠编译期验证）。

---

## 九、第四刀（待点单）

**媒体四件套分支出 `executeAction`**：`media_session_start`/`media_session_stop`/
`media_list`/`media_delete`。抽完之后：

- `g_playProc`/`g_mediaTick`/`g_mediaAutoStop`/`g_mediaStopping` 的最后一批直接访问消失
  ⇒ 四个状态可整体收进 `MediaSession`
- `MediaSession.cpp` 的 8 行 extern 段 + `CameraActions.cpp` 的 3 行 **一次删干净**
- 第二刀留下的"过渡形态"到此闭环

预计再减 150–200 行（main.cpp 7708 → 约 7550）。

---

## 十、脚本清单（探针类，**不入库**）

| 脚本 | 职责 |
|---|---|
| `scripts/_split_media3.py` | 第三刀执行器（彩排/落地，锚点复核 + 留 bak） |
| `scripts/_verify_media3_fidelity.py` | 逐字保真校验（备份 ↔ 新模块逐行比对） |
| `scripts/_build_show_errors.py` | 构建 + GBK 错误解码 |
| `scripts/_test_console_fix.py` | 沙箱实弹（conhost 铁证判据） |
| `scripts/_run_gate.py` | 规模门禁驱动 |
