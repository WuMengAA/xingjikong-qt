# Stelarith-xingjikong-qt · 项目速查
> 细节见 `.workbuddy/memory/YYYY-MM-DD.md`（append-only）。说明文档统一写 README.md，不新建 docs/。

## 0 铁律
- 裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。属实 bug 直接修并同轮闭环。
- **只改三工程** `{Stelarith-cloud,Stelarith-control-qt,Stelarith-viewer-qt}`；`D:\Stelarith\` 同名三份是 bare 远端（remote `outer`）只 push；meta 无 remote。
- **顺带发现 ≠ 点名重点**：只报清单等点单；「复制」只 cp 不 mv；取证避让：不跑被控端/不动桌面。
- 部署到**生产安装目录**；工作区只编译（隔离预览例外）。提交路径限定（**永不 `-A`**）；探针 `scripts/_*.py`/`_*.mjs` 不入库。

## 1 部署 / 发版
- 生产真源 `C:\Program Files\Stelarith\`（AgentGuard 每 10min；互斥体 `Global\StelarithAgentQt_Singleton`）⇒ **换生产=换 exe + `qml/` 整目录**。SOP：停任务→杀进程→改名备份→拷件→sha256→Start；装机后按字节核对不看版本号。
- ⚠️⚠️ **换 qml 前必验「每个 import 的模块目录非空」+「依赖的 Qt6*.dll 可达」**（2026-10-20 血案：`QtQuick/Controls` 空目录+缺 DLL ⇒ 5 面板一崩，编译零症状）。用「旧完整树打底 + 新 app QML 覆盖」。
- 云端生产=工作区份 `Stelarith-cloud/`（任务 StelarithCloud/StelarithCloudWatchdog；`ota.json` 每请求现读）。版本真源=云端 `ota.json`（UTF-8 无 BOM）；stable 先 bump 再发。
- **stable 临时承载 0.6.47-rc.1**（2026-10-21 授权全量铺 rc）；正式 stable 最新 `0.6.46`；**0.6.45 缺 Controls 运行库勿用**。
- ⚠️ 安卓版本号改了可能不生效 ⇒ 改完必 badging 复核。⚠️ 同工作树有并行线：**提交前先 `git log`**。
- 443/wss：`control.245959623.xyz/ws/agent|viewer`；cloudflared 回源 8788。GitHub curl 恒 000 ⇒ 加 `--ssl-no-revoke`。

## 2 编译 / 自检 / 隔离预览
- Qt 6.12.0 `D:/QTqwq/6.12.0/msvc2022_64`；`control/scripts/_build_agent.py`、`viewer/scripts/_build_viewer.py`（ninja+MSVC -j4；C1060⇒-j1；输出 GBK 手动 decode）。离屏自检 `STE_ALLOW_MULTI=1 -platform offscreen`，看退出码（10过/20败/30兜底）。
- viewer 隔离预览：cp 云端最小集到 %TEMP，`CLOUD_WS_PORT=18788`；viewer 用 `STE_VIEWER_URL=ws://127.0.0.1:18788/ws/viewer` + `STE_VIEWER_TOKEN=CLOUD_VIEWER_TOKEN`（不是 dev-cloud-token）；假设备必须发心跳（10s）。**停进程按 PID，禁 `taskkill /IM node.exe`**。
- ⚠️ viewer 认 `build/qml/Stelarith/<pages|shell>`；只改 QML 时 build/qml 不自动更新 ⇒ `viewer_qml_sync`/`agent_qml_sync`。
- 出包：agent `build-agent-installer.sh`(ISCC)；viewer `makensis scripts/make-installer.nsi`；APK `scripts/make-apk.ps1`。

## 3 仓库 / UI / 门禁
- github `WuMengAA/xingjikong-qt`；根仓即 meta（gitlink，无 remote）。⚠️ `Stelarith-shared-qml` 不是独立仓库，是 meta 子目录，共享 ui 随 meta 提交。
- QML 纪律：字号 12/13/14/16(+20)；锚只锚 parent；不用 Qt Button/TextField；符号字形 tofu⇒SvgIcon 或汉字。NSIS `.nsi` UTF-8 带 BOM。
- 门禁 `shared-qml/scripts/` 七件：ui-contract/action-manifest/island-*/module-registry/agent-qml-deps/plugin-island-gate/ui-terms。⚠️ ui-terms 扫字符串字面量（16 禁用词）；C++ 只扫 QStringLiteral。
- **UI 单一真源 = `shared-qml/ui/Md3.qml` + 12 件组件**；`AGENTS.md §9` = 全局 UI 红线。骨架唯一外壳 = viewer `qml/shell/MainWindow.qml`；竖横屏 `portrait=宽<高` ⇒ 竖屏走 `shell/BottomNav.qml`。

## 4 已交付 / 进行中（细节查 git log 与归档）
- 0.6.45/0.6.46 发版；OOBE 向导；二维码绑定；班级真源统一；RBAC 两批；MD3 全局统一+竖横屏（viewer `23df3f6`）。
- **CMD 窗口根治（2026-10-22 收官）**：`winlauncher.h` 两 helper（CREATE_NO_WINDOW）；云端两任务+guard/P2P 全改 wscript+vbs；agent 6 处 ffmpeg/ffplay 补齐；装机源头 `install-autostart.ps1` 同改防回退。ffmpeg 实弹未验。
- 语音对讲 P0 已修未发版（8 槽轮转+云端背压）；裸 PCM 300kbps/路是教室炸掉根因，Opus@16k≈1Mbps；不上 WebRTC。VoiceHub 本地联动（排期数据+广播站 UI 已通）；SMTC 控制通道已入（网易云不注册 SMTC，读只能靠窗口标题+WASAPI 峰值）。
- 待点单（只报清单）：permOk 越权/图标错配/URL 派生/Md3Btn enabled 自遮蔽/kind=banner|mini/安卓版本真源/0.6.45 prerelease/M3 看板/灰度选择器/VNC 收口(200KB/s 首帧+DXGI+泄漏)/门禁扩共享 ui/班级码目录页/XJK- 实测/班级改动未发版/骨架默认首页与旧壳下线。

## 5 踩坑实证（还会再踩）
- 抓图只用 `_shotpw.py`（DPI 感知 + `PrintWindow(PW_RENDERFULLCONTENT)`）；屏幕 DC BitBlt 在 Win11/DWM 全黑；判有效看 PIL 黑占比 <10%。
- 找主窗按类名过滤 MessageWindow/IME 再取最大（托盘消息窗 984x555 比竖屏主窗大）。
- **EnumWindows 不过滤可见性** ⇒ 判"用户可见控制台窗"必须加 `IsWindowVisible`（vbs 壳拉起的是隐藏窗）。
- `build/viewer-run.log` 跨运行追加 ⇒ 轮询带字节 offset。被控端日志重定向是 GBK。
- 时间戳事件时落、显示时现算。dedup 快照剔时间戳字段。QTimer 建了不 start=只有首查。
- QML 属性自遮蔽（子组件声明 ctx 后宿主注入用 panelCtx）。qmllint 假安全感（传不存在属性整页崩而 0 Error）⇒ 运行时探针才是出口。
- 并行线 hunk 择收：临时 `GIT_INDEX_FILE` + plumbing，`git show --stat` 双验。
