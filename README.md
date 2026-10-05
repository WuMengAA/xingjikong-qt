# Stelarith-xingjikong-qt · 星集控 Qt 线工作台

> 建目录：2026-10-04 21:00｜来源：`D:\Stelarith\{同名工程}` **cp 整棵复制**（源文件未移动）

## 一、本目录是什么

星集控「老师机—云端—教室机」这条 Qt 线的**开发工作台**。原本三个工程各自散在
`D:\Stelarith\` 下，为收拢开发位、避免跨工程找路径，2026-10-04 21:00 整棵 cp 进本目录。

**真源（旧位置）仍在原位，未删除、未移动。** 此后本目录为**开发真源**：改动只在本目录做，
改完不再往旧位置回抄（反之亦然）。两边内容以本目录为准。

## 二、三个子工程（职能三角）

| 子目录 | 技术 | 角色 | 关键锚点 |
|---|---|---|---|
| `Stelarith-control-qt` | Qt6 C++ / AUTOMOC | **教室机被控端** | 产物 `stelarith-agent-qt.exe`，托盘常驻、抓屏推帧；依赖 Core/Gui/Widgets/WebSockets/WebEngineWidgets/Network；带 OOBE 绑定（POST 站点 `/api/device/activate`）；MSI 装机包 |
| `Stelarith-viewer-qt` | Qt6 + QML | **老师机管理端** | `qml\Main.qml` + 6 个 dialog；`src\viewerbackend.{h,cpp}` 管连接/鉴权/设备表/帧/指令/文件推送 |
| `Stelarith-cloud-ws` | Node.js | **中转站 WS** | `127.0.0.1:8788`；`/ws/agent` 注册被控端、`/ws/viewer` 订阅下发；计划任务 `StelarithCloud` |

依赖顺序：`control-qt` ↔ `cloud-ws` ↔ `viewer-qt`（viewer 与 cloud 同机，8788 不起 viewer 连不上）。

## 三、接手必读：路径可见性判据（2026-10-04 21:0x 实测复核）

上一轮交接（`D:\Stellara\cl\.workbuddy\memory\2026-10-04.md` 838–906 行）A 段称
`D:\Stellarith` 前缀对 agent 进程**被定向拦截**、须盘根枚举拿 `FullName` 绕行、且源码间歇不可见。
**本轮复核：1–4 条全部复现失败。**

| 判据 | 实测 | 结论 |
|---|---|---|
| 前缀字面量被拦（`Test-Path`/`Get-Item`/`ls`/`os.listdir`） | `Test-Path`=True、`Get-Item` 命中、`ls`/`Read`/`Glob` 全通 | ❌ 不复现 |
| 必须 depth 4..6 盘根枚举绕行 | 无需绕行 | ❌ 不需 |
| 目录间歇不可见 | 连续 5+ 次调用零失败 | ❌ 不复现 |
| T-3 卡住 = 源码取不上 | `viewerbackend.h`=8498 B，与交接记录一致，源码完整 | ❌ 定性错误 |
| D 盘剩 3.5G / 100% 满 | `df` + `cmd dir` 互证 | ✅ 成立 |

**所以真正的嫌疑根因是磁盘 100% 满，不是前缀拦截。** 复制三个工程后剩余 **2.9G**。

> 备用：真撞上时再走「`Get-ChildItem -Path 'D:\' -Recurse -File -Depth 4` 拿 FullName 做 IO」，
> 命中后用已知清单直拉，不走全量枚举。

## 四、边界铁律（开工前记住）

- 只做点名的那条线；不删不搬（safe-delete 垫片会拦批量删）。
- **不重启在线被控端**；验真机只起独立实例（假 uid `TEST-RTC-001`，指本地云端），验完只杀它自己。
  `C:\Program Files\Stelarith\` 那份在线的不碰。
- 构建树建在 **C 盘**（CMake 直查源码易撞拦截）：`C:\tmp\build-ctl`（源码 cp 过去编，编完 exe 推回）。
- 工具链都不在 PATH：cmake/ninja = `C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Scripts\`；
  Qt = `D:\Qt\6.8.1\msvc2022_64`；MSVC = `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64`。
- `control-qt/CMakeLists.txt` 编译选项带 `/utf-8`（源码含中文注释，MSVC 默认 GBK 会炸）。
- `cloud-ws` 活日志 `cloud-service.log` / `events.log` 正被进程占用，**勿删勿移**。

## 五、当前进度（WebRTC 这条线）

- **T-1** 云端信令中继 ✅ 落盘 + 真链路 14/14（`cloud-ws/src/registry.js` + `index.js`）
- **T-2** 被控端推流 ✅ 真机 8/8 PASS（真 exe + 真云端 8788 + 模拟管理端；原截图像轮播与心跳全程没断）
- **T-3** 管理端收流 ⏸ **前置阻塞已消除**（见第三节），可直接开工，只改 `viewer-qt`
- **T-4** 三进程端到端真画面 ⏸ **未做**：上一轮的"管理端"是 node ws 客户端回假 answer，
  **从未有浏览器解码真帧**，这条不证明，"画面出来了"就是假象
