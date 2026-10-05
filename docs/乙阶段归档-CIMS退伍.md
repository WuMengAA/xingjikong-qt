# 星集控 · CIMS 退伍 · 乙阶段 —— 被控端大屏通知 + 广播切云端 · 加减归档
日期：2026-10-04 ~ 2026-10-05

> ⚠️ **灾后重建说明（2026-10-05 14:26）**
> 本文件在 14:26 曾被误用 `write` 整份覆盖（原 25 节内容丢失，文件只剩 1.8KB）。
> `D:\Stellara` 无 git、无回收站、无 shadow copy，原逐字文本无法恢复。
> 现依据**三仓 git log + 甲阶段归档 + 会话记录**重建，事件、commit、验证数据均可核；
> 但个别过程细节（如当轮临时命令原文）可能不完整。此为诚实标注，不是原文。
> **教训：给已有文档追加内容必须用 `edit`，绝不能用 `write`。**

前情见：《星集控-CIMS退伍-甲阶段第一段-数据切自有-加减归档-2026-10-04.md》

---

## 一、乙阶段目标

甲阶段切的是**数据真源**（班级/用户/我的班级归一到 `/api/class`）。
乙阶段切的是**运行链路**：把广播/通知/指令的传输通道从 CIMS 代理迁到自有云端，
并让被控端能**真实呈现大屏通知**（不是只往系统弹窗里丢一条）。

## 二、被控端大屏通知（notify 动作）

**问题**：CIMS 的通知是"系统级弹窗"，教室大屏上看不见；广播页发了通知，
老师只能去被控机托盘里找。

**方案**：云端 WS 新增 `notify` 动作，被控端本地用 QWebEngine 开一个**无边框窗口**，
按 kind 分三档呈现：

| kind | 呈现 | 用在哪 |
|---|---|---|
| `island` | 灵动岛（右下角小卡片，自动收起） | 低优先级提示 |
| `popup` | 居中弹窗（需手动关） | 一般通知 |
| `fullscreen` | 全屏（占满屏，需点"关闭"） | 广播/紧急通知 |

**通知静默化**：不加 `WNA`、不抢焦点、不弹任务栏闪窗。用户点名"闪窗"是痛点。
（对应 website commit `6f64c6e fix(xingjikong): 通知彻底静默化 + 根治闪窗（用户点名）`）

**类型化呈现 v2.1**：通知带 type 字段，被控端按类型换图标/配色/文案。
（website `cba7e3e` 被控端 v2.1 + `62ebd3c` 控制台 v2.1 island/popup/fullscreen）

**送达回执**：被控端收到 → 回执带 `notice_id` 回传 → 控制台显示谁收到了。
（cloud `507af6a` 指令回执带 notice_id + receiptsByNotice 查询）

## 三、广播通道切云端

广播页（`admin/console/broadcast`）原来走 `/api/console/cims/*` 代理 → CIMS。
改为直接打云端 `/api/instructions`：

- 站点端点 → 云端 POST → WS 下发到目标班级全部设备
- 云端按 `class_id` 过滤在线设备
- 回执聚合回站点，控制台显示送达率

## 四、CIMS 代理路由处置

`/api/console/cims/*` 是整段 CIMS 代理。乙阶段起**逐页切断调用方**，代理本身
保留为兜底（CIMS 不可达时降级）。

## 五 ~ 十八、逐页迁移（班级/设备/应用/日志/音量/远控/随机机/切班/定时/文件）

按页面逐个从 CIMS 切自有/云端。关键几段：

- **班级**：甲阶段已归一 `/api/class`，乙阶段把广播页目标班级从**手输文本**
  改为 **CIMS 真实班级多选**（website `4b0e58d`），再进一步切本地班级真源。
- **切班（swap）**：用户判"切班没用"——切班是换两个班的整堂课表，
  真正要的是**调课**（换单个课时）。swap 页加 amber 停用横幅，首页入口移除，
  页面保留作历史兜底。
- **文件推送**：见第十一节。
- **定时广播**：见第十二节。

## 十九、防多进程（被控端单例锁）

**问题**：计划任务开机自启 + 装机引导 + 老师手点 start-agent.bat，多入口叠加，
实测出现 **2 个 agent 实例**在跑 —— 重复连云端抢连接、日志互相覆盖。

**实现**（control-qt `main.cpp`）：`main()` 开头 `QApplication` 之后：

```cpp
g_singletonMutex = CreateMutexW(nullptr, TRUE, L"Local\\StelarithAgentQt_Singleton");
if (!g_singletonMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
    QMessageBox::information(...);   // 提示"被控端已在运行"
    if (g_singletonMutex) CloseHandle(g_singletonMutex);
    return 0;
}
```
`main()` 结尾释放锁。

**踩坑**：`Global\` 前缀需要 `SeCreateGlobalPrivilege`，普通用户/受限上下文下
`CreateMutexW` 返回 NULL → **单例形同虚设且无报错**。改 `Local\`（同会话内足够）
并加 NULL 回退。

**验证**：实例 2 起 → 弹提示 → `RC=0` 退出 ✅

## 二十、云端自愈 watchdog

**问题**：云端 8788 挂了没人知道，agent 全部离线，控制台一片灰。

**实现**（cloud-ws `cloud-watchdog.cmd`，纯 ASCII + CRLF）：
```
netstat -ano | findstr /r /c:":8788 .*LISTENING"
```
没监听 → 记 `cloud-watchdog.log` → 直接起 node：
```
start "" /b "C:\Program Files\nodejs\node.exe" --env-file=.env src\index.js
```

**计划任务**：`StelarithCloudWatchdog` 每 5 分钟跑一次（SYSTEM）。
旧的 `StelarithCloud`（BootTrigger-only）已删。

**踩坑**：`start /b "path\to.cmd"` 会把引号内的路径**误解析成窗口标题**。
→ 直接起 node，不套 cmd。

**踩坑**：`.cmd` 必须纯 ASCII + CRLF。`write` 工具出 LF，LF-only 的 cmd
会把行尾半个字符吞掉。→ 读字节规范化，写完必查非 ASCII 数 = 0、CRLF = True。

**验证**：kill 8788 PID → watchdog ~10s 内重新拉起 ✅

## 二十一、文件推送切自有

`pushFileToDevices` 原来走 CIMS `send-notification` + `stelarith_task` 信封代推。
改为云端 `/api/instructions`：
```json
{ "uid": "...", "action": "file_push",
  "params": { "payload": { "file_id","name","sha256","kind","size" } } }
```
删掉 `env` / `getCimsAccount` / `cimsToken` / `MGMT_URL` 四个无用 import。

（website `f316032`，`file-transfer.ts` -51 行）

## 二十二、调课功能（换课）

用户原话：**"切班没有用啊，调课才是含金量啊（换课）"**

**建模**（`db.ts` SCHEMA 内新增 4 表）：
- `school_timetable`（作息节次，UNIQUE school_id, period_no）
- `class_schedules`（班级课表，UNIQUE school_id, class_id, weekday, period_no）
- `schedule_swaps`（调课申请，from/to 两个格 + status pending/approved/rejected/applied）
- `scheduled_broadcasts`（见下）

`reviewSwap` 批准时用 `BEGIN/COMMIT` 事务交换两格的 subject/teacher/remark
（空的一边 DELETE），再置 `applied`。

**关键教训**：`db.ts` 的 `SCHEMA` 是模板字符串，`ensureDb()` 只在启动跑一次
`db.exec(SCHEMA)`。**新表必须放在反引号块内**（` `; ` 之前），放外面永远不会建。
第一次就踩过：build 产物里有 SQL 但库里没有表。

**页面**：`admin/console/schedule/+page.svelte` 全量重写（原文件备份
`.cims-bak-20261005`）——班级选择器、星期×节次网格、调课申请表单、
调课列表（批准生效/驳回）、课表维护单元格编辑器。

**踩坑**：用 `{#each}` 而非内联 `.map()` JSX（后者 TS 报错）；
班级下拉用 `onchange` 不用 `$effect`（避免依赖环）。

## 二十三、全面优化（第二批）

- **notice_deliveries**：加 UNIQUE 索引去重（清掉 20 组重复）
- **_archive**：清理 217MB 历史归档
- **file-transfer 切云端**（见二十一）
- **build OOM 根治**：见下

**build OOM 排查**：`rendering chunks` 阶段在 9208 模块时稳定崩，
报错 "Zone Allocation failed - process out of memory"。系统 RAM 6.9GB 空闲也崩，
`ROLDDOWN_MAX_PARALLELISM=1` 无效 → **不是内存压力，是 `.svelte-kit` 缓存损坏**。
清缓存 + `npx svelte-kit sync` 后 build 成功。
**结论：以后 build 前先清 `.svelte-kit`。**

## 二十四、定时广播切自有

CIMS 最后一页真实依赖是 scheduled。自建：

- `scheduled_broadcasts` 表（schedule_type once/daily/weekly, run_at,
  target_class_id, next_run_at, enabled）
- `scheduled-broadcast.ts`：list/save/toggle/delete/`dueScheduledBroadcasts`/
  `markScheduledRun`/`computeNextRun`（weekly 用 `(target - getDay() + 7) % 7`，
  diff===0 且已过期时 +7）
- `scheduled-broadcast-scheduler.ts`：30s 扫 due → 对目标班级每个在线设备
  POST 云端 `/api/instructions` notify(fullscreen) → `markScheduledRun`（幂等）
- `hooks.server.ts` 加 `export const init = () => { startScheduledBroadcastScheduler(); }`
- 页面 `scheduled/+page.svelte` 的 `cimsCall` 改 `sbCall` 走自有端点，
  `PUT` 降级 `POST`（自有端点无 PUT），body 带 `id: editId`

**验证（12:46 部署实测）**：POST 建 daily 08:00 → `201 {"ok":true,"item":{...
next_run_at:"2026-10-06T00:00:00.000Z"...}}`；GET list → 200 items=1；DELETE 清理 OK ✅

## 二十五、部署收官 + 调课兼容端点

**ClassPlan 兼容端点**（`/api/console/schedule/classplan`）：把自有课表转成
CIMS 信封格式。`planId=default_classplan` → 第一个班，否则按 class_id。
稳定 subject GUID：`"sub_" + [...subject].map(ch => ch.codePointAt(0).toString(16)).join("")`。
`?subjects=1` 返回 `{Subjects:{guid:{Name}}}`。
→ ClassIsland（教室大屏第三方应用）把服务地址指过来就能读调课后的课表。

**device-status 端点**：`/api/console/control/device-status` 返回 CIMS 兼容的
`devices[]`，合并 `listDeviceBindings()` + `cloudDeviceStatus()`（uid→{online,
version,lastSeenAgoSec}），加 `bindings` 和 `fresh_seconds:30`。
这一个端点服务 devices/apps/logs/volume/remote/classisland/random 七页的 loadDevices。

**git（三仓全绿）**：
- website `f316032`（定时广播+classplan+file-transfer+清理）
- control-qt `2335281`（离线不抓屏+WebEngine 按需加载）、`3aa2de0`（RTC+OOBE 两版合一）
- cloud `d14d077`（watchdog）、`507af6a`（回执+鉴权）

**CIMS 全景（13 → 1）**：只剩 **swap 页**（用户判无价值，已停用横幅，保留兜底）。

## 二十六、CIMS 正式退伍（2026-10-05，用户确认停）

**执行**：停 PID 17984（`Stelarith-cims-eval\cims-boot.py`，python 起 8097+8100）。
8096 端口是 svchost（iphlpsvc）占位假象，非 CIMS。

**停后全链路回归（CIMS 不可达下全部正常）**：
| 项 | 结果 |
|---|---|
| 班级列表 | 200 source=**local**（本地真源 13 班）✅ |
| 设备状态 | 200 local+cloud 5 台 ✅ |
| 课表/调课 | 200（自有表）✅ |
| 定时广播 | 200（自有表）✅ |
| 设备指令 | ping **sent**（云端通道）✅ |
| /api/me | 200 ✅ |

**里程碑**：2026-10-04 启动退伍 → 2026-10-05 全链路切自有 → **CIMS 停止服务**。
站点完全自持运行。遗留 CIMS 死代码（cims-client/代理路由/reply-sync 镜像）
保留为"不可达自动降级"，待清理（不阻塞运行）。

## 二十七、工作台合并（xingjikong-qt）

用户指示工作重心转到 `D:\Stelarith\Stelarith-xingjikong-qt`（WorkBuddy 建的 Qt 线工作台，
含三端副本 + README 声明"此后本目录为开发真源"），"查看代码，安全合并即可"。

**分叉现状**：
- main.cpp：旧位置 163KB vs 工作台 151KB（工作台已带 OTA 自更新 + 0.5.0）
- cloud index.js：旧位置 23KB（生产在跑）vs 工作台 24KB（含 `/api/ota/latest` 超集）

**已合并进工作台**（control-qt）：
1. 单例锁（CreateMutexW Local + QMessageBox 提示 + main 结尾释放）
2. OOBE 班级接入码（`siteBaseFromWsUrl` + `consumeActivationCodeAsync` + codeEdit 字段 + 保存时消费）
3. 补 include：`QMessageBox`、`QUrlQuery`

**保留工作台独有**：OTA 自更新（self_update + kAppVersion 0.5.0 + QCryptographicHash）

**未合并**：采集页回收 `scheduleRtcViewReap` —— 依赖旧位置的 `ensureWebEngine`
按需加载结构，工作台是 WorkBuddy 独立实现的 WebRTC（`rtcStart/rtcStop + g_rtcView`，
无按需加载）。强并会破坏 WebRTC 线，按"安全合并"原则跳过。

**cloud-ws 无需合并**：工作台 store/registry/protocol 与旧位置完全一致，
config 是超集（+HOST），ota.js 为独有。

**验证**：用旧位置编译环境编工作台源码 → 321KB 产物（13:20）✅
**工作台建 git 基线**：605a240（13 文件，.gitignore 排除 build/node_modules/log）

**⏸ 未解**：单例锁在三个 Session 1 进程间**未拦住**（生产 + 两个测试实例都在跑）。
源码 L2824 有 `CreateMutexW`，产物二进制含 UTF-16 锁名 `StelarithAgentQt_Singleton`，
三进程同 Session 1。锁逻辑正确但实际未生效，原因未明 —— 需下次排查。

## 二十八、吉祥物（2026-10-05 14:2x · 参考 grok-icon-study）

**需求**：用户给 `Downloads\grok-icon-study-main`（Grok Bot 登录页角色动效学习复刻），
说「参考效果」「你决定」。

**决策**（我自定）：
- 位置 = 网页登录页（Svelte 直接可用、改动最小、立即可见，符合"网页只做轻量功能"定位）
- 风格 = **借鉴机制、全新角色**（Grok 形象/几何数据受 xAI 版权，其 README 明确仅供学习
  勿商用；星集控做自己的控制台主机造型）

**实现**：`src/lib/components/stelarith-mascot.svelte`（131 行，零依赖）
- 弹簧插值 `spring(cur, target, k)`（临界阻尼趋近，不带频率项）
- 姿态状态机 `idle / curious / listening / happy`（每 4-6s 随机游走）
- 多边形眼睛 + 随机眨眼（2.5-5s）+ 眼皮开度插值
- 呼吸位移 / squash 压扁 / tilt 倾斜 / 天线随视线偏
- 指针跟随视线（pointermove，clamp 到瞳孔范围）

**踩坑（值得记）**：
1. 动画变量必须 `$state`，否则 svelte-check 报 `non_reactive_update`
2. **变量名不能用 `state`** —— Svelte 编译器把 `$state` 展开后与变量名冲突，
   报 `Cannot use 'state' as a store`。改用 `mood`
3. `$state<T>()` 泛型被 svelte-check 误判为 any → 用 `let x: T = $state(...)` 显式标注
4. `let tx = 0, ty = 0` 多声明会被 rune 展开漏掉一个，必须拆开写

**验收**：svelte-check 基线 75/95/50 保持；build 成功；部署后登录页 SSR HTML
含吉祥物（`aria-label="星集控吉祥物"` + SVG）。website commit `84d7b0f`。

---

## 当前状态快照（2026-10-05 14:3x）

| 项 | 状态 |
|---|---|
| 8090 站点 | ✅ PID 44064（14:25 新 build，含吉祥物） |
| 8788 云端 | ✅ 自愈 watchdog 每 5 分钟 |
| 被控端 | ✅ TEST1 在线 |
| CIMS 8097/8100 | ✅ 已停 |
| git | website 84d7b0f / control-qt 2335281 / cloud d14d077 / 工作台 605a240 |
| svelte-check 基线 | 75 errors / 95 warnings / 50 files |

**服务全景**：星集控 = 纯自研三端一体（站点 + 云端 + Qt 被控端），CIMS 已是历史参照。

**遗留（不阻塞）**：
1. 单例锁在 Session 1 多进程间未生效 —— 需排查（见二十七节）
2. CIMS 死代码清理（cims-client/代理路由/reply-sync 镜像）
3. swap 页最终处置（删或留兜底）
4. ClassIsland 服务地址改指 `/api/console/schedule/classplan`（需教室机配置变更）
5. 工作台 vs 旧位置：生产仍跑旧位置，工作台是新真源但改动未进生产
6. UI/文案打磨、Qt 管理端（viewer-qt）开发
