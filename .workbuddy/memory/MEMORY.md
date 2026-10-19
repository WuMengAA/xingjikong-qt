# Stelarith-xingjikong-qt · 项目速查
> 细节见 `.workbuddy/memory/YYYY-MM-DD.md`（append-only）。**说明文档统一写 README.md，不再新建 docs/ 独立文档**（用户 2026-10-19 定）。精简于 2026-10-19。

## 0 铁律
- 裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。属实 bug 直接修并同轮闭环。
- **只改三工程** `{Stelarith-cloud,Stelarith-control-qt,Stelarith-viewer-qt}`（2026-10-20 云端改名去 ws 后缀；`D:\Stelarith\` 同名三份是 bare 远端，只 push）。
- **顺带发现 ≠ 点名重点**：只报清单等点单；不许在烂地基上叠补丁。取证避让：不跑被控端/不动桌面；「复制」只 cp 不 mv。红线可越（已授权）但先备份+汇报。
- **避开工作区运行程序**：部署/调试一律到生产安装目录，工作区只编译。

## 1 部署 / 发版
- 生产真源=`C:\Program Files\Stelarith\`（AgentGuard 每 10min 按完整路径巡检；互斥体 `Global\StelarithAgentQt_Singleton`）⇒ **换生产=换 exe + `qml/` 整目录**（QML 从 exe 旁读盘，混版⇒`UiRegistry is not a type`）。线上 agent 0.6.44 / viewer 0.6.37。
- **云端生产=工作区份** `D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-cloud\`（计划任务 StelarithCloud/StelarithCloudWatchdog 直指；2026-10-20 从「外部份」切回并改名，子仓 README 旧说法已更正）。
- ⚠️ C 盘长期告急（剩 10–850MB）：打安装包/整树 cp 会失败；只留小备份。
- 版本真源=云端 `ota.json`（**UTF-8 无 BOM**）；`bump-version.mjs` 改 4 副本，**stable 必须"先 bump 再发"**（bump→重编→build-agent-installer.sh→publish-release.mjs）；bump 会同时改 viewer 2 处。
- **最新版本=0.6.45**（用户 2026-10-19 指定；当前 4 副本=0.6.44 一致，发版即 `bump-version.mjs bump`）。
- ⚠️ **安卓 manifest 不在 bump 脚本覆盖内**：`viewer-qt/android/AndroidManifest.xml` 停在 `versionCode=1`/`versionName=0.6.25`（10-11 老值）。发安卓必须手改 versionName→当期版本 + versionCode→**必须 >1**（已装包全是 1，不涨必拒装；推荐 semver 映射 0.6.45→645）。bump 脚本纳入 manifest 待点单。
- ⚠️ 同一工作树有并行工作线：**提交前先 `git log`**；装机后按字节核对，不看版本号。
- 443/wss：`control.245959623.xyz/ws/agent|viewer`；cloudflared 回源 8788；本地验证一律 `127.0.0.1:8788`。

## 2 编译 / 自检
- Qt 6.12.0 `D:/QTqwq/6.12.0/msvc2022_64`；被控端 `scripts/_build_agent.py`（viewer `_build_viewer.py`，-j4）。
- ⚠️ MSVC C1060⇒`-j1`。改 CMakeLists 源列后手动 configure；`file(GLOB)` 不自动重查。
- 离屏自检：`STE_ALLOW_MULTI=1 ... -platform offscreen`（无 CJK⇒加 `msyh.ttc`）；判定看**退出码**（10 过/20 败/30 兜底），文字加 `QT_ASSUME_STDERR_HAS_CONSOLE=1`。
- AUTOMOC：被 include 的头会单独成 TU；含 Q_OBJECT 的头必须显式列入 add_executable；`LNK1104`=隔离实例占用产物。
- ⚠️ VNC 线构建用隔离目录 `build-vnc/`（防并发 configure 撞车）；部署前 grep CMakeCache + 二进制 strings 双验签。

## 3 仓库 / UI / 探针
- github `WuMengAA/xingjikong-qt` 四分支；curl 到 github 恒 000，push 用 `git -c credential.helper=`。根仓即 meta（gitlink，无 remote）。
- QML 纪律：字号 12/13/14/16(+20)；**锚只锚 parent**；不用 Qt Button/TextField；令牌实例 id 用 `tok`；`Flickable` 显式 `contentWidth: width`；符号字形教室机 tofu⇒`SvgIcon` 或汉字。NSIS `.nsi` UTF-8 带 BOM。
- 提交路径限定（**永不 `-A`**）。探针 `scripts/_*.py`/`_*.mjs` 不入库（白名单 `_build_agent.py`、`_voicehub_fake_server.mjs`）。
- 门禁（`shared-qml/scripts/`）：test-ui-contract(32) test-action-manifest(23) test-island-metrics(11) test-manifest-scope(15) test-module-registry(10) test-agent-qml-deps(12) test-island-forms(21) diff-island-tree。

## 4 已交付（细节查 git log）
U1–U5+D 档（共享 ui 基座/14 面板声明化/44 动作单一真源/被控端 5 页）；内置模块层+U盘（`b851e67`）；VoiceHub（`92be4c6`）；多界面/形态五档/G2 圆角（`f12f79b`/`3a82ec5`/`87afff8`）；通知记录与时间（`c2ba193`+`dd34d80`+`983e30f`）；ClassIsland 插件线 v1.5.0.0（连被控端 `ws://127.0.0.1:26789` 认领 4 action）；CIMS 课表供给（Stelarith-cloud `c6ebdac`，必须 node:http 不能 fetch）。

## 5 待点单（只报清单，等点头）
1. `permOk("admin")` 恒真越权。2. `check-circle` 图标名错配。3. viewerbackend 5 处 URL 派生未收敛。4. 站点 `/api/device/activate` 不回 classCode。5. `Md3Btn.qml:11` 自定义 `enabled` 盖掉 `Item.enabled`。6. 管理端下发 `kind=banner|mini` 入口。7. ~~stable 0.6.44 是否 bump≥0.6.45~~ **已定：0.6.45**（用户 2026-10-19）——G2/通知记录/通知时间三批 + #368/#369/#370 随 0.6.45 发；流程 bump→重编→installer→publish-release.mjs；安卓另手改 manifest（name 0.6.45 + code>1，推荐 645）→ make-apk.ps1 → 发 `WuMengAA/xingjikong-qt` release（tag 待给）。8. 远控 VNC 收口：DXGI 化已部署生产（sha256 `25bffbf4…`）；14:58 实测（id=4730）抓到位——**「没有画面」根因=200KB/s 配额丢全屏首帧**（`main.cpp:7337`，275KB 首帧>204800 ⇒ 有 `桥出向` 无 `up 已发`，viewer 无基础图全空白；丢帧在 WS lambda，libvncserver 不重发）；次要干扰=GameViewer 虚拟屏横跳拓扑；**会话泄漏**（viewer 离开不发 stop ⇒ 岛长期冻结）。待点单：首帧免配额/配额上调、DXGI 输出选择加固、会话泄漏收口、关 id=4730。两仓 git 提交 + bump 发版仍等他线。

## 6 踩坑实证（按"还会再踩"排序）
- ⚠️ **云端目录改名先停服务**：node 占着日志/DB 句柄，`git mv` 必 Permission denied；时序=Disable 看门狗→Stop 任务→改名→改任务动作→Enable+Start（`%~dp0` 自定位的 .cmd 搬家零影响；看门狗手动触发 exit 0=no-op 验证）。
- ⚠️⚠️ **QML 属性自遮蔽（2026-10-20 实证，探针抓到的生产级真 bug）**：Dialog 里 `Child { theme: ctx.window.th; backend: ctx.window.backendRef; ctx: ctx }`——右值裸 `ctx` 在**子对象作用域优先解析到子组件自己的属性**。子组件一旦也声明 `ctx`，三行全变自引用 ⇒ theme/backend/ctx 恒 undefined，面板能开但全死。规则：**宿主注入上下文的子组件，属性名必须避开 `ctx`（本批改名 `panelCtx`），且 Dialog 侧赋值一律显式 `dlgId.ctx` 限定**。Popup 无 `children`（内容在 `contentItem`）——探针遍历对象树必须兼顾。
- ⚠️⚠️ **时间戳在「事件发生」时落，不在「消费」时算**；派生时间文本绝不持久化，显示时现算。
- ⚠️⚠️ **截图/几何取证必须声明 DPI 感知**（`SetProcessDpiAwareness(2)`）否则虚拟化坐标。
- ⚠️⚠️ **qmllint 给假安全感**——唯一可靠出口是运行时探针。被控端无 QtQuick.Controls/Shapes⇒G2 只能 Canvas。
- ⚠️⚠️ **`#ifdef Q_OS_WIN` 在纯 Win32 文件里是陷阱**：不 include Qt 头⇒Q_OS_WIN 恒假⇒静默编占位实现。纯 Win32 用 `_WIN32`。
- ⚠️ **心跳必须保证带宽**：帧洪流灌满上行⇒心跳迟到 28s+⇒云端 30s 踢线。帧必须过同一条背压闸（积压>1MB 丢批+每秒配额，帧可丢心跳不可丢）。
- ⚠️ **QTcpSocket 每次 close+deleteLater 会崩**⇒长生命周期桥接 socket 改终身单例（cleanup 只 abort()）。
- ⚠️ **libvncserver win32 threads 无 join**：`rfbRunEventLoop(TRUE)` 每客户端 spawn 线程，shutdown 时 UAF⇒自管 QThread 循环 `rfbProcessEvents`，stop 先 wait 再销毁。
- ⚠️ **崩溃取证不必等 WinDbg**：WER「错误偏移」对 x64 即 RVA，PE 导出表反解函数名。先装 WER LocalDumps 指到 D 盘。
- ⚠️ **cl 链接步绝不带 /TP**；Git Bash 把 `/EHsc` 当路径⇒cl 参数走 Python subprocess（list）。
- ⚠️ **状态文件「成功不清错误」=长期误导**：`lastResult=ok`+`failCount=0` 才是真相。同一条状态必须同源写。
- ⚠️ **云端"逐帧事件"是事件表杀手**：帧路径只累计，会话边界/15s 窗口聚合入账。
- ⚠️ 历史陷阱：WebRTC/libdatachannel 已彻底移除（2026-10-18 清道夫），勿复辟。
- 其他：带 `elide` 的 Text 用 `TextMetrics.advanceWidth`；`setProperty` 后 QML 绑定异步重算（同帧读得 `<invalid>`，注入留 ≥500ms）；WM_DEVICECHANGE 要 SendMessage 到 Qt 顶层窗口；QObject 子对象按添加顺序析构。

## 7 远控 VNC 线（in-agent，进行中）
- **架构**：被控端 `VncServer`（libvncserver）只绑 `127.0.0.1` loopback；RFB 字节走**主 WS `vnc-data` 二进制帧**经云端中继（`vncDataToAgent/Viewer` 按 uid 双向，谁订阅投谁）；viewer `VncClient` 回环 `127.0.0.1:0`+QTcpServer+QThread 跑 rfbInitClient。
- **编码/帧率**：720p 内接 + HexTile（`encodingsString="hextile copyrect raw"`，hextile 必须第一）；fps 链路 viewer→`VncServer::start(0,want)`→抓屏间隔 clamp 33..1000ms；VNC up 帧过背压闸（200KB/s 配额+<1KB 豁免，`main.cpp:7337`）；远控期间停发 JPEG。
- ⚠️⚠️ **本机装着 GameViewer（网易远程）虚拟显示器**（`GameViewer Virtual Display Adapter` + GameViewerService 等 4 进程常驻）：其虚拟屏激活/退出会**搅乱显示拓扑**——DXGI「含原点输出」选择随之翻版（实测 dup 尺寸 1280x720↔2560x1440↔1024x768 横跳，每次横跳=ACCESS_LOST+丢帧+等首帧）。远控测试前建议先退 GameViewer；根治=输出选择改「取最大模式/与 GetSystemMetrics 对齐」。
- ⚠️⚠️ **200KB/s VNC 配额会吞掉全屏首帧 ⇒ viewer 全空白**（2026-10-19 实测根因）：1280x720 首帧 275KB>200KB 配额被丢（`main.cpp:7337`），且丢帧发生在 bridge 已读走本地 TCP 字节之后，**libvncserver 认为已发送不会重发** ⇒ viewer 永远拿不到基础图。修法：首帧免配额或配额上调（心跳已稳定 10s）。
- ⚠️ **远控会话会泄漏**：viewer 直接离开（不发 remote_control_stop）⇒ agent 侧 g_remoteControl 永真、岛冻结、VNC server 空转。需 bridge 空闲/viewer 断开自动停。
- **库级铁律**：①`rfbGetScreen` 不分配 frameBuffer⇒自己 malloc+`rfbNewFramebuffer`（BGRX 字段须在其后设；cleanup 会 free）；②libvncclient Raw/ultra 都走 `GotBitmap`⇒GotBitmap/GotFillRect/GotCopyRect 三件套 all-or-none，缺了恒黑；③客户端主循环 `WaitForMessage`+`HandleRFBServerMessage`；④`rfbInitClient` 失败库已自行释放，勿 double free；⑤密码走 `GetPassword` 回调；⑥`SendKeyEvent(client,key,down)` 顺序；⑦无 zlib 构建下每帧 FU 第一个像素 rect 的 buf 恒带 12B 前缀（左上 3 像素脏，待深修）。
- **✅ 新客户端安全（源码实证）**：libvncclient 握手后首请求=非增量全屏（vncviewer.c:467）；服务端非增量请求把请求区 union 进 `cl->modifiedRegion`（rfbserver.c:2719）⇒ 脏区模式下新客户端仍拿全屏首帧。
- **✅ DXGI 化（2026-10-19 已部署生产，sha256 字节核对一致）**：`screen_capture.h`=Grabber 基类+GdiGrabber+DxgiGrabber 门面（init 优先 DXGI 失败回落 GDI，`backend()` 暴露实际后端）；DxgiGrabber=staging B8G8R8A8 直拷 BGRX、非阻塞 AcquireNextFrame（无新帧⇒不推）、脏区+MoveRects 保守超集映射缩放到输出、ACCESS_LOST 限流 1s 重建、输出选 DesktopCoordinates 含原点者；vnc_server worker **逐脏区拷贝+逐块 mark**（⚠️ DXGI 后禁止整屏 memcpy——脏区外是旧内容会抹好数据）。真机探针：backend=dxgi、首帧 100% 脏区、与 GDI 逐点 49/49 一致、静态画面 10/10 跳过 0.15ms/帧。
- ⚠️⚠️ **DXGI 首帧恒黑（2026-10-19 真机实证，库行为）**：DuplicateOutput 后**第一个帧必全黑**（LastPresentTime=0，合成器未 present），第 2 帧起才有内容（本机 present 约 1s/次，岛时钟驱动）。对策：DxgiGrabber 黑帧跳过+2s 预算（预算耗尽仍黑⇒桌面真黑，如实交付）；vnc_server 起服 memset 帧缓冲为黑（跳过期间连上的客户端看到黑不是垃圾）。⚠️ 教训：老 dxgi_probe 只数帧没验内容⇒漏过此问题；**抓屏类探针必须做内容差分（DXGI vs GDI 同时刻逐点比）**。
- **✅ 换生产标准时序（2026-10-19 实证）**：`Disable-ScheduledTask StelarithAgentGuard`+`StelarithAgentQt` → Stop-Process → 重命名备份 → cp 新 exe → sha256 核对 → `Enable`+`Start-ScheduledTask`。⚠️ 只 Stop-ScheduledTask 不 Disable 会被任务自动重拉（2s 内新 PID）。⚠️ 生产 qml/ 是 10-17 旧快照而 exe 是 10-18 构建（上轮只换 exe 的既有混版，UiRegistry 等契约文件一致故无感）——本批只动 C++ 与 QML 契约零交集，继续只换 exe；要修混版得单独点单。

## 8 闪传线（2026-10-20 起 · U1 云端侧已落地：UDP eb05080 + TCP 5d61c8f；用户裁决「那就tcp」⇒ TCP=公网主路）
- 架构：`src/flash-core.js` 共享内核（帧编解码+AEAD+seq 空间纪律+中继自发帧构造+HELLO/PING 校验+会话存储，UDP/TCP 两中继一份代码两处审计）；`udp-relay.js`（dgram 8789，回环/局域网，重构为消费内核、行为逐字不变，31+15 测试当护栏）；`tcp-relay.js`（net 8790，公网主路）。密钥协商走 WS `udp-session`/`tcp-session`（open/close，权限=canAccess 同「看画面」，设备不在线直拒；opened 双端下发 {sessionId, key(base64 32B), tcp:{host,port}}；密钥只走已鉴权 WS，帧道不传长期秘密）。
- 帧（两代共用）：`SU`|ver|type|session(4)|seq(8)|sendTs(8)|len(2)|tag(16)|payload；AEAD=ChaCha20-Poly1305（RFC 7539，nonce=session||seq，AAD=整个帧头）；type：HELLO/HELLO_ACK/DATA/ACK/NACK/PARITY/PING/PONG/CLOSE。
- ⚠️⚠️ **seq 空间划分（nonce 重用=泄令牌铁律）**：端→中继帧 seq<2^63；中继自发帧（HELLO_ACK/PONG）seq=2^63+会话内计数，越界拒收。**UDP 中继不做 seq 单调闸门**（会挡 U2 选择重传的合法重传，重放防护在两端）；**TCP 中继做严格单调闸门**（重放即拒，兼守 nonce 不重用）。
- TCP 中继要点：流式分帧（每 socket 累积缓冲+按 len 切帧，半帧等下一 chunk、多帧合一 chunk 一次吃光）；⚠️ len 是 UInt16⇒65535 即字段上限，「len 越界即断」分支在协议封顶下不可达（防御性保留）；**背压不丢帧**（对端写满⇒pause()源 socket，对端 drain⇒resume()，pauses/resumes 进 stats——实测停读时转出 9 帧即暂停、续读后 96 帧一帧不丢）；**一端断即收口**（TCP socket 断⇒closeSession(reason=peer-tcp-gone) 双端 WS 收 closed——防跨连接 nonce 重用窗口）；fail-closed（`TCP_RELAY_TOKEN` 空⇒拒绝启动；HELLO timingSafeEqual；未 HELLO 只许发 HELLO，15s 宽限；连接封顶 256；会话空闲 60s 收口）。
- 令牌红线：CLOUD_WS_TOKEN/CLOUD_VIEWER_TOKEN 不动；新增 `TCP_RELAY_TOKEN`（与 UDP 独立，生产 .env 已配 24B 随机）。
- 测试：`cd Stelarith-cloud && npm run test:udp`（31）+`test:udp-session`（15）+`test:tcp`（41）+`test:tcp-session`（19，隔离实例+临时 DB 三件套）。⚠️ 8096/8097 被 cims-eval 占着；`curl -s -o /dev/null` 对 403 也返 0，就绪判断须看 HTTP code+进程存活；v1 注册回包=`registered`。
- 拓扑硬约束：cloudflared 只转 TCP ⇒ 公网 UDP 不可达。公网主路=TCP 8790；UDP 8789=回环/局域网直连。⚠️ **公网还差人工步骤**：CF Zero Trust 仪表盘给隧道加 Public Hostname（`flash.245959623.xyz`，Type=TCP，URL `tcp://127.0.0.1:8790`）——本机跑的是令牌型远程管理隧道，ingress 在仪表盘、本地无 config/cert.pem/API 凭据；`*.245959623.xyz` 泛解析已存在。
- 重启策略：令牌已入生产 .env 但**不单独重启云端**——等 Qt 客户端就绪随 U3 一次重启做多件事（当前生产进程跑旧码无 tcp-session，无兼容问题）。
- 待做：Qt 两端 C++ 闪传客户端（纯 C++ ChaCha20-Poly1305 ~300 行——两工程 CMakeLists 均未链 OpenSSL；QTcpSocket 回环/局域网 + QSslSocket 公网走 `flash.245959623.xyz:443`；WS `tcp-session` 协商对接 + 对 node crypto 互通验证）；U2 延迟/统计层（PING/PONG RTT+sendTs+SRTT/RTTVAR/RTO 钳制+云端每跳打点+自适应抖动缓冲+统计上报；ARQ/FEC/AIMD 仅 UDP 中继需要）；U3 远控 RFB over 闪传 TCP 主路（绕开 200KB/s 背压闸解首帧全白——**用户痛点验收靶点**，协商失败回落 WS）；U4 文件传输 over 闪传+file-relay 60s 缓存+断线续传+应急中转 HTTP+Psql（pg+DATABASE_URL 双写降级回落 node:sqlite）。
