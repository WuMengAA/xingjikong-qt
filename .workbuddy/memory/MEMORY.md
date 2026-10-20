# Stelarith-xingjikong-qt · 项目速查
> 细节见 `.workbuddy/memory/YYYY-MM-DD.md`（append-only）。**说明文档统一写 README.md，不再新建 docs/ 独立文档**（用户 2026-10-19 定）。精简于 2026-10-20。

## 0 铁律
- 裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。属实 bug 直接修并同轮闭环。
- **只改三工程** `{Stelarith-cloud,Stelarith-control-qt,Stelarith-viewer-qt}`；`D:\Stelarith\` 同名三份是 bare 远端，只 push。
- **顺带发现 ≠ 点名重点**：只报清单等点单；不许在烂地基上叠补丁。取证避让：不跑被控端/不动桌面；「复制」只 cp 不 mv。红线可越（已授权）但先备份+汇报。
- **避开工作区运行程序**：部署/调试一律到生产安装目录，工作区只编译。

## 1 部署 / 发版
- 生产真源=`C:\Program Files\Stelarith\`（AgentGuard 每 10min 按完整路径巡检；互斥体 `Global\StelarithAgentQt_Singleton`）⇒ **换生产=换 exe + `qml/` 整目录**（QML 从 exe 旁读盘，混版⇒类型找不到）。**SOP**：Disable+Stop 任务→杀进程→改名备份→拷件→sha256 核对→Start。
- ⚠️⚠️ **换 qml 前必须验「每个 import 的模块目录非空」+「模块依赖的 Qt6*.dll 在 app 目录/System32/PATH」**：2026-10-20 血案——`build/qml/QtQuick/Controls` 是空目录且缺 `Qt6QuickControls2.dll`，而共享 Md3Btn 引 Controls、被 5 个被控端页面 import ⇒ 面板一开即 `qtquickcontrols2plugin.dll 找不到指定的模块`（编译零症状）。**目录在 ≠ 有实现**。
- 构建期兜底（2026-10-20 起）：control 有 `windeployqt --qmldir`（三处源码 QML 目录）+ `scripts/check-qml-modules-deployed.cmake` 模块断言；viewer 有 `windeployqt` + OpenSSL 拷贝 POST_BUILD。**windeployqt 不传 `--qmldir` 只部署核心模块**（靠扫 QML import 决定）。
- **云端生产=工作区份** `Stelarith-cloud/`（任务 StelarithCloud/StelarithCloudWatchdog；`ota.json` 每请求现读，publish 即生效）。
- ⚠️ C 盘长期告急：打安装包/整树 cp 会失败；只留小备份。
- 版本真源=云端 `ota.json`（**UTF-8 无 BOM**）；`bump-version.mjs`（show/bump/set/release）管 4 副本并回读校验；**stable 先 bump 再发**（bump→重编→出包→publish-release）。
- **最新版本=0.6.46**（2026-10-20：双端 stable + GitHub `release-0.6.46` + 本机生产；归档 `加减归档-0.6.46-2026-10-20.md`）。**0.6.45 安装包缺 Controls 运行库，勿再用**（release 保留作历史）。
- ⚠️ **安卓版本号「改了源文件也可能不生效」**：`android/AndroidManifest.xml` 写了 0.6.46/646，但 `aapt2 dump badging` 实测产物仍是 `org.qtproject.example.viewer_qt` / `1` / `1.0`（androiddeployqt 回退了 Qt 默认模板）⇒ 改完**必须 badging 复核**；修法待点单。
- ⚠️ 同一工作树有并行工作线：**提交前先 `git log`**；装机后按字节核对，不看版本号。
- 443/wss：`control.245959623.xyz/ws/agent|viewer`；cloudflared 回源 8788；本地验证一律 `127.0.0.1:8788`。
- GitHub：`curl`/`Invoke-WebRequest` 到 github 恒 000 ⇒ 加 `--ssl-no-revoke` 可用；PAT 从 `git credential fill` 取（用完即删脚本，不落盘）。

## 2 编译 / 自检
- Qt 6.12.0 `D:/QTqwq/6.12.0/msvc2022_64`；`control/scripts/_build_agent.py`、`viewer/scripts/_build_viewer.py`（包装 ninja + MSVC env，-j4）。改 CMakeLists 后 ninja 会自 reconfigure。
- ⚠️ MSVC C1060⇒`-j1`。`file(GLOB)` 不自动重查（`CONFIGURE_DEPENDS` 除外）。
- 离屏自检：`STE_ALLOW_MULTI=1 ... -platform offscreen`；判定看**退出码**（10 过/20 败/30 兜底），文字加 `QT_ASSUME_STDERR_HAS_CONSOLE=1`。
- AUTOMOC：被 include 的头会单独成 TU；含 Q_OBJECT 的头必须显式列入 add_executable；`LNK1104`=实例占用产物。
- ⚠️ VNC 线构建用隔离目录 `build-vnc/`；部署前 grep CMakeCache + 二进制 strings 双验签。
- 出包：agent `./build-agent-installer.sh`（ISCC，包 `build\qml\*` + `build\Qt6*.dll`）；viewer `makensis scripts/make-installer.nsi`（translations/position 缺件警告无害）；APK `scripts/make-apk.ps1`（需 `-ExecutionPolicy Bypass`，失败可 `-SkipConfigure` 续跑）。**Inno 包不能用 7z 列内容**。

## 3 仓库 / UI / 探针
- github `WuMengAA/xingjikong-qt` 五分支；根仓即 meta（gitlink，无 remote）。
- QML 纪律：字号 12/13/14/16(+20)；**锚只锚 parent**；不用 Qt Button/TextField；令牌实例 id 用 `tok`；`Flickable` 显式 `contentWidth: width`；符号字形教室机 tofu⇒`SvgIcon` 或汉字。NSIS `.nsi` UTF-8 带 BOM。
- 提交路径限定（**永不 `-A`**）。探针 `scripts/_*.py`/`_*.mjs` 不入库（白名单 `_build_agent.py`、`_voicehub_fake_server.mjs`；构建脚本 `check-qml-modules-deployed.cmake` 入库）。
- 门禁（`shared-qml/scripts/`）：ui-contract/action-manifest/island-metrics/manifest-scope/module-registry/agent-qml-deps/island-forms/island-tasks/plugin-island-gate/diff-island-tree。
  ⚠️ test-agent-qml-deps 只扫 `control-qt/qml/shell`，**扫不到共享 ui 组件的新增 import**（Md3Btn 加 Controls 时漏网）。

## 4 已交付（细节查 git log / 归档）
- 早期（细节查 git log）：U1–U5+D 档（共享 ui 基座/14 面板/44 动作单一真源/被控端 5 页）、内置模块层+U盘、VoiceHub、多界面与形态五档/G2、通知记录与时间、ClassIsland 插件 v1.5.0.0、CIMS 课表供给（必须 node:http）。
- **加强 OTA 三批次**（cloud `ccea463` / viewer `0d19abc` / control `fd7fcba`）：管理台可发 rc/alpha+灰度 channel（`manifestFileFor` 单一真源）+ 发布后端点自验；管理端检查带 channel + 周期自动检查（`STE_VIEWER_UPDATE_CHECK_SEC` 默认 4h）+ mandatory 气泡（只提示不装）；被控端 `otaReportHealth`（变化才发、lastAt 不参与比对）+ guard 删 gray 硬编码。归档 `加减归档-加强ota和维护功能-2026-10-20.md`。
- **0.6.45 全链发版**（10 个在途提交入库、双端+APK、GitHub `release-0.6.45`、本机生产切换）。归档 `加减归档-0.6.45-2026-10-20.md`。
- **0.6.46 构建链修复**（control `9f44ee3` / viewer `a5e9775`）：windeployqt + QML 模块断言（治面板 Controls 缺件）、OpenSSL 拷贝规则恢复、channel.txt 切 stable、双端重发 + GitHub `release-0.6.46`。

## 5 待点单（只报清单，等点头）
1. `permOk("admin")` 恒真越权。2. `check-circle` 图标名错配。3. viewerbackend 5 处 URL 派生未收敛。4. 站点 `/api/device/activate` 不回 classCode。5. `Md3Btn.qml:11` 自定义 `enabled` 盖掉 `Item.enabled`。6. 管理端下发 `kind=banner|mini` 入口。7. **安卓打包版本真源**：APK 仍走 Qt 默认模板（package/versionCode=1）⇒ 需补 android 模板结构或 build.gradle 覆盖，并 badging 复核。8. `release-0.6.45`（缺件包）是否打 prerelease/加标记。9. **M3 设备健康看板**：ota_health 已进 events.log，但无按设备查健康的面板。10. admin.html 灰度通道选择器浏览器实点。11. 远控 VNC 收口剩余项：首帧免配额/配额上调（200KB/s 吞全屏首帧）、DXGI 输出选择加固（GameViewer 虚拟屏横跳）、会话泄漏收口。12. test-agent-qml-deps 门禁扩到共享 ui 组件。

## 6 踩坑实证（按"还会再踩"排序）
- 其他小条目（elide 用 TextMetrics.advanceWidth、setProperty 后绑定异步、WM_DEVICECHANGE 走 Qt 顶层窗口、WER 错误偏移=RVA）见 2026-10-1x 日志。
- ⚠️⚠️ **换 qml/装包前的缺件判据**：模块目录非空 + 该模块依赖的 `Qt6*.dll` 可达（app 目录→System32→PATH）。2026-10-20：#Controls 空目录 + 缺 Qt6QuickControls2.dll ⇒ 被控端 5 个页面全挂，编译/主日志零症状。
- ⚠️⚠️ **`windeployqt` 靠源码 import 决定部署范围**：不传 `--qmldir` 就只给核心模块；共享组件新增一个 import = 给所有消费端加隐性部署依赖。
- ⚠️⚠️ **「变化才上报」的 dedup 快照必须剔掉时间戳字段**：`lastAt` 每次注册回执都刷 now ⇒ 每重连上报一条「没变化」。
- ⚠️⚠️ **QTimer 建了不 start = 只有首查**（编译无感）；凡新建定时器同轮确认 start 调用点。
- ⚠️⚠️ **QML 属性自遮蔽**：子组件声明 `ctx` 后，宿主 `Child { ctx: ctx }` 全变自引用 ⇒ theme/backend 恒 undefined。宿主注入上下文的属性名必须避开 `ctx`（用 `panelCtx`），Dialog 侧赋值显式 `dlgId.ctx`。Popup 内容在 `contentItem`。
- ⚠️⚠️ **被控端日志重定向到文件是 GBK**（Qt local 8-bit）：读文件 `decode('gbk','replace')`，中文关键词按 ASCII 筛。
- ⚠️ **并行线 hunk 级择收**：`GIT_INDEX_FILE=<tmp> git read-tree HEAD + apply --cached + write-tree + commit-tree + update-ref`，真实索引不动；plumbing 提交 reflog 是空消息，必须 `git show --stat` + 内容 grep 双验。
- ⚠️ **bash heredoc 后的换行命令不受 `&&` 链保护**；且命令里出现敏感词（如 powershell）会被安全层拦。
- ⚠️⚠️ **时间戳在事件发生时落，派生文本显示时现算**，绝不持久化。
- ⚠️⚠️ **截图/几何取证必须声明 DPI 感知**（`SetProcessDpiAwareness(2)`）。
- ⚠️⚠️ **qmllint 给假安全感**——唯一可靠出口是运行时探针；被控端无 Controls/Shapes 时 G2 只能 Canvas（2026-10-20 起 Controls 已随部署带上，仍需按需确认）。
- ⚠️⚠️ **`#ifdef Q_OS_WIN` 在纯 Win32 文件里恒假**（未 include Qt 头）⇒ 纯 Win32 用 `_WIN32`。
- ⚠️ **心跳必须保证带宽**：帧洪流灌满上行⇒心跳迟到⇒云端 30s 踢线；帧过同一条背压闸，帧可丢心跳不可丢。
- ⚠️ **QTcpSocket 每次 close+deleteLater 会崩**⇒长生命周期桥接 socket 改终身单例（cleanup 只 abort()）。
- ⚠️ **libvncserver win32 threads 无 join**⇒自管 QThread 循环 `rfbProcessEvents`，stop 先 wait 再销毁。
- ⚠️ **cl 链接步绝不带 /TP**；Git Bash 把 `/EHsc` 当路径⇒cl 参数走 Python subprocess（list）。
- ⚠️ **状态文件「成功不清错误」=长期误导**：`lastResult=ok` + `failCount=0` 才是真相，同源写。
- ⚠️ **云端"逐帧事件"是事件表杀手**：帧路径只累计，会话边界/15s 窗口聚合入账。
- ⚠️ 历史陷阱：WebRTC/libdatachannel 已彻底移除（2026-10-18 清道夫），勿复辟。

## 7 远控 VNC 线（已随 0.6.45 上线；细节查 2026-10-19/20 日志与 `加减归档-0.6.45-*.md`）
- 架构：被控端 libvncserver 只绑 loopback，RFB 字节走主 WS `vnc-data` 二进制帧经云端按 uid 中继；viewer 回环 + QThread 跑 rfbInitClient。720p + HexTile；VNC up 帧过背压闸（200KB/s 配额，`main.cpp:7337`）。
- 已落地：DXGI 抓屏（GDI 回落、首帧黑跳过、**逐脏区拷贝禁止整屏 memcpy**）、会话泄漏看门狗、岛抑制模式。
- **待点单**：200KB/s 配额丢全屏首帧 ⇒ viewer 全空白（首帧 275KB，libvncserver 不重发）；GameViewer 虚拟屏搅乱 DXGI 输出选择；viewer 端 VNC 客户端（#345-#348）未开工。
- 库级铁律（rfbGetScreen 自己 malloc framebuffer / GotBitmap 三件套 all-or-none / 失败勿 double free / 密码走 GetPassword）详见 2026-10-19 日志。

## 8 闪传线（U1 云端侧已落地：UDP `eb05080` + TCP `5d61c8f`；TCP=公网主路，细节查 2026-10-20 日志）
- `flash-core.js` 共享内核 + `udp-relay.js`(8789) / `tcp-relay.js`(8790)；密钥走 WS `udp-session`/`tcp-session`。帧 ChaCha20-Poly1305，nonce=session||seq，**中继自发帧 seq=2^63+**（防 nonce 重用）；TCP 严格单调 + 背压不丢帧 + 一端断即收口 + fail-closed。
- ⚠️ 公网还差人工步骤：CF Zero Trust 给隧道加 Public Hostname（`flash.245959623.xyz`，Type=TCP，`tcp://127.0.0.1:8790`）——ingress 在仪表盘，本地无凭据。
- 待做：Qt 两端 C++ 闪传客户端、U2 延迟/统计、U3 远控 RFB over 闪传（解首帧全白，用户验收靶点）、U4 文件传输+断线续传。
