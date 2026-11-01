# Stelarith-xingjikong-qt · 项目速查
> 细节见 `.workbuddy/memory/YYYY-MM-DD.md`（交接优先读当日日志）；批次全账见根目录 `加减归档-*.md`。**本文件只追加/定点替换，禁止整份回写**（11-01 血案：拿上下文压缩版覆盖丢了新内容；改完 grep 关键小节确认在）。

## 0 铁律
- 裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**；属实 bug 直接修同轮闭环。
- **只改三工程** {cloud,control-qt,viewer-qt}；`D:\Stelarith\` 同名三份是 bare 远端只 push；meta 根仓无 remote；shared-qml 随 meta 提交。
- **顺带发现 ≠ 点单**：只报清单；复制只 cp 不 mv；**提交路径限定（永不 `-A`），必看 `git diff --cached --stat`**（commit 提交整个 index，两次血案）；跨线用 `git commit -- <paths>`。
- **生产动作一律受管 Python**（禁 cmd/PowerShell 落地）：`C:\Users\Administrator\.workbuddy\binaries\python\versions\3.14.3\python.exe`。

## 1 部署 / 发版
- 生产真源 `C:\Program Files\Stelarith\`；换生产=换 exe+qml 整目录（SOP：停任务→杀进程→备份→拷件→sha256→Start）。杀进程前 ctypes 取真实 exe 路径防误杀开发实例。
- 换 qml 前必验「import 模块目录非空」+「Qt6*.dll 可达」；用旧完整树打底+新 QML 覆盖。`_deploy_agent.py` 的 `QML_FILES` 是白名单（忘登记=静默缺文件；`scripts/_*.py` 在 gitignore 换机即丢）。
- ⚠️ 生产长期落后工作区（10-31 实证 exe 停 10-28）；部署前 dry-run 比对两侧 qml 项数。部署脚本别重跑 `--go`。
- 云端=工作区份 `Stelarith-cloud/`；版本真源 ota.json；改代码必须重启 `_restart_cloud.py`；ota*.json/assets/pkg 均 gitignore。
- viewer 发版：手改两副本（CMakeLists+main.cpp；`bump-version.mjs` 单品升版不能用）→ `_build_viewer.py` → makensis → `publish-release.mjs publish --product viewer`（stable 与 rc 各一次）。
- 443/wss：`control.245959623.xyz` 回源 8788；GitHub curl 加 `--ssl-no-revoke`。

## 2 编译 / 自检
- Qt 6.12.0 `D:/QTqwq/6.12.0/msvc2022_64`；正解=跑 `_build_agent.py`/`_build_viewer.py`（shared 必带否则 C1083）；MSVC 在 VS 18 BuildTools；cmd 被禁 vcvarsall 走不通。
- qml.exe GUI 子系统 console.log 不进 stderr ⇒ 需 `QT_QPA_PLATFORM_PLUGIN_PATH` + `QT_ASSUME_STDERR_HAS_CONSOLE=1`；offscreen 截图必须 `QT_QPA_FONTDIR=C:\Windows\Fonts` 且等动画落定。

## 3 仓库 / UI / 门禁
- github `WuMengAA/xingjikong-qt`；根仓即 meta。UI 单一真源=`shared-qml/ui/theme/Theme.qml`+`ui/App*.qml`(10 件)；Md3.qml 上一代令牌存量退役中；两端 md3 组件层已归零。
- `Theme.neutralAccent`：一处开关全部选中态变中性反白（被控端「不要紫色」）；**OOBE 是独立文档须在 OobeWizard.qml 再设**。
- ⚠️ 10-29 用户裁决：两端全量 QtShadcn（vendor `third_party/qtshadcn`）。**`QT_QUICK_CONTROLS_STYLE=Basic` 必做**（两端 main.cpp qputenv）；QtShadcn 是 static 模块 ⇒ 裸 qml.exe 报 plugin not found；Theme.qml 自带调色板不 import 它。
- 门禁十件在 `shared-qml/scripts/`：file-size（台账 `fixtures/oversize-legacy.json`，**只手工改 lines+plan，禁 --update-whitelist**）/ ui-terms（禁用词：教室机·机器·电脑·节点·黑屏·投屏·学生机 ⇒ 一律「设备/X端」）/ ui-contract / qml-structural / module-registry 等。
- 被控端页=`qml/shell/AgentPages.js` 声明表；AGENTS.md §9=UI 红线、§10=工程红线；**§7 第三条（11-01）：开工前 `wbds.mjs kickoff xingjikong` 查跨线便签**（工作台 `D:\DeepSeekHarness\workbuddy-deepseek-workbench`，只是便签墙不是闸；WorkBuddy 侧无 wbds_* 工具只能走 CLI）。

## 4 现役能力（判断实现 ⇒ grep blob，不信提交信息/文档）
- 设备身份两维 `(归属码, role∈{classroom,broadcast})`；班级归属只认 knownDevices；班级接入码 6 位票 → `/api/device/activate`。
- 课表：72 ClassPlan=12 群×6 天；排序=AssociatedGroup 首现顺序；TimeType 0=上课/1=课间/3=行动。
- 云端班级域 class-events/room/data/admin/timetable.js；viewer 班级域 `src/class/`；发布中心 `POST /api/class/publish`。
- **灵动岛顶替 ClassIsland（11-01 三步全完，全未部署生产）**：①EndTime 绝对结束时刻（setEndTimeForNext，d2e8ca6）；②通知提供方注册制 8 提供方×渠道（`src/notify/provider_registry.*`，未知名放行，门控已全接线含新增 notifyExamMode，d4330cf/239e3db）；③排队仲裁——低优先入队（队列 4）不顶当前、enterIdle 出口补播（e599103）。**抢占在播未做**。探针：--endat/--providers/--queue 均带反向对照。ClassIsland 是 GPL-3.0：只学机制不移植不复制。

## 4.5 盘点纪律（10-31）
- 文档/旧记录的"未完成"可能是过时误判（10-31 连抓两处）；"API 已就位"要验真存在 ⇒ **动手前先 grep 核验**。

## 5 踩坑实证（精选高复发）
- QML 类型静默抢占：显式 import 优先级高于隐式 ⇒ 同名包装件永不被用；包装件与自身同名=自引用。
- x/y 恒相对父坐标；底色看"父是谁"（页面上用 surface，面板上用 surfaceVariant）。
- **QML 静态检查全不可信**：两个 Component.onCompleted=整页不加载；删 wrapper 漏补根级 `}`=整页静默空白（只能 qml.exe 直接加载诊断）；唯一可信出口=真 exe/探针运行时。
- `.pragma library` 必须在前 128 字节（中文注释会超）；copy_directory 只复制不删旧件；configure 期 QML 扫描滞后一个周期。
- LNK1104=exe 被占用（并行线在 build/ 起的实例**不杀**）；taskkill //PID。
- 跨子目录引兄弟组件必须 `import "../"`；跨 TU const 默认内部链接须 `extern const`；**搬家后去 inline**（单 TU 定义不产外部符号⇒LNK2019）；纯搬迁不动对齐。
- 布局红线：班级工作台三栏最小 1220 宽；「按钮按不动」=在可视区外。
- 日志：被控端 `%LOCALAPPDATA%\xingjikong\agent-qt.log`；抓图只用 `_shotpw.py`（BitBlt 全黑）；临时 QSettings 写入不落地须显式 sync；本会话注册表可能 AccessError（探针切 ini）。
- git stash/pop 在 autocrlf 下静默改行尾；`.nsi` 会丢 UTF-8 BOM。

## 6 落点
- **被控端优先、岛为主入口**（老师面对讲台机）：老师当场用的能力主入口一律放被控端/岛；管理端面板是陪跑。

## 7 顶层指导
- QML 纯图形禁逻辑；远程桌面=DXGI+FFmpeg H264+WS 透传（待建）；Local API 127.0.0.1:8080（待建）；多租户 class_id（待建）。
- UI 规范（10-29）：去边框；圆角 Theme.radius.lg；颜色一律 Theme.color.* 禁硬编码 Hex；禁裸 Button/TextField 必须(AppButton|AppInput)；只用 Layout；单文件 ≤300 行。
- main 拆分已完成（9026→~4443/4768）：只保留「只降不升」护栏（check-main-size.mjs）；新业务进 `src/<domain>/`；跨 TU 依赖注入 `g_xxxDeps` 模式。

## 8 viewer 打包与外壳（11-01 并行线实证）
- 打包三坑：`file(GLOB)` 须 CONFIGURE_DEPENDS；NSIS SetOverwrite 默认 off（升级装 rc=0 但 exe 旧）+ 跨 Session 杀不掉 ⇒ Rename 让路；.nsi 丢 BOM。
- QtShadcnplugin 曾无人依赖从未构建（viewer 打不开根因）；修=add_dependencies+整目录同步。
- 外壳：10-29 只迁 9 个页面、外壳从未迁移；11-01 并行线已转正新外壳（退役 5 经典件、删预览闸、rc.10 已发 OTA）；**viewer 已发版，被控端仍落后多个提交（U盘/EndTime/提供方/接线/排队，未部署）**。
- 命名纪律：`backend`=全局 context property 可直用；`backendRef`=转发名，子件自行声明同名会 Binding loop 全 undefined。
- 遗留待办：用户曾点「切 QtShadcn 主题」指 viewer 外观层选错（App* 自绘层 vs QtShadcn 原生件），**被用户叫停未做完**；广播站地址更新（voicehub-2026.netlify.app）任务未开始即被打断。
