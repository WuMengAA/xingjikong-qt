# 加减归档 · 管理端日志排障（viewer 0.6.24-rc.4）

日期：2026-10-10（跨 0 点，承接 10-09 夜）
触发：用户贴出 `C:/Program Files/Stelarith/星集控管理端/viewer-run.log`（数千行警告），要求按 A/B/C 三簇修复。用户选定 **A+B+C 全做**，401 令牌由其提供，ldc 构建问题授权临时处理。

---

## 〇、先说三条"账"（含我自己纠错）

1. **我上一条汇报有一句是错的**：我说"viewer 的 C++ 现在编不了（`_probe/ldc_libs` 不存在）"。**错在查错了路径** —— CMake 里写的是 `../../_probe`（相对 `Stelarith-viewer-qt` 上溯两级 = `D:/Stelarith/`），真实位置是 **`D:/Stelarith/_probe/ldc_libs/datachannel.lib`（43 MB，10-09 23:02 建）**，它一直都在。所以 **ldc 本来就编得过**，我为此做的"临时 stub + 改 CMakeLists"是**多余动作**。
   - 处置：**CMakeLists.txt 已逐字节还原**（sha256 `fda05e85…03f59`，与改动前一致），临时 stub 文件 `scripts/_ldc_receiver_stub.cpp` 已删，`build.ninja` 已重配（含 `libdatachannel 已链入（RTC_STATIC）`）。
2. **`stream N finished with error: "Host requires authentication"`（刷到 600+ 的那条）不在我们的源码里** —— 全仓 grep 无该串。它是 **Qt/Chromium（WebEngine）内部**经我们安装的消息处理器加了 `[viewer]` 前缀后打出来的。当前这版跑的是 WebEngine 收流路径（日志里没有 `[viewer-ldc]`）。**本轮不追**（属另一会话 ldc 落地范畴），标注待真机复验。
3. **用户装的 rc.3 是旧构建**：日志里 `CameraCenter.qml` 行号（230/66/76）与当前源码（337/421…）对不上 ⇒ 装的是一版**不含"监控中心表头重排"**的 CameraCenter；且安装路径仍是中文 `…\Stelarith\星集控管理端`（`InstallDirRegKey` 迁移修复未进那份）。本轮重出的 rc.4 包已包含两者。

---

## 一、A 簇 · QML 真错（6 处，已修）

| # | 症状（日志实锤） | 真根因 | 改法 |
|---|---|---|---|
| A1 | `ReferenceError: normOf is not defined`（每次点画面） | `btnOf`/`fitRect`/`normOf` 声明在**外层 Rectangle** 上；QML 非限定名只查「自身对象 → 组件根」两级，MouseArea 的手柄够不到 ⇒ **远控点击整个失效** | 三个声明**移进 `MouseArea{id:screenArea}`**（唯一使用者） |
| A2 | `Cannot assign to non-existent property "tick"`（每帧） | `property int tick` 挂在 **Image** 上，代码却写 `cell.tick++`（cell 是那格 Rectangle，没这属性）⇒ 缓存键永不刷新、缩略图看着不更新 | Image 加 `id: thumbImg`，`cell.tick` → `thumbImg.tick`（2 处） |
| A3 | `ReferenceError: hint is not defined` | `hint()` 定义在 **consolePage** 里，别的页调用必炸（代码注释 3404-3408 早就记过这一类） | `hint()` 与根级 `root.toast()` **函数体逐字相同** ⇒ 全部 16 个调用点统一成 `root.toast()`，**删掉局部 `hint`**（消除整类问题，不留第二个名字） |
| A4 | `QML CameraCenter: Binding loop detected for property "backend"` + 其后 `devices/recordings/fetchRecordings` 全 undefined | 挂载写 `backend: backend`，CameraCenter **自己也有 `backend` 属性** ⇒ 右侧被自身属性遮蔽 ⇒ **自绑定** | 根加 `readonly property var backendRef: backend`，挂载改 `backend: root.backendRef` |
| A5 | `Parameter "title" is not declared` + `Implicitly defined onFoo properties … deprecated` | Connections 里写属性名（隐式注入参数），Qt 6 已弃用 | 整组改 `function onX(...)`，`onIslandNote(title, desc, icon)` 形参显式 |
| A6 | `QML Settings: Failed to initialize …Status code is: 1` + `Qt.labs.settings is deprecated` | ① 模块已迁入 QtCore ② 落点依赖 organizationName（见 B2） | `import Qt.labs.settings` → `import QtCore` |

**文件**：`qml/Main.qml`、`qml/components/CameraCenter.qml`（均已同步到 `build/qml/Stelarith/` 运行时副本，逐字节一致）。
**验证**：`qmllint` 两文件 **rc=0**（仅剩既有 layout/unqualified 风格告警）。

---

## 二、B 簇 · C++（3 处，已修；未提交，见下）

| # | 症状 | 真根因 | 改法 |
|---|---|---|---|
| B1 | `Property 'setCurrentUid' of object ViewerBackend is not a function` | `viewerbackend.h:306` 是**纯 `void`**，没 `Q_INVOKABLE`；它是 Q_PROPERTY 的 WRITE，只能属性赋值、**不能当方法调**（紧邻的 `setScreenActive` 就是对的）⇒ 点缩略图墙切设备全废 | 加 `Q_INVOKABLE` |
| B2 | QML `Settings` status 1 / `organizationName not set` | `main.cpp` 只设了 `applicationName`，**没设组织名** | 补 `setOrganizationName("Stelarith")` + `setOrganizationDomain("stelarith.local")` |
| B3 | `收到未处理的云端消息 type=unsubscribed` | 云端对我们主动退订的正常确认，落进了"未处理"分支 ⇒ 每次切设备/离开带画面页刷一条 | 加 `else if (type == "unsubscribed")` 显式收口（不改正状态机） |

**文件**：`src/viewerbackend.h`、`src/viewerbackend.cpp`、`src/main.cpp`。

---

## 三、C 簇 · 401 / 流鉴权洪水（代码已修，配置待你给令牌）

**根因（读云端源码实锤）**：`Stelarith-cloud-ws/src/index.js:398-404` 把 `/api/classes` 与 `/api/storage` 划进**只认静态令牌 `VIEWER_TOKEN`** 的严格组；而 viewer 的 HTTP 调用是 **"有票优先发票"**（`viewerbackend.cpp:2430/2460/2490`）⇒ 登录后拿票据去拉，**必 401**。加上你机器**没设 `STE_VIEWER_TOKEN`**，连兜底都是空。

**改法**：
- `requestClasses()` / `requestStorage()` 的令牌取值改为 **静态令牌优先、票据兜底**（`m_token` → `m_cloudTicket`）。
- **止血**：这两条的失败日志改为"**只在 HTTP 状态变化时打一次**"（新增 `m_classErrStatus`/`m_storageErrStatus`），不再被 15 s 轮询刷屏。
- **仍需你给值**：在管理端的 `viewer.env`（exe 同目录）里设 `STE_VIEWER_TOKEN=<云端 VIEWER_TOKEN>`，否则静态令牌为空，仍会 401。
  - 备选修法（1 行，需重启云端）：把 `/api/classes`、`/api/storage` 从严格组挪到"静态 **或** 票据"组（与其它管理面端点一致），即可完全不依赖静态令牌。

---

## 四、版本与产物

- 版本 `0.6.24-rc.3` → **`0.6.24-rc.4`**（`scripts/bump-version.mjs bump`，4 处源同步；因 rc.3 **已发布过** ota.json，故必须前进一位）。
- 重编管理端（含 ldc 静态链）→ 出两包：

| 产物 | 体积 | sha256 |
|---|---|---|
| `Stelarith-viewer-qt/dist/stelarith-viewer-setup-0.6.24-rc.4.exe` | 82,935,251 B（79.1 MB） | `b2da2a79…78afb0` |
| `Stelarith-viewer-qt/dist/stelarith-viewer-hotfix-0.6.24-rc.4.zip` | 1,100,325 B（1.05 MB） | `79724339…dacab46` |

热修包自检：**34 条目、必需件齐**（`viewer-qt.exe` + `qml/Stelarith/Main.qml` + `CameraCenter.qml`）；`version.nsh` = `0.6.24-rc.4`。

---

## 五、提交与"没提交"的边界

- **已提交**：viewer `9d86b87` —— 仅 `qml/Main.qml` + `qml/components/CameraCenter.qml`（diff 经核对**全是我的**，无他方痕迹）。
- **刻意未提交**：`src/viewerbackend.h`、`src/viewerbackend.cpp`、`src/main.cpp`、`CMakeLists.txt`。
  原因：这些文件的未提交 diff 里**混着另一会话在途的 ldc 接线**（`viewerbackend.*` 的 diff 含 14 行 ldc 相关；`CMakeLists.txt` +32 行 ldc 块；另有两个**新增未跟踪**文件 `src/ldc_rtc_bridge.cpp/.h`）。按项目铁律「他方在改仓库时不抢提交」，我不把这些卷进我的 commit。
  - ⚠️ **代价要说清**：因此**我的 C++ 三处修复与 rc.4 版本号目前只在工作树、不在 git**（但已编进 rc.4 两个包里）。
  - ⚠️ **不能只提交一半**：`CMakeLists.txt`（版本真源）与 `main.cpp`（版本回落值）受 CMake 命名守门校验，只提交其一会让 HEAD 变成"两端版本不一致" ⇒ 干净检出直接 `FATAL_ERROR`。所以要么两个一起提，要么都不提；我选了都不提。

---

## 六、明确未做 / 未验证（不糊弄）

1. **运行时一律未目检**（不跑 exe、不点界面、不装包）：normOf 远控点击、缩略图刷新、CameraCenter 绑定、灵动岛通知参数、Settings 落盘 —— **全靠编译 + qmllint + 静态核对**，未实跑。**要灰度**。
2. **B2 的一次性副作用**：补 organizationName 后，基于 QSettings 的旧偏好（如 `prefs/firstRunDone`）落点从"无名"迁到 `Stelarith` 下 ⇒ 老用户**首启引导会再出现一次**，之后正常。
3. **401 未闭环**：等用户的 `VIEWER_TOKEN`；未改云端严格组。
4. **`stream N` 洪水未根治**：来源是 Qt/Chromium，不属本轮代码面；待 ldc 路径真机验证。
5. **被控端版本源被 bump 工具一并带到 rc.4**（工具按"两端同号"设计），但**本轮没重编/没出被控端包** ⇒ control-qt 工作树：源码 rc.4 / 构建产物仍 rc.3。下次它构建时进 rc.4。若不需要，可退回 rc.3。
6. **未发布**：没跑 `publish-release.mjs`、**没动 ota.json**（仍是 rc.3）、没推 GitHub、没同步站点。包只在本地 `dist/`。
7. 附带发现（未修，仅记账）：`qml/Main.qml` 里 `consolePage.hintText` / `hintTimer` / 那行 Text 是一套**没人写入的**死 UI（旧 `hint()` 从不设 `hintText`），恒不可见。

---

## 七、改动文件总表

| 文件 | 归属 | 说明 |
|---|---|---|
| `Stelarith-viewer-qt/qml/Main.qml` | 我 | A1/A2/A3/A4/A5（已提交 9d86b87） |
| `Stelarith-viewer-qt/qml/components/CameraCenter.qml` | 我 | A6 + 注释订正（已提交 9d86b87） |
| `Stelarith-viewer-qt/src/viewerbackend.h` | 我+他方 | B1 + C 日志止血成员（未提交） |
| `Stelarith-viewer-qt/src/viewerbackend.cpp` | 我+他方 | B3 + C 令牌优先级/止血（未提交） |
| `Stelarith-viewer-qt/src/main.cpp` | 我 | B2 + 版本 rc.4（未提交） |
| `Stelarith-viewer-qt/CMakeLists.txt` | 我+他方 | 版本 rc.4（未提交；他方 ldc 块在前） |
| `Stelarith-control-qt/CMakeLists.txt`、`src/main.cpp` | 我 | 版本 rc.4（bump 工具；未提交） |
