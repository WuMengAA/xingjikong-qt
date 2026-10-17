# Stelarith-xingjikong-qt · 项目笔记（速查）
> 过程细节见 `.workbuddy/memory/YYYY-MM-DD.md`（append-only）与根 `docs/*.md`（归档）。
> 本文件只留**仍会影响下一步动作**的事实。压缩于 2026-10-17（19.5K → ~7K）。

## 0 铁律
- 总纲=skill `stelarith-project-overview`；裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。
- 重要修复同轮闭环：属实 bug 直接修；重启/发布/出包同轮做；收尾逐条对账。
- **只改三工程** `{cloud-ws,control-qt,viewer-qt}`；`D:\Stelarith\` 下同名三份是 **bare 远端**（remote `outer`，只 push 不手改）。
- **顺带发现 ≠ 点名重点**：只报清单、等点单；不许在烂地基上叠补丁。
- 取证避让：不跑被控端/不动桌面；「复制」只 cp 不 mv。
- **红线可越**（用户授权）：停生产/换生产 exe 在验证需要时可做，但**先备份 + 汇报**。

## 1 部署 / 发版（最容易错）
- ⚠️ **本机生产真源＝`C:\Program Files\Stelarith\`**，不是 build 产物：计划任务 `StelarithAgentGuard` 每 10 分钟检查进程名，
  没了就拉起安装版（`StelarithAgentQt` 登录自启同路径）。⇒ **换生产 = 换安装目录 exe + qml 整目录**。
- ⚠️ **换 exe 必须连 `qml/` 一起换**（QML 运行期从 exe 旁读盘；混版 ⇒ `UiRegistry is not a type` 之类，fail-loud 在 agent-qt.log）。
- **线上现状（2026-10-17 11:00）**：生产安装目录＝**0.6.41 + 形态多样化那笔 exe/QML**（并行线装机 0.6.41，
  我随后换成含 Banner/Mini 的构建；备份 `stelarith-agent-qt.exe.bak-pre-form` + `_appqml-bak-pre-form/`）。
  云端 ota.json 仍在 0.6.40/0.6.41 这条线上（同版本不会触发自更新）⇒ **要固化必须自己 bump+出包**，
  否则并行线一发新版本（版本号高于 0.6.41）就会把这版覆盖回去。
- ⚠️ **C 盘长期告急**（120G 用满，剩余 10–850MB 徘徊）：**打安装包 / 整树 cp 会失败**（本次备份被卡断）。
  安装目录只保留应用自有 QML 小备份 `_appqml-bak-pre-*`（island/shell/components），**别再整树 cp**。
- 443/wss：`control.245959623.xyz/ws/agent`(被控)、`/ws/viewer`(管理)；cloudflared 回源本机 8788。
  令牌 `CLOUD_WS_TOKEN`/`CLOUD_VIEWER_TOKEN`/`CLOUD_VIEWER_SECRET`。本地验证一律 `127.0.0.1:8788`。
- 版本真源＝云端 `ota.json`（**UTF-8 无 BOM，带 BOM ⇒ latest:null**；不入库）。`bump-version.mjs` 改 4 副本，**出包前先 bump**。
  发布 `publish-release.mjs publish --product agent|viewer --version X --file <包>`；验证 `curl 127.0.0.1:8788/api/public/ota?product=X[&current=旧]`。
  `ota.json.url` 同时是装机卡链接 ⇒ **只挂全量件**。
- ⚠️ **同一工作树有并行工作线**（2026-10-17）：他线提交 `03bafbc`（H264 空转修复 + bump **0.6.41**）并已**装机到生产**
  （10:32 安装）；此前 `4c228e5` 还发过 gray 0.6.40。他线另有 `src/ldc_streamer.cpp` 在途。**提交前先 `git log`**，
  版本号/发布可能与对方抢；装机后要核对"装进去的包是否含我这边改动"（比字节，不看版本号）。

## 2 编译 / 自检
- Qt **6.12.0** `D:/QTqwq/6.12.0/msvc2022_64`；被控端入口 `control-qt/scripts/build-agent.sh`；`_build_viewer.py` 默认 -j4。
- ⚠️ MSVC C1060 ⇒ `-j1`。改 CMakeLists 源列后手动 `cmake -S . -B build`（**必须带脚本同款 -D**，CMakeCache 的
  `CMAKE_PREFIX_PATH` 曾被污染成 `D:/Qt/6.8.1`）。`file(GLOB)` 不自动重查 ⇒ 改 resources 文件名后必须重跑 configure。
- 离屏自检：`STE_ALLOW_MULTI=1 ... -platform offscreen`；offscreen 无 CJK 字体 ⇒ 加 `msyh.ttc`；
  Windows Qt 日志走 OutputDebugString ⇒ 判定看**退出码**（10=过/20=败/30=兜底超时），看文字加 `QT_ASSUME_STDERR_HAS_CONSOLE=1`。
- ⚠️ **探针的 QML 暂存来自 `build/qml`**（不是 shared-qml）⇒ 改共享岛后先
  `cp shared-qml/island/*.qml build/qml/island/` 再跑，否则测的是旧文件（症状"改了没反应"）。
- **AUTOMOC 四连坑**：① 被 include 的头会被单独编译成 TU ⇒ 头里不能引用 main.cpp 符号（用「快照+信号」，fill 用 friend 不能 static）；
  ② 头里不能 `#include "xxx.moc"`；③ 含 Q_OBJECT 的头**必须显式列入 add_executable**（否则 LNK2001）；
  ④ **moc 对 .cpp 输入生成的 TU 不含原文件** ⇒ QObject 探针/夹具类必须拆头。
  另：include 自家头**绝不能进匿名 namespace**（Qt 头被拽进域内 ⇒ C2872/C1003）。

## 3 仓库 / UI / 探针
- github `WuMengAA/xingjikong-qt` 四分支；curl 到 github 恒 000，push 用 `git -c credential.helper=`。
- 根仓库即 meta（三子工程 gitlink，无 .gitmodules）：子仓提交后 `git add <子目录>` 推进指针；根仓无 remote。
- QML 纪律：字号 12/13/14/16(+20)；**锚只锚 parent**；不用 Qt Button/TextField（被控端无 QtQuick Controls）；
  令牌实例 id 用 `tok` 不用 `md3`（子组件同名 ⇒ 自引用绑定环）；`Flickable` 必须显式 `contentWidth: width`。
- 符号字形（▶♪ emoji）教室机 tofu ⇒ 用 `SvgIcon` 或单字汉字。
- NSIS：`.nsi` UTF-8 带 BOM；提交路径限定（**永不 `-A`**）。
- 探针脚本：`scripts/_*.py`、`_*.mjs` 不入库（白名单 `!scripts/_build_agent.py`、`!_voicehub_fake_server.mjs`）；
  `_*.qml/_*.cpp` 入库。Bash cwd 不跨命令保持。
- 固化成测试的门禁（`Stelarith-shared-qml/scripts/`）：`test-ui-contract`(32断) / `test-action-manifest`(23) /
  `test-island-metrics`(11) / `test-manifest-scope`(15) / `test-module-registry`(10) / `test-agent-qml-deps`(12) / `diff-island-tree`。

## 4 已交付（按批次，均可从 git log 找回）
- U 系列 U1–U4：共享 ui 基座（Md3 + md3/12 件）、契约 `UiContract.js`+`UiRegistry`+`UiPanelHost`、14 面板声明化、44 动作单一真源。
- D 档：双绘常量统一 + 漂移门禁；IslandRoot 2207→2148（抽 IslandIdleFace/IslandActionRow）。
- U5：`ui/UiPageHost.qml`；viewer 三页抽两页（Main.qml 现 1914）；被控端 QML 控制面板 5 页全部就位（`2b30a7a`/`a8b843e`），真机 0 QML ERROR。
- 岛：A 档（管理端跑共享 IslandRoot）+ C 档（qml-patch 并岛 + MD5 门禁）；扩展组件 1–7 + 审批面板随 0.6.36 上线。
- **内置模块层**（`b851e67`）：`src/modules/`（modulehost/usbmodule/voicehubmodule）编译进 agent，与进程外 PluginHost 并列第二条扩展通道；
  manifest 同口径；`executeAction` **前置内置优先**；`ModuleAgentApi` 10 个 std::function 在 main.cpp 注入；
  AgentPluginsPage「内置模块」分区（快照 Q_PROPERTY + 信号）。门禁认 `registerModule(new X)` 与「先 new 后 registerModule(变量)」。
- **U盘模块**（`b851e67`）：WM_DEVICECHANGE + 岛 Centered 询问；只开根目录、绝不自动运行；考试静默。探针 19/19。
- **VoiceHub 联动**（`92be4c6`）：匿名只读 `GET {base}/api/music/broadcast` + SSE `{base}/api/music/websocket`。
  线上资料见下「坑」。SSE 主 + 30s 轮询兜底（`qBound` 下限 5s）+ 锚点本地 tick + staleSec 自愈（下限 10s）+ 开播提醒（考试静默）。
  控制三件（点歌/歌词/广播下发）**留桩默认关**（x-api-key 存放口径未定）。探针 32/32；岛 `vhRow` + 设置页卡。
- **灵动岛多界面**（`f12f79b` + 根仓）：展开态 `face`（0 概/1 媒/2 史/3 态）+ 面可用性 + `faceNow`（失效回 0）+ `facesOn` + `facePanelOn`；
  右上竖排面签（22×16 单字）；非概览面满盖面板三套版式。**概览面零改动**；动作按钮/搜索/投稿打开时不开面。
  探针 `--face 1/2/3` 全绿、`--expand` 仍 320、errors=0。
- **形态多样化**（`3a82ec5` + 根仓）：五档外框 `Capsule/Centered/Fullscreen/Banner/Mini`。
  **Banner** 顶部通栏（可用宽−48，≤1280；高 72/60；单行 elide —— 解决"胶囊上限 640 ⇒ 长文案折行涨高压课件"）；
  **Mini** 右下角角标（112 方牌 / 无描述 96 正圆，贴 availableGeometry 右下 24；点一下升格成胶囊展开，默认穿透）。
  `showWithActions` 有按钮时强制降级 Centered；`paintCompactForm()` 是 QML 缺失时的手绘退路。
  验证开关 `STE_QT_ISLAND_DEMO_FORM=banner|mini|mini-nodesc`（启动 2.5s 弹、12s 收、打几何+可用区+dpr）。
  门禁 `test-island-forms.mjs`（13 断言，三方一致性；已负向证伪）。真机三档几何与公式逐条吻合。

## 5 待点单（只报清单，等用户点头）
1. `permOk("admin")` 恒真越权（teacher 可锁屏；quickGuard 只保了快捷行）。
2. **IslandRoot 引用 `image://svgicon/check-circle` 但资源叫 `circle-check.svg`**（存量图标名错配，岛历史列表图标加载失败，日志持续刷）。
3. viewerbackend 5 处 URL 派生未收敛（`cloudUrl()` 是 WS，管理面走 `httpBaseUrl()`）。
4. 站点 `/api/device/activate` 不回 classCode。
5. `md3/Md3Btn.qml:11` 自定义 `enabled` 盖掉 `Item.enabled` ⇒ 祖先 enabled 传导失效（共享组件不擅自改）。
6. 管理端下发 `kind=banner|mini` 的入口（viewer 未改，解析已在 main.cpp 就位）。
7. 打包 0.6.42 走 OTA：C 盘剩 2.5G，打包+备份约需 100MB+，且与并行线抢版本号 ⇒ 需用户裁决。

## 6 踩坑实证（最贵，按"还会再踩"排序）
- ⚠️⚠️ **截图/几何取证必须声明 DPI 感知**：未 `SetProcessDpiAwareness(2)` 时 `GetWindowRect`/`PrintWindow` 返回**虚拟化坐标**
  ⇒ 右侧一列 + 底部一条永远在截图外，被误判成"没画"。判据：彩条标尺（80 逻辑量到 120px ⇒ 缩放 1.5）。
- ⚠️⚠️ **qmllint 给假安全感**（不查目录导入类型缺失，0 Error 也能加载失败）——唯一可靠出口是**运行时探针**。
  抽 QML 块的纪律：awk 括号平衡 → 加载探针 → 最后才 qmllint。
- ⚠️ **删组件判据必须是「类型名」不是「路径」**（`SvgIcon {` 靠目录导入解析）。
- ⚠️ **QML 隐式作用域是迁移地雷**：裸标识符（th/isPhone）搬文件后链断且无声；`property var X: Y` 的 RHS 替换防自引用（`theme: theme` 环）。
- ⚠️ **z 只在同一父级内比较**：根级历史面板(z40) 会盖住 expandedCol 内的面面板(z60) ⇒ 修法是关下层 `visible`，不是加 z。
- ⚠️ **setProperty 后 QML 绑定异步重算**：同帧读派生属性得到 `<invalid>`（探针假红）⇒ 注入留 ≥500ms。
- ⚠️ **跨父级锚被静默丢弃**；锚链对不可见 item 依然起效（隐藏须 `height: 0` 清零）。
- ⚠️ **普通 `Item` 的 height 不跟 implicitHeight**（只有 Image/Text 那类默认绑定）⇒ `Md3NavBar{implicitHeight:80}` 不写 height 就落 0。
- ⚠️ **契约默认值是"对一半人正确"的陷阱**：`UiContract.js` 的 `scope` 默认 `["viewer"]` ⇒ 被控端声明漏写就全被过滤（零错误零日志）。
- ⚠️ **被控端部署不含 QtQuick Controls**（exe 旁无 `Qt6QuickControls2.dll`）⇒ 被控端 qml/ 只能用 QtQuick 基础类型
  （ScrollBar/TextArea 一律加载失败，症状 `entry 解析失败`）；viewer 侧有，两端不同。
- ⚠️ **`qBound` 下限会钳掉探针的"压缩时间参数"**（pollSec 5s / staleSec 10s）⇒ 探针等待窗口按**下限值**设计。设计探针前先读 clamp。
- ⚠️ **假服务必须复刻真实载荷形状**：SSE `broadcast_state.data` 挂 nextUp/listeners，轮询端点平级 ⇒ 两种形状都要认；
  线上 SSE **无 `event:` 行**（类型只在 JSON `type`），行尾 `\n`/`\r\n` 必须归一否则分帧失败。
- ⚠️ **门禁正则会误杀合法写法 / 会命中注释**⇒ 匹配前先剥注释；门禁要能演进。
- ⚠️ **Windows 原生事件投递语义**：`PostThreadMessageW` 投的 WM_DEVICECHANGE **到不了** QAbstractNativeEventFilter；
  **SendMessage 到 Qt 顶层窗口能到** ⇒ 合成消息探针必须走隐藏顶层窗口。探针宏值带冒号（`"X:"`）。
- ⚠️ **cl 链接步绝不带 /TP**；MSYS/Git Bash 会把 `/EHsc` 当路径转换 ⇒ cl 参数一律走 Python subprocess（list 形式）。
- ⚠️ **探针链的是 agent 的 `island.cpp.obj`**（`_island_qml_probe.py` 只编探针自身）⇒ 改 enum/头文件后
  **必须先重编 agent**，否则旧 obj 把新枚举值落到 `else` 分支（实测 `Banner(3)` 变 Fullscreen：752×752 且 form 仍 capsule）。
  判据：几何恰好等于"全屏尺寸"而形态串是 capsule ⇒ 先怀疑 obj 混版，别怀疑公式。
- ⚠️ **嵌套 lambda 捕获 `main` 的局部 `app` ⇒ C2326**（外层 lambda 没捕获，内层再捕获也拿不到）⇒ 定时器接收者用 `qApp`。
- ⚠️ **`availableGeometry()` 的 x/y 偏移会让"反推可用区"算错**（本机任务栏在**左**，ag=(48,0,1659,960)）⇒
  几何核对必须**把 ag 本身打进日志**，不要靠窗口位置反推（本轮因此一度以为几何公式错了）。
- ⚠️ 探针自身也会骗人：QML 匿名类名（`_QML_21`）每次变（基线先剥序号）；`Repeater` delegate 的 objectName 不在 QObject 树；
  参数"看着该生效"先验它真生效（--actions 实测没注入）；假上下文属性**必须照真实签名**写。
- ⚠️ 测量方法自身的错会污染判断：划范围看缩进层级+闭合标记；`uniq -c -f1` 在"行号:匹配"上不生效。
