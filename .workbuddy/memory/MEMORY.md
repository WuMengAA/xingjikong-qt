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
- **CMD 窗口根治（10-22 收官+实弹）**：winlauncher.h 两 helper（CREATE_NO_WINDOW）；云端两任务+guard/P2P 改 wscript+vbs；agent 6 处 ffmpeg/ffplay 补齐；装机 install-autostart.ps1 防回退。实弹 4 路径全触发、零可见窗（桌面终验待现场）。机理：控制台父起的 CUI 子继承父控制台不建新窗 ⇒ 命令行跑 ffmpeg 从不弹窗；**弹窗只在 GUI 父起 CUI 子时**；Qt+CREATE_NO_WINDOW+管道 stdio ⇒ 有 conhost 但窗口不显示。
- 语音对讲 P0 已修未发版（8 槽轮转+背压）；裸 PCM 300kbps/路炸教室，Opus@16k≈1Mbps；不上 WebRTC。VoiceHub 已通；SMTC 已入（外部音源靠窗口标题+WASAPI 峰值）。
- **拆 main.cpp（三刀 8142→7708）**：已建 `media/{MediaTools,MediaSession,CameraActions}`+`core/{ProcessUtil,ExecOut}`；第四刀=媒体四件套。

## 5 踩坑实证（还会再踩）
- 抓图只用 `_shotpw.py`（DPI 感知+PrintWindow）；BitBlt 屏幕 DC 在 Win11/DWM 全黑；判有效看 PIL 黑占比<10%。
- 找主窗按类名过滤 MessageWindow/IME 再取最大。
- **监察代码三戒（10-22）**：`GetWindowThreadProcessId` 在 **win32process**（别处没有；回调 except 吞异常连演 4 轮假阴性）；对照先过黄金基准自检（DETACHED pythonw AllocConsole 必建可见窗）；**本环境看不到任何新建控制台窗** ⇒ 桌面可见性只能现场验。
- **EnumWindows 不过滤可见性** ⇒ 判可见控制台窗必须加 IsWindowVisible。
- **跨 TU 引用 main.cpp 符号前先查它在不在匿名域**（251 起那片）：不止变量，**类型**也跨不了（`ExecOut` 已提到 `core/ExecOut.h`）。
- 被控端日志 GBK；viewer-run.log 追加 ⇒ 轮询带 offset；QTimer 不 start=只有首查。
- QML 静态检查都不可信（qmllint 对不存在属性 0 Error 但整页崩）⇒ 运行时探针才是出口。**整页崩溃两坑（10-22）**：属性名避 `on`+大写开头；普通 Row 写 Layout.fillWidth 会崩（需 RowLayout）⇒ 用行宽计算；无头验证 QGuiApplication+offscreen+Qt bin 入 PATH。
