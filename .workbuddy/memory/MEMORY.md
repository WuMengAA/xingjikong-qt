# Stelarith-xingjikong-qt · 项目速查
> 细节见 `.workbuddy/memory/YYYY-MM-DD.md`；文档统一写 README.md。

## 0 铁律
- 裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。属实 bug 直接修并同轮闭环。
- **只改三工程** {cloud,control-qt,viewer-qt}；`D:\Stelarith\` 同名三份是 bare 远端（outer）只 push；meta 无 remote。
- **顺带发现 ≠ 点名重点**：只报清单等点单；复制只 cp 不 mv；取证避让：不跑被控端/不动桌面。
- 部署到生产安装目录；工作区只编译（隔离预览例外）。提交路径限定（永不 -A）；探针不入库。

## 1 部署 / 发版
- 生产真源 `C:\Program Files\Stelarith\`（guard 每 10min；互斥体 Global\StelarithAgentQt_Singleton）⇒ **换生产=换 exe+qml 整目录**。SOP：停任务→杀进程→改名备份→拷件→sha256→Start；装机按字节核对不看版本号。
- ⚠️⚠️ **换 qml 前必验「import 模块目录非空」+「Qt6*.dll 可达」**（10-20 血案：Controls 空目录+缺 DLL）；用「旧完整树打底+新 app QML 覆盖」。
- 云端生产=工作区份 `Stelarith-cloud/`（Cloud/Watchdog 两任务；ota.json 每请求现读，UTF-8 无 BOM）。版本真源=ota.json；stable 先 bump 再发；正式 stable 最新 0.6.46、临时承载 rc.1（10-21 授权）；**0.6.45 缺 Controls 运行库勿用**。
- ⚠️ 并行线提交前先 git log。443/wss：control.245959623.xyz/ws/agent|viewer；回源 8788。GitHub curl 恒 000 ⇒ --ssl-no-revoke。

## 2 编译 / 自检 / 隔离预览
- Qt 6.12.0 `D:/QTqwq/6.12.0/msvc2022_64`；构建 `control/scripts/_build_agent.py` 等（ninja+MSVC；C1060⇒-j1；GBK 输出）。离屏自检 STE_ALLOW_MULTI=1 -platform offscreen 看退出码（10过/20败/30兜底）。出包：agent ISCC / viewer makensis / APK make-apk.ps1。
- 隔离预览法见 10-21/22 日志；停进程按 PID（禁 taskkill /IM）。

## 3 仓库 / UI / 门禁
- github `WuMengAA/xingjikong-qt`；根仓即 meta（gitlink，无 remote）。⚠️ shared-qml 是 meta 子目录非独立仓库，随 meta 提交。
- QML 纪律：字号 12/13/14/16；锚只锚 parent；禁原生控件；NSIS .nsi 带 BOM。
- 门禁八件在 `shared-qml/scripts/`（ui-terms 扫字符串字面量 / file-size 守行数）。
- **UI 单一真源 = `shared-qml/ui/Md3.qml`+12 件组件**；AGENTS.md §9=UI 红线、§10=工程架构红线（规模门禁 `node shared-qml/scripts/test-file-size.mjs` + 46 项拆分台账 `fixtures/oversize-legacy.json`）。骨架唯一外壳=viewer `qml/shell/MainWindow.qml`。

## 4 已交付 / 进行中（查 git log）
- 0.6.45/0.6.46 发版；OOBE；二维码绑定；班级真源统一；RBAC×2；MD3+竖横屏。
- **CMD 窗口根治（10-22 收官+实弹）**：winlauncher.h 两 helper（CREATE_NO_WINDOW）；云端两任务+guard/P2P 改 wscript+vbs；agent 6 处 ffmpeg/ffplay 补齐；装机 install-autostart.ps1 防回退。实弹零可见窗（桌面终验待现场）。机理：控制台父起的 CUI 子继承父控制台不建新窗 ⇒ 命令行跑 ffmpeg 从不弹窗；**弹窗只在 GUI 父起 CUI 子时**；Qt+CREATE_NO_WINDOW+管道 stdio ⇒ 有 conhost 但窗口不显示。
- 语音对讲 P0 已修未发版（8 槽轮转+背压）；裸 PCM 300kbps/路炸教室，Opus@16k≈1Mbps；不上 WebRTC。VoiceHub 已通；SMTC 已入（外部音源靠窗口标题+WASAPI 峰值）。
- **拆 main.cpp（四刀 8142→7525）**：已建 `media/{MediaTools,MediaSession,CameraActions}`+`core/{ProcessUtil,ExecOut,AgentSettings}`；媒体域 extern 已清零（剩 4 行存储/上传域）。
- **班级域事件通道（#458 · cloud cc311ef 本地未 push）**：`src/class-events.js` 312 行——班级工作台地基，三通道正交（订阅按班 subscribe_class / 推送按班 class-event / 历史按班环形缓冲 200 条），与 devices 广播、pushEvent 流水、按设备 relayToSubscribers 三条不混。铁律：**班级归属只认云端 knownDevices 表，载荷 classCode 一律忽略**（伪冒防护）；订阅过 canAccess；四道闸门（kind 格式/data≤64KB/uid×kind 1s 限流/fanout 背压 8MB）。index.js 接线 2826→2860（台账已登记含两段增量；P1 拆 router 计划不变）。固化测试 `tests/regression-class-events.mjs` 11 断言。**cloud 本地比 outer 多 37ec4ae（广播社）+ cc311ef，用户 10-23 裁「A」= 两批一起 hold 等广播社实弹后再推**。
- **被控端缩略图上报（#459 · control-qt e809dd8 未 push）**：`src/media/ThumbnailReporter.{h,cpp}` 91+224 行——班级工作台设备网格画面源。tick 纯逻辑（ready→抓帧→黑帧→JPEG 三档降质→sender）**零 QObject 依赖**（定时器归 main.cpp QTimer，间隔 env 现读）；默认帧源=独立小尺寸 ScreenCapture（DXGI→GDI 自选，长边 320 保比例，pickTargetDisplay 比例变化重建）——**不复用 VNC 帧缓冲**（私有+远控专用节奏），DXGI 多 duplication 实例合法。端侧四闸门：未绑班/断连零空转 / 黑帧跳（锁屏·登录桌面·Session0）/ q50→35→25 守 46KB（data JSON 实测 3586B≤64KB）/ payload 无 classCode。发 `class-event{kind:thumbnail, data:{w,h,mime,cap?,b64}}`，默认 15s/台（STE_THUMB_INTERVAL_MS/STE_THUMB_REPORT/STE_THUMB_MAX_EDGE）。探针 `tests/thumbnail_probe.cpp` + `scripts/_build_thumb_probe.py`（gitignore 白名单放行）36 断言全绿。
- **viewer 班级域数据面（#460 · viewer-qt 4c16aa1 未 push）**：`src/class/` 模块 655 行——ClassService（订阅/快照/事件分发/缩略图/断线恢复，**零 WS 依赖**：出海走 sendRequested 信号→backend sendEnvelope）+ ClassDeviceModel（设备网格）+ ClassEventModel（Timeline 新在头/id 水位去重/cap 200 与云端同容）+ ClassThumbProvider（`image://classthumb/<uid>?t<rev>`，rev 破缓存）。即门禁台账 viewerbackend.cpp [P1]「拆 DeviceService/ClassService/FileService」第一刀：viewerbackend.cpp 只留 6 处委托钩子（+16 行止步于路由）。设计铁律：member 事件只改本地模型（未知 uid 不建行，清单以快照为权威）/ thumbnail 不进 Timeline / 断线保留数据+重鉴权自动重订 / 订阅被拒经 error 通道转 subscribeFailed（**pending 窗口认领、不看 m_subscribed**——已订 A 订 B 被拒是真实换班场景）/ 缩略图缓存退订不清（重订恢复 hasThumb 防闪白）。QML 入口 `backend.classService.*`。探针 `scripts/_class_probe.{cpp,py}`（复用主构建 autogen moc+只链 src/class/ 四 TU+logf 桩）16 组 47 断言全绿。**任务链余：#461 三栏 UI（Md3 真源+viewer MainWindow 外壳+消费 classService）→ #462 收口**。
- **外部 UI 提案裁决（10-23 用户转帖）**：不采纳「新建 StarControl-UI 独立工程+第二套 Theme.qml+原生控件堆页面」——撞铁律：只改三工程 / UI 单一真源 shared-qml/ui/Md3.qml / 禁原生控件 / 字号 12/13/14/16 / 骨架唯一外壳=viewer qml/shell/MainWindow.qml。**可收**：ASCII 线框图作沟通 artifact（但按既定三栏修正：外部提案两栏缺右栏快捷动作+Timeline）。

## 5 踩坑实证（还会再踩）
- 抓图只用 `_shotpw.py`（DPI 感知+PrintWindow）；BitBlt 屏幕 DC 在 Win11/DWM 全黑；判有效看 PIL 黑占比<10%。
- 找主窗按类名过滤 MessageWindow/IME 再取最大。
- **监察三戒（10-22）**：`GetWindowThreadProcessId` 在 **win32process**（别处没有；回调 except 吞异常⇒连演 4 轮假阴性）；**本环境看不到任何新建控制台窗** ⇒ 桌面可见性只能现场验。
- **EnumWindows 不过滤可见性** ⇒ 判可见控制台窗必须加 IsWindowVisible。
- **跨 TU 引用 main.cpp 符号前先查它在不在匿名域**（251 起那片）：不止变量，**类型**也跨不了（`ExecOut` 已提到 `core/ExecOut.h`）。
- **被控端 agent 日志是 UTF-8**（2026-10-24 实证纠正，旧记"GBK"是错的），路径 `%LOCALAPPDATA%\xingjikong\agent-qt.log`（含 `.log.1` 轮转）；`channel.txt`/`agent.env` 同目录。viewer-run.log 追加 ⇒ 轮询带 offset；QTimer 不 start=只有首查。
- **commit 提交的是整个 index**（2026-10-24 血案）：`git add A B` 不会清掉 index 里别处已 staged 的改动 ⇒ commit 会连带提交别人/上一轮的半成品。**提交前必看 `git diff --cached --stat`**；只提交指定路径用 `git commit -- <paths>`。
- QML 静态检查都不可信（qmllint 对不存在属性 0 Error 但整页崩）⇒ 运行时探针才是出口。**整页崩溃两坑（10-22）**：属性名避 `on`+大写开头；普通 Row 写 Layout.fillWidth 会崩（需 RowLayout）⇒ 用行宽计算；无头验证 QGuiApplication+offscreen+Qt bin 入 PATH。
- **`Array.prototype.findIndex` 没有 fromIndex 参数（10-23 #458 测试实证）**：第二参是 thisArg——想"跳过已收消息"传第 4 参整个失效 ⇒ 消息定位错位引发连锁时序错乱、连演假失败。等价需求用 `inbox.findIndex((m,i)=>i>=from&&pred(m))`。同族教训：**API 签名想当然 = 假阴性之源**（监察三戒扩展）。
- **幂等护栏要并查全部等价状态（10-23 #460 探针实证）**：subscribeClass 用 `cur = m_subscribed ? m_code : m_pendingCode` 三选二 ⇒ 已订 A 订 B 时漏判 pending 同班，连点两下 B 发两条订阅。修为 `(m_subscribed && m_code==c) || (!m_pendingCode.isEmpty() && m_pendingCode==c)` 并查。同族：**"省一个分支"的省略式是漏分支之源**。
- **探针构建第三轮同坑（10-23 #460 viewer 侧复踩）**：①link 目标目录必须先 makedirs——否则 LNK1104「无法打开文件」，现象像被占用极误导；②cl/link 输出 GBK vs 探针 exe（/utf-8 编译）UTF-8——同一脚本两种编码，解码分场合，否则 reader thread 炸；③moc 复用要按头文件内容判 Q_OBJECT（QQuickImageProvider 不是 QObject 子类，没有 moc 产物，按"每 .h 一个 moc"想当然会误报缺文件）。
- **CMake/ninja 链接是三层包装（10-23 #459 探针实证）**：真实命令 = `cmd /C → cmake -E vs_link_exe → link.exe /nologo @rsp`，obj/libs/flags 全在 ninja **rspfile**（构建后即删，磁盘不存在）——把 `ninja -t commands` 的 cmd 整串拆开改 = link.exe 根本没被调用、cmd 碎片报错、零输出秒败。正解：解析 `build/build.ninja` 的 build 语句（`|` 后=显式 libs）+ 其下变量区（LINK_FLAGS / LINK_LIBRARIES=系统库 d3d11/dxgi/ole32/user32/gdi32），自拼 link 命令。同批两个 Python 小坑：re.sub 的 replacement 含反斜杠抛 `bad escape` ⇒ 传 lambda；函数内 `import re` 会把它降为局部变量（先用在 import 前 = UnboundLocalError）⇒ import 全挪文件顶。
- **构建环境（10-25 实证，推翻旧记"PATH 无编译器所以编不了"）**：MSVC 在 `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`（**不是 2022**，MSVC 14.51）；且 `cmd.exe` 在 Bash/PowerShell 两工具都被安全策略禁 ⇒ vcvarsall.bat 路线走不通。**正解 = 直接跑项目自带脚本** `python control/scripts/_build_agent.py` / `python Stelarith-viewer-qt/scripts/_build_viewer.py`（内部 subprocess+ninja+自拼 INCLUDE/LIB，SDK 10.0.26100.0，**shared 必须带上**否则 C1083）。本机**能编译**，之前"未编译"是方法问题不是环境问题。
- **`test-file-size.mjs --update-whitelist` 是破坏性的（10-25 实证）**：它按现状**重写整个台账**，把别人的拆分计划全冲成「待拆：填写拆分计划与责任人」（island.cpp 的 [P2] 三段拆等），note 里的注册记录也一并丢（脚本重写=89 增 90 删）。**正解 = 手工改 lines + plan 末尾追加归属说明**（台账 note 的机制原文：「增量归属写进 plan…回写不等于批准变胖」）。同族教训：**`git checkout -- <file>` 撤销脚本破坏会连带丢他人在途的工作区改动**（未提交 ⇒ git 无法恢复），撤销前先看 diff 里有没有别人的未提交内容。
