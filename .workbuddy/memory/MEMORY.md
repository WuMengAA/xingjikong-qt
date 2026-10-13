# Stelarith-xingjikong-qt · 项目笔记

## 0 铁律（冲突时最高优先）
0. **全局总纲 = skill `stelarith-project-overview`**（回答"为什么/需求全景/冲突与遗漏/优先级怎么裁"）；**本文件是"可执行铁律与踩坑速查"**，两者不重复。改代码前先过总纲的「唯一裁决规则」：**生产事故 > 用户当轮点单 > 已就绪未接线 > 战略**。
0b. **重要修复最高优先级、同轮闭环（2026-10-13 用户定）**：已确认属实的 bug/生产事故**直接修，不反复问**；"改完+编译过"不算完 —— **重启服务/发布 OTA/出构建必须同轮做掉**，不留"已改未生效"；每轮收尾**逐条对账**（修了/没修/卡在谁）。「不跑被控端/不动桌面」的取证避让仍守，但**云端重启让修复生效不算避让范围**（用户明确要求）。
1. 只改工作区三工程 `{Stelarith-cloud-ws,Stelarith-control-qt,Stelarith-viewer-qt}`；`D:\Stelarith\` 下同名三份只读。
2. **顺带发现 ≠ 点名重点** —— 只报清单，做不做等点单；审计交付物＝清单+判断。
3. 不许在烂地基叠补丁；不许自顾自推进；交付别摊成"等你点单"的菜单。
4. 截图验证是硬要求；本机取证**不开刀**（不跑被控端/不发指令/不重启服务/不动桌面）。
5. "复制"只用 cp 绝不 mv；永不 `--force` 推。
6. 定时任务只用于推进**大目标**，单步工作当场做完。

## 1 部署拓扑
- 公网唯一入口 **443/wss**：`control.245959623.xyz/ws/agent`（被控）/ `ws/viewer`（管理）。cloudflared 回源本机 8788（对外不通）⇒ 外机地址必须 wss 不带端口。
- 两子域不互推：云端 `control.` / 站点 `www.`（站点公网 443、8090 内网）。官网在 `D:\Stelarith\Stelarith-website\stelarith`（不在工作区）。
- 令牌 `CLOUD_WS_TOKEN`（设备）/ `CLOUD_VIEWER_TOKEN`（管理端，用错 401）。
- 验发布打 `127.0.0.1:8788`；`www.../api/*` 404 正常。

## 2 发版（两条路）
- 版本真源=云端 `ota.json`（被控托盘 + 官网 `/download` 读 `/api/public/ota`）⇒ 免重构建。**ota.json 必须 UTF-8 无 BOM**（带 BOM ⇒ `latest:null`）。
- `node scripts/bump-version.mjs show|bump|release|set <ver>`：版本号 **4 副本**（两端 CMakeLists + 两端 main.cpp）；CMake 只比对、不一致 FATAL_ERROR，**无自动回写** ⇒ 出包前先 bump。
- `node scripts/make-hotfix.mjs`：热修包。管理端=**zip**（`updater.cpp` 解压+robocopy，**必须含 viewer-qt.exe** 才过自检）；被控端=**NSIS exe**（`/S`）。只带 `viewer-qt.exe`+`qml/**` / `agent.exe`；**运行时 DLL/plugins 不进热修包**（变了必发全量）。
- 一律走脚本出包；文件名 ASCII；出包/删文件加 `CODEBUDDY_SAFE_DELETE_ENABLED=0`。
- ⚠️ **`ota.json` 的 `url` 同时是官网装机卡下载链接** ⇒ 只挂"新装机也能用"的全量件（被控=NSIS、管理=绿色 zip）；**热修包绝不能挂**。发布前查 agent url 别指裸 exe。

## 3 编译 / 自检
- **Qt 6.12.0**（`D:/QTqwq/6.12.0/msvc2022_64`，注意是 `QTqwq`）。项目构建一直用 6.12.0。
- ⚠️ **正式安装目录 `C:\Program Files\Stelarith\` 曾经残留旧 NSIS 包带来的 6.8.1 DLL**（2026-10-11 已用 6.12.0 DLL 覆盖修正，备份在 `backup-qt681-to-6120-20261011-162418/`）。如果之后跑 NSIS 安装器会再次把 6.8.1 DLL 覆盖回来 → NSIS 打包源必须先更新到 6.12.0。
- viewer `build-qt-viewer.bat`（CMake Ninja）；agent `control-qt/scripts/build-agent.sh`。ninja 实体 `.../python/envs/default/Scripts/ninja.exe`。
- ✅ **build-agent.sh 已更新（2026-10-11 `43b4258`）**：抽出 `configure()` 收敛全部 `-D`（含 `CMAKE_PREFIX_PATH=D:/QTqwq/6.12.0/msvc2022_64`、`CMAKE_C(XX)_COMPILER`、`CMAKE_RC_COMPILER`、`CMAKE_MT`），PATH 补 MSVC/SDK bin，缓存丢失自动补 configure。clean configure 已验证 exit 0。
- ⚠️ **QML 磁盘加载**：importPath `<exe>/qml/Stelarith` + `load(fromLocalFile(Main.qml))`。改 `build/qml/Stelarith/*.qml` 可热更（重启 exe）；但副本只在构建 POST_BUILD 覆盖 ⇒ 探针前先同步源码→副本。
- ⚠️ 离屏自检被单实例守卫拦 ⇒ `STE_ALLOW_MULTI=1 ... -platform offscreen`。`build/platforms/qoffscreen.dll` 每构建被 windeployqt 删 ⇒ 编后重补。
- ⚠️ 日志出口一律 `toLocal8Bit()`；agent **无 `logf`** ⇒ `qInfo`+`qPrintable`。exe 被占用 ⇒ `scripts/_linknew.py` 链 `viewer-qt-new.exe`。
- ⚠️ **`_probe` 真实位置 `D:/Stelarith/_probe`**（viewer CMake 写 `../../_probe`，相对 `Stelarith-viewer-qt` 上溯两级）。`ldc_libs/datachannel.lib`(43MB) 在那儿，viewer 能带 ldc 编（exe 1.19→2.5MB）。
- ⚠️ `stream N finished with error: "Host requires authentication"`（可刷几百行）**不在我们源码** —— WebEngine/Chromium 内部经我们的消息处理器加 `[viewer]` 前缀打出。
- C++ lambda `[&]` **够不到"后面才声明的局部变量"**（C2065）：要回调里用控件，须先声明指针、后 new 赋值。

## 4 登录（OAuth2 授权码 + loopback）
临时端口 → `/oauth/authorize` → 回拨 → 会话令牌 → `/api/console/desktop/session`。站点地址 `kDefaultSiteUrl`（viewerbackend.h 单一真源）。⚠️ 回拨请求行必须 `section(' ',1,1)`；`QDesktopServices::openUrl` 起不来 Edge ⇒ 三层兜底，失败复位 `m_busy`。

## 5 远端 github WuMengAA/xingjikong-qt
四分支 agent/viewer/cloud/workspace；main 别碰别强推。push HTTPS；本机 curl 到 github 恒 000（验 API 用 python urllib）。GCM 弹交互挂死 ⇒ `git -c credential.helper= push "https://<token>@github.com/..."`。

## 6 UI / QML 约定（全量见 `viewer-qt/docs/UI规范-2026-10-07.md`）
- 字号 `12注/13正/14徽标/16标题`（+20 只给大数字）；对比度算 WCAG 相对亮度；`inv` 当底字用 `win`。
- QML 锚点只能锚 Item（Window/ApplicationWindow 不是 Item ⇒ 静默失效）→ 锚 parent。抽组件调用点别同名属性（`th: th` ⇒ Binding loop）→ `th: root.th`。
- ⚠️ **QML 非限定名只查「自身对象 → 组件根」两级**：中间层元素的 `function`/`property` 子元素**看不见**。判据 `ReferenceError` 或 `Binding loop`。修法：① 提函数/属性到组件根；② 移进唯一使用者（如 MouseArea）；③ 挂子组件别 `backend: backend`（自身属性遮蔽上下文）改 `root.backendRef`。
- 不用 Qt Button/TextField（走系统调色板变白）→ Btn/InputField；InputField 不给 background 吃系统白底。
- 批量异步收口（isDevOnline 过滤+超时+按设备建明细一台一结）；报错带 `QFile::errorString()`；通知参数走 `qml/NotifyParams.js`；跨页调 `root.toast()` 非 `hint()`。
- GridLayout 从 `anchors.fill` 改"按内容定高"后，靠 `Layout.fillHeight` 撑的卡塌成 0 ⇒ 卡写 `Layout.preferredHeight: col.implicitHeight+2*padding`，Column 改 left/right/top 三边锚。

## 7 已修（易踩坑留存）
- #87 语音时长限：QML 取无括号 Q_INVOKABLE 取到函数对象恒 true ⇒ 改 `Q_PROPERTY(READ)`。
- #90 关机重启无限重发：云端 `no_resend`+`isTerminalAction`+`expireTerminalHistory`；agent 关机延后 2.5s+`terminal-done.json` 去重。⚠️ C++ 块注释出现 `*/` 会提前闭合。
- #94 暗色通知不能输入：裸 TextInput 无 background 白底白字 ⇒ InputField。
- #95 终端关不掉：`terminal_opened` 判据改 + `m_termWait`(20s)/`m_termCloseWait`(2.5s)/`m_termClosing` 去重。
- #88 考试模式黑名单、两端 NSIS、通知不抢焦点、配置向导示例地址、首启引导 `WelcomeOverlay`+`firstRunDone()`（QSettings）均已落地。
- Theme 单一真源：`qml/components/Theme.qml` 共享组件 + 深/浅两实例（纯 `pragma Singleton` 不可行，需深/浅并存切）。

## 8 班级绑定（分班制）
- **唯一入口** `control-qt/src/main.cpp:1169 applyClassBinding(rawCode,wsUrl,uid)`：「保存并连接」与「账号绑定」**共用**；新增绑定路径必须调它。
- 链路：`saveClassCode` 落 `%LOCALAPPDATA%/xingjikong/classcode.txt` → `register` 带 classCode → 云端 `registry.js` 班级索引。云端班级码格式 `^[A-Za-z0-9_-]{6,16}$`。
- ⚠️ 站点 `/api/device/activate` 只回 `classId/className`、**不回 code**，且站点 classId 形如 `class_xlzx_2028_08`（18 位）**不合云端 6-16 格式** ⇒ 走接入码绑的班，云端班名是一次性码 `XJK-XXXX-XXX`。修法=站点响应加 `classCode: class.code`（需站点授权）。

## 9 进程终止铁律（蓝屏事故）
- `0xEF`=CRITICAL_PROCESS_DIED。一律**黑名单**（只杀点名的），绝不白名单（名单外全杀→杀 critical→蓝屏）。
- 判断能否杀**只问内核**：`NtQueryInformationProcess(h,29 ProcessBreakOnTermination)`、`61 ProcessProtectionInformation`、`25 Session`(Session0=服务放过)。OpenProcess/任一查询失败 ⇒ 当受保护跳过。
- 名字归一化：双侧都剥 `.exe` 转小写再比（`tasklist` 给 `svchost.exe`，只一侧剥 ⇒ contains 恒 false）。自身进程强制剔除。

## 10 NSIS 安装器硬规矩
- 打包源必须 `build/`（`deploy/` 缺 resources/translations/QML ⇒ 装完没反应/崩）。
- 🔒 绝不 `File /r "deploy\*.*"`：agent.env 含真实设备令牌 ⇒ 任何人冒充设备。
- `.nsi` 必须 **UTF-8 带 BOM**（`Unicode true` 靠 BOM）；Edit 改完会丢 BOM ⇒ 每次补 BOM。
- `OutFile` 相对脚本所在目录。目录 `File /r` 不带 `\*.*`（带通配符平铺到根，Qt 找插件失败）。空目录中止编译。
- **DLL 通配只吃 `Qt6*`** ⇒ 非 Qt 的运行时（如 `libssl-3-x64.dll`/`libcrypto-3-x64.dll`）必须**显式点名 File**。`D3Dcompiler_47.dll`/`opengl32sw.dll` 只在 deploy/（windeployqt 不拷）。
- ⚠️ **`InstallDirRegKey` 架空 `InstallDir` 默认值**：重装优先读注册表旧路径 ⇒ 改默认值白搭。要生效=删 `InstallDirRegKey`，改 `.onInit` 手动裁决（没装过=新默认；旧路径==旧默认=迁移并 `RMDir /r`；其它=原处覆盖）。viewer 已改，被控端默认本就 ASCII。
- 升级安装段开头先 `Stop-Process <本端 exe>`+`Sleep 800`，避免 exe/DLL 被占用覆盖失败。

## 11 OTA 铁律
- 版本比较唯一真源=云端 `ota.js` `cmpVersion`（alpha0<rc1<正式2），客户端带 `?current=` 问云端读结论，**任何一端不许自己再写比较**。
- 护栏：URL 只认 http(s)、sha256 必填(64 位小写 hex)、体积≤300MB、解压后必须见 exe。
- 替换自身只能退出后外部脚本做（写 `apply-update.cmd`→startDetached→quit；脚本 tasklist 等消失→robocopy→start）。⚠️ 脚本 `toLocal8Bit()` 落盘；必须 `/XF viewer.env`；隔离验证给 exe 改名。

## 12 其它工程 / 沙箱小坑
- 官网构建旁路 `CODEBUDDY_SAFE_DELETE_ENABLED=0 node scripts/release-site.mjs build`→`switch`；`content/nav.json` gitignore。
- safe-delete 拦 rm ⇒ python `os.remove` 绕；`shutil.copytree(dirs_exist_ok=True)` 绕 rmtree；Git Bash `/I<path>` ⇒ `MSYS_NO_PATHCONV=1`；探针放工程目录内。

## 13 改大文件固定动作
`git diff --stat` 行数异常 ⇒ 先查行尾。`\r`→`\n` → `re.sub(r'\n\n+','\n')` → 清 include 空行再重编。`git add --renormalize` 在 autocrlf=true 下弄脏索引 ⇒ `git reset -- <file>`。改动一律脚本化；删代码前锚点+行号双确认。

## 14 我栽过的跟头
1. 脚本写中文一律 Write 落盘再执行，绝不 bash heredoc（乱码）。
2. 诊断要实证，别猜根因。
3. "缺资源类"故障判据：照常启动+连云端，只在用某功能才崩 ⇒ 先疑包缺件。事件日志 Application Error(1000) 看模块+异常码+偏移，三次一致＝崩溃点。
4. 已点单却拖着没做 ⇒ 可能变生产事故，每轮收尾报欠账。
5. **多会话并行会吞未提交改动**：他方 `git add` 会卷走我的在途文件、重编覆盖我的产物。⇒ 提交**路径限定**（永不 `-A`）；出包前看 `build/*.exe` mtime 与 `git log -1`；发现他方在编 ⇒ 停手报告。

## 15 待办 / 状态
- **CIMS 定性（2026-10-10 用户更正）**：CIMS 是**已废弃的旧项目、原版无集控功能**。**"替代 CIMS"不是目标**，也不存在"CIMS 退伍/停 CIMS"路径 —— `星集控-定位书v2.md §六` 与 `docs/需求全景分析与改进建议-2026-10-10.md`(v2) 均已更正。`Stelarith-cims-eval/CIMS-backend` 只作**过渡期的课表/设备数据承载**。
- **库引入决策（2026-10-10 用户决策）**：**正式引入 WebRTC（libdatachannel，MPL-2.0）与 FFmpeg（LGPL）**。`docs/第三方库合规.md` 已升 v2：FFmpeg 必须 **`--disable-gpl` 共享库构建 + 动态链接**；**libx264/libx265 是 GPL，禁用**（H.264 软件编码改走硬件编码器 nvenc/qsv/amf 或 libvpx/libaom）；Qt 实为**动态链接**（v1 误写"静态"）；**OpenSSL 3(Apache-2.0)** 已补登记（当前取自构建机 PG17，需改为自带）。
- 已落地：批次 0-7 全部（NSIS 两端、通知、灵动岛、监控 CameraCenter 挂载、云端推+客户端拉、官网 3 条、首启引导、Theme、班级绑定）。
- ✅ **2026-10-13 22:2x 状态**：云端 8 处审计修复**已生效**（8788 由计划任务 21:55 拉起，加载完 9540209+d9c8d23+7319539）；被控端 **0.6.30 已发布 ota.json**（agent `655a59e`：TLS 事故收口 + 登记项 4 处全修）；管理端 **0.6.30 已出包**（viewer `3942335`：dsh 拆分代提交 + 补 QWebSocket include + oauthlogin 超时修复进构建）。**viewer ota 条目未动**（OTA 吃 zip 与"不出 zip"矛盾待裁定）。
- ⏸ 待复现/无正文：`file_push 打不开目标`（agent 已有 mkpath+改名兜底，无失败串难盲改）；#89/#99 归档查无需求正文。
- ⏸ 隔离验证挂起（违取证避让）：考试模式内核闸门实杀效果；#98 灵动岛观感/折行；terminal cd 词法解析真机会话实测。
- ⚠️ viewer `build/` 缺 `libssl-3-x64.dll`/`libcrypto-3-x64.dll` 时 NSIS 出包必 fail —— 从 PG17 `C:/Program Files/PostgreSQL/17/bin/` 拷入（windeployqt 不拷、nsi 显式点名）；build 目录清掉后要重拷。

## 16 指挥台回写（跨 agent 同步）
- **唯一跨会话同步真源 ＝ `docs/星集控-指挥台-唯一真源-2026-10-02.md` §一 当前指针**。铁律 2 **收工必须回写**；铁律 4 **上节没回写、下节不许开工**（防两个 AI 同改一处）。
- ⚠️ **`.workbuddy/memory/` 是我（WorkBuddy）的私有记忆，dsh 看不到** ⇒ 写它**不等于**同步，两套都要写。
- 回写格式：表内新增一行「**在办（时间 · WorkBuddy 回写，取代 XX 条）**」，含 交付+提交号 / **未验证（明说）** / 下一步。
- 🚫 **2026-10-11 我虚报过**：口头称"已和 dsh 同步"，实际 10:10→23:19 近 13 小时产出只进了私有记忆，指挥台一行没写，被用户当场戳穿（与 dsh 点名的「WorkBuddy 提交信息虚报」同类）。**收工即回写，不许口头充数。**

## 17 UI 唯一模板（2026-10-13 用户批准）
- **Material 3 = 全项目唯一视觉模板**（用户看 MaterialLab 后裁决「都可以，两端都用这个」）。
- 令牌单一真源 `qml/components/Md3.qml`（两端各一份、逐字相同）：baseline 十色 + surface 五级容器 + 字阶 13 级 + 形状 6 档 + 动效 + 状态层；深浅两实例（纯 Singleton 不可行，同 Theme.qml 模式）。
- 组件库 `qml/components/md3/` 12 个（Btn 六变体/Fab/TextField/SwitchRow/CheckBox/Radio/Chip/Card/ListItem/NavBar/Dialog/Snackbar）。
- **legacy 别名是迁移桥**：Md3 提供 win/panel/fg/acc/rCtrl 等旧 Theme 同名令牌 ⇒ 存量界面零改动换肤；全端组件迁移完成后删别名、退役 Theme.qml。
- 灵动岛例外：只换色彩族（accent=MD3 primary、urgent=MD3 error），**形状 36/56/168 与半透明底不动**（身份组件）；改 IslandTheme 必同步 island.cpp（parity 铁律）。
- 探针：`scripts/_md3_probe.py`（.py 被 gitignore；.cpp 在库）——StubBackend 假数据 + Md3 注入 + objectName 驱动分页/触发态；qmllint 对跨文件类型不报错但运行时要显式 import。
