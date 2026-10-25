import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components"     // Btn / ToggleRow 等本端自有通用组件
// 2026-10-18：MD3 布局组件（Md3Chip / Md3SwitchRow…）。
// 真源在 Stelarith-shared-qml/ui/md3/，构建时由 CMake copy_directory 落到
// qml/Stelarith/components/md3/ —— 源码树下 qmllint 解析不到（磁盘加载模式），
// 以「构建 + 运行」验证为准，与岛同套路。
import "../../components/md3"
import "../../island"         // SvgIcon（唯一真源在共享岛目录，构建期拷到 qml/island/）
import "../../components/theme"   // Md3 令牌（pragma Singleton）

// 控制页（整屏页 · U5「两端同 shell」A-2 第二页）
//
// 从 `Main.qml` 的 `StackLayout` 第二个 `Item` 抽出（原 761–1542，782 行）。
// 抽取规矩与设置页同一套（见 pages/SettingsPage.qml 顶部说明）：
//
// 1. **只搬不改** —— 几何 / 文案 / 交互逐字保留（探针 + 人眼回归的判据）。
// 2. **隐式作用域搬不走**，依赖一律显式化：
//      `th`（58 处裸标识符） → 本页同名 `readonly property`（宿主经 ctx 传令牌）
//                              ⇒ body 里那 58 处**零改动**
//      `root.<member>`（16 种 57 处） → `ctx.window.<member>`
//      `uiReg.capMissing()`          → `ctx.registry.capMissing()`
// 3. `backend`（36 处）是 C++ **上下文属性** —— 独立 QML 文件里同样可访问，**零改动**。
// 4. 页内两个内联组件（`GroupTitle` / `ActBtn`）是**本页局部** component（右栏 Column 内），
//    随页一起搬，作用域不变。
//
// ctx 形状 = `shared-qml/ui/UiPageHost.qml` 的 `_ctx()`（**同一份契约**）：
//   window / theme / registry / host / scope / open / close / closePanels
Item {
    id: page

    property var ctx

    // 设计令牌：宿主经 ctx 传入。
    // ⚠️ 刻意**不写 fallback 实例**：`Md3` 只存在于**产物树**的 `components/`
    //    （共享 ui 由构建期 `copy_directory` 拷过去），源树里没有 —— 写下来会让
    //    "直接吃源树"的探针加载失败。宿主保证给，这里只做空保护。
    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null
    Rectangle { anchors.fill: parent; color: th.body }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: 0
        anchors.bottomMargin: 14
        spacing: 14

// ───── 左：设备（配角）─────
    // ── 2026-10-18 真布局修复：左栏整体包 Flickable（原来没有滚动）──
    // 原来这里是裸 Column，设备一多（>10 台）就**超出左栏高度被裁掉**，
    // 老师以为"机器没上线"——其实是列表撑不下了。右栏早就用 Flickable 解决了
    // 同样的问题（见 653 行注释），左栏漏了。现在补上，与右栏同模式。
    Flickable {
        // 响应式（三·响应式增量层）：窄窗且未手动展开时收起，监控画面吃满宽度；
        // 展开时给 200 更舒服。完整三栏重排属 dsh S2 Main.qml 重构，本层不抢。
        Layout.preferredWidth: ctx.window.compact ? (ctx.window.devOpen ? 200 : 0) : 160
        Layout.maximumWidth: ctx.window.compact ? (ctx.window.devOpen ? 220 : 0) : 184
        visible: !(ctx.window.compact && !ctx.window.devOpen)
        Layout.fillHeight: true
        contentWidth: width
        contentHeight: devCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: devCol
            width: parent.width
            spacing: 2

    Text {
        text: "设备"
        color: th.fg3
        font.pixelSize: 12
        leftPadding: 10
        bottomPadding: 8
    }

    // 分组视图（4.4）：全部 / 在线 / 离线 三段筛选
    Row {
        width: 168
        spacing: 4
        leftPadding: 10
        bottomPadding: 4
        Repeater {
            model: [ { k: "all", l: "全部" }, { k: "online", l: "在线" }, { k: "offline", l: "离线" } ]
            // 2026-10-18 MD3 布局：分组筛选从手写 42×22 方块 → Md3Chip（filter 变体）。
            // 高 32/圆角 8、选中 secondaryContainer + ✓、未选 outline 描边 + 状态层。
            // 三枚 chip 一排 + 计数，是把"分段筛选"说清楚的 MD3 标准形态。
            delegate: Md3Chip {
                text: modelData.l
                selected: (ctx.window.deviceFilter === modelData.k)
                onClicked: ctx.window.deviceFilter = modelData.k
            }
        }
        // 计数（放在筛选右边，一眼看到每组多少台）
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: (ctx.window.deviceFilter === "all")
                  ? (ctx.window.devices.length + " 台")
                  : (ctx.window.filteredDevices.length + " 台")
            color: th.fg4
            font.pixelSize: 11
        }
    }

    // 分班制（需求 #4）：按班级维度筛选设备。班级来自云端 /api/classes（界面只消费
    // backend.classes），当前选中写 backend.currentClass，filteredDevices 已按它过滤。
    // 班级网格：2 列平铺每格班名+设备数，点格按该班过滤下方列表（与设备列表共用面板高度，无独立滚动）。
    // ⚠️ 2026-10-11 修「选中班级是灰的、看不见字」：
    //   旧写法是 Item + Rectangle + Text，宽度写 `clsLbl.width + 16`，而 clsLbl
    //   又是 `anchors.centerIn: parent` —— 宽度绕回自己身上算，chip 被压到几乎没有
    //   宽度，文字全被裁掉，只剩一条灰边（看着就是"灰的、没字"）。
    //   改成 Rectangle 自己当 chip，宽度取 Text 的 **implicitWidth**（纯内容宽，
    //   不依赖布局），并给未选中态一个淡底，选中/未选中一眼可分。
    // 分班制（需求 #4）班级网格：类平铺成 2 列网格，每格班名(过长省略)+设备数；
    // 点格即按该班过滤下方设备列表(filteredDevices 已按 backend.currentClass 过滤)。
    Grid {
        x: 10
        width: 158
        columns: 2
        spacing: 4
        Repeater {
            model: (function () {
                var devs = backend.devices || []
                var byCls = {}
                for (var i = 0; i < devs.length; ++i) {
                    var c = devs[i] ? (devs[i].classCode || "") : ""
                    byCls[c] = (byCls[c] || 0) + 1
                }
                var arr = [{ code: "", name: "全部班级", cnt: devs.length }]
                var cs = backend.classes || []
                for (var j = 0; j < cs.length; ++j) {
                    arr.push({ code: cs[j].code, name: (cs[j].name || cs[j].code), cnt: byCls[cs[j].code] || 0 })
                }
                return arr
            })()
            // ── 2026-10-18 MD3 色阶重排（保留 2 列网格与"班名+台数"双行信息）──
            // 班级筛选是"双行信息密度"场景，Md3Chip 单行放不下"班名+台数"；
            // 用 MD3 容器色阶表达选中/未选：选中 secondaryContainer、
            // 未选 cContLow 底 + cOutlineVar 细边。班名 cOnSurface、台数 cOnSurfaceVar。
            // 与左侧"分组筛选 Md3Chip"、上方统计卡同语言，整栏不再三套色调。
            delegate: Rectangle {
                width: 73; height: 38; radius: 8
                color: (backend.currentClass === modelData.code) ? th.cSecondaryCont : th.cContLow
                border.color: (backend.currentClass === modelData.code) ? "transparent" : th.cOutlineVar
                border.width: 1
                Column {
                    anchors.centerIn: parent
                    spacing: 1
                    Text {
                        width: 65; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
                        text: modelData.name
                        color: (backend.currentClass === modelData.code) ? th.cOnSecondaryCont : th.cOnSurface
                        font.pixelSize: 11
                        font.weight: (backend.currentClass === modelData.code)
                                     ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        width: 65; horizontalAlignment: Text.AlignHCenter
                        text: (modelData.cnt !== undefined ? modelData.cnt : 0) + " 台"
                        color: (backend.currentClass === modelData.code) ? th.cOnSecondaryCont : th.cOnSurfaceVar
                        font.pixelSize: 10
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.setCurrentClass(modelData.code)
                }
            }
        }
    }

    Repeater {
        model: ctx.window.filteredDevices
        // ── 2026-10-18 MD3 色阶重排（保留 36 高紧凑布局与状态点）──
        // 设备行信息密度高（可能 30+ 台），不换 56 高的 Md3ListItem（会撑长整栏）；
        // 改用 MD3 容器色阶表达状态，与上方班级网格/筛选 chip 同语言：
        //   cur（当前查看）= secondaryContainer 底
        //   picked（多选）= secondary 描边 + 淡 secondaryContainer 底
        //   设备名 cOnSurface / 元信息 cOnSurfaceVar / 离线 cOutline
        delegate: Rectangle {
            width: 168
            height: 36
            radius: 9
            property bool cur: (modelData.uid === ctx.window.currentUid)
            property bool picked: (ctx.window.picked.indexOf(modelData.uid) >= 0)
            // picked 优先于 cur 显示（多选态是最明确的反馈）
            readonly property bool md3Active: picked || cur
            color: md3Active ? th.cSecondaryCont : (devHover.containsMouse ? th.cContLow : "transparent")
            Behavior on color { ColorAnimation { duration: th ? th.durShort : 200 } }

            // 状态点：在线亮、离线暗。以前这一颗恒亮，掉线的机器也画成"在线"，
            // 老师按着它发指令，等半天没回音 —— 先把真假摆出来。
            // ⚠️ 只信云端给的 online 字段；老版本云端不带这个键时按在线处理
            // （`=== false` 才判离线），免得整个列表一夜之间全灰掉。
            property bool devOnline: (modelData.online !== false)

            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: 10
                spacing: 9
                Rectangle {
                    width: 5; height: 5; radius: 3
                    anchors.verticalCenter: parent.verticalCenter
                    // 2026-10-10 彩色令牌：在线=ok 绿 / 离线=err 红
                    color: devOnline ? th.ok : th.err
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    // 2026-10-08：优先显示设备名（云端 register 带上来），没有才回退 uid
                    text: (modelData.name !== undefined && modelData.name !== null && modelData.name !== "")
                          ? modelData.name : modelData.uid
                    color: md3Active ? th.cOnSecondaryCont : (devOnline ? th.cOnSurface : th.cOutline)
                    font.pixelSize: 14
                    font.weight: (md3Active || picked) ? Font.Medium : Font.Normal
                    elide: Text.ElideRight
                    // 右侧那行小字要留位置，别让长机器名压上去
                    width: Math.max(60, 168 - 10 - 5 - 9 - devMeta.width - 16)
                }
            }

            // 右端一行小字：在线显示被控端版本，离线显示"多久没见"。
            // 全部取自云端真字段（version / lastSeenAgoSec），没有就不画 —— 不编数据。
            Text {
                id: devMeta
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: !devOnline
                      ? ((modelData.lastSeenAgoSec !== undefined
                          && modelData.lastSeenAgoSec >= 0)
                         ? (modelData.lastSeenAgoSec < 60 ? "刚断线"
                            : Math.floor(modelData.lastSeenAgoSec / 60) + " 分前")
                         : "离线")
                      : (modelData.version !== undefined && modelData.version !== ""
                         ? modelData.version : "")
                color: md3Active ? th.cOnSecondaryCont : th.cOnSurfaceVar
                font.pixelSize: 12
            }

            // ∠ 选中态（4.4）：picked 那台描边强调——"选了哪台"是当前最明确的反馈。
            // 底色已用 secondaryContainer 表达，这里补一条 secondary 描边区分
            //  cur（当前看）与 picked（已选中）两件事。
            Rectangle {
                anchors.fill: parent
                radius: 9
                color: "transparent"
                border.color: picked ? th.cSecondary : "transparent"
                border.width: picked ? 1 : 0
            }
            MouseArea {
                id: devHover
                anchors.fill: parent
                hoverEnabled: true
                // 多选交给 root：按钮位置要拿 mods 和 index，那是主界面才知道的事
                onClicked: (mouse) => ctx.window.pickDevice(modelData.uid, mouse.modifiers, index)
            }
        }
    }

    // 已选 N 台 + 批量动作（4.4 多选 / 4.5 批量三段式）
    // ⚠️ 刻意放在**左栏设备列表下面**，不动右栏：右栏那列是固定高的一列，
    //    塞进去只会把底部标签栏顶出去（见文件头 minimumHeight 的注释）。
    Column {
        visible: ctx.window.picked.length > 0
        width: 168
        spacing: 6
        Text {
            text: "已选 " + ctx.window.picked.length + " 台"
            color: th.fg
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        // 批量按钮沿用右栏 ActBtn 的外观（同高 30、同圆角 rCtrl）：
        // 同一屏里一个方块一个方块地漂，比样式不统一更容易看出不对。
        Repeater {
            // U4：同样从声明派生（ActionDefs.js 里 bulk:true 的两条）
            model: ctx.window.bulkList()
            delegate: Rectangle {
                id: batBtn
                width: 168; height: 30; radius: th.rCtrl
                color: batMa.containsMouse ? th.hover2 : "transparent"
                border.color: th.stroke
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: modelData.title
                    color: th.op
                    font.pixelSize: 13
                }
                MouseArea {
                    id: batMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ctx.window.runActionById(modelData.id)
                }
            }
        }
        Text {
            text: "取消选择（Esc）"
            color: th.fg4
            font.pixelSize: 12
            MouseArea { anchors.fill: parent; onClicked: ctx.window.clearPick() }
        }
    }

    Item { width: 1; height: 14 }
    Rectangle { width: 148; height: 1; color: th.line; x: 10 }
    Text {
        text: "全校"
        color: th.fg3
        font.pixelSize: 12
        leftPadding: 10
        topPadding: 12
    }
    }   // ← devCol（内部 Column）结束
    }   // ← Flickable 结束（2026-10-18 新增的滚动层）

// ───── 中：画面（主体）─────
ColumnLayout {
    // 主体：窗口有多宽，画面就吃多少（左上 badge 那行也在这栏里）。
    Layout.fillWidth: true
    Layout.preferredWidth: 640
    Layout.fillHeight: true
    spacing: 10

    // 响应式（三·响应式增量）：窄窗时左「设备」栏自动收起，这里给入口展开/收起
    Rectangle {
        visible: ctx.window.compact
        width: 96; height: 30; radius: th.rCtrl
        color: devToggleMa.containsMouse ? th.hover2 : th.panel
        border.color: th.stroke; border.width: 1
        Text { anchors.centerIn: parent; text: ctx.window.devOpen ? "收起设备" : "设备列表"; color: th.fg; font.pixelSize: 13 }
        MouseArea { id: devToggleMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ctx.window.devOpen = !ctx.window.devOpen }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Text {
            text: ctx.window.currentUid === "" ? "未选择设备" : ctx.window.currentUid
            color: th.fg
            font.pixelSize: 16
            font.weight: Font.Medium
        }
        Text {
            visible: ctx.window.currentUid !== ""
            text: Math.round(backend.fps * 10) / 10 + " fps · "
                  + Math.round(backend.lastFrameBytes / 1024) + " KB"
            color: th.fg3
            font.pixelSize: 12
        }

        // 链路徽标：让用户一眼看出"现在看的是实时流还是轮询截图"。
        // 设计稿的规矩是黑白里唯一的强调手段＝反白，所以：
        //   实时（RTC）＝反白（最亮，因为它是我们要的状态）
        //   轮询（JPEG）＝描边灰（弱化，说明它在走退化的路）
        //   建流中     ＝todo 底 + 呼吸圆点（说清"正在办"，不是"没反应"）
        Rectangle {
            visible: ctx.window.currentUid !== ""
            radius: 4
            height: 20
            // 文案宽度不固定，交给 trailing Text 自己撑
            width: linkLabel.implicitWidth + (ctx.window.linkBusy ? 24 : 14)
            color: backend.frameSource === "vnc" ? th.inv
                 : ctx.window.linkBusy ? th.todo : "transparent"
            border.color: backend.frameSource === "vnc" ? th.inv
                        : ctx.window.linkBusy ? th.stroke : th.stroke
            border.width: 1

            Rectangle {
                visible: ctx.window.linkBusy
                anchors.left: parent.left
                anchors.leftMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                width: 5
                height: 5
                radius: 3
                color: th.op
                // 呼吸用 QML 自带的循环动画，不用 Math.sin + 高频 Timer：
                // 后者为了让一个 5px 的点动起来，每秒要把整个绑定重算 20~30 次
                opacity: 0.25
                SequentialAnimation on opacity {
                    running: ctx.window.linkBusy
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.25; to: 1.0; duration: 620 }
                    NumberAnimation { from: 1.0; to: 0.25; duration: 620 }
                }
            }

            Text {
                id: linkLabel
                anchors.left: parent.left
                anchors.leftMargin: ctx.window.linkBusy ? 18 : 7
                anchors.verticalCenter: parent.verticalCenter
                text: backend.frameSource === "vnc" ? "实时"
                    : ctx.window.linkBusy ? "正在接通…"
                    : backend.frameSource === "jpeg" ? "轮询" : "等待画面"
                color: backend.frameSource === "vnc" ? th.win
                     : th.fg3
                // 稿上没有 10px 这一档（最小是"注 11"），徽标字太小会把
                // "实时/轮询"变成需要凑近看的装饰 —— 统一提到 11。
                font.pixelSize: 12
                font.weight: backend.frameSource === "vnc" ? Font.Medium : Font.Normal
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                ToolTip.visible: containsMouse
                ToolTip.delay: 400
                ToolTip.text: backend.frameSource === "vnc"
                              ? "实时画面，延迟百毫秒级"
                              : ctx.window.linkBusy
                                ? "正在建立实时连接…"
                                : "轮询截图，比实时慢"
            }
        }

        Item { Layout.fillWidth: true }
    }

    // 画面本体：按住拖动＝直接操作那台机器
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: th.rCard
        // 稿 .canvas＝panel 底 + #1A1A1A 边。以前这里写死 #0E0E0E/#1F1F1F，
        // 两个都不在令牌里 —— 换主题时这块永远是暗的，是个没人记得住的例外。
        color: th.panel
        border.color: th.canvas
        border.width: 1
        clip: true

        Image {
            id: screenImg
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            visible: backend.frameSource !== "vnc"
            cache: false
            property int tick: 0
            source: ctx.window.frameReady ? ("image://frames/f?" + tick) : ""

            Connections {
                target: backend
                function onFrameChanged() { screenImg.tick++ }
            }
        }

        // ── 远控画面（VNC over WebSocket · libvncclient）──
        // 渲染管线：libvncclient 帧缓冲 → QImage → VncImageProvider → QML Image。
        // 绝不用 QWidget 画图（约束）；tick 递增逼 Image 重取图（source 不变时 Qt 不重读）。
        // jpeg 轮询模式下本图隐藏，画面走上面的 screenImg；vnc 模式下反过来。
        Image {
            id: vncImg
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            visible: backend.frameSource === "vnc"
            cache: false
            property int tick: 0
            source: backend.frameSource === "vnc" ? ("image://vnc/live?" + tick) : ""
        }
        Connections {
            target: backend
            function onVncFrameReady(img) { vncImg.tick++ }
        }

        // 空态照稿 .empty：标题 12/操作色 + 说明 11/辅助色，两行。
        // 分三种情形（没选机器 / 正在接通 / 选了但没帧）：以前一句话包打天下，
        // 用户看不出是"正在办"还是"没人理"。
        Column {
            anchors.centerIn: parent
            visible: !ctx.window.frameReady && backend.frameSource !== "vnc"
            spacing: 6

            BusyIndicator {
                visible: ctx.window.linkBusy || (ctx.window.currentUid !== "" && backend.frameCount > 0)
                anchors.horizontalCenter: parent.horizontalCenter
                width: 28
                height: 28
                running: ctx.window.linkBusy || (ctx.window.currentUid !== "" && backend.frameCount > 0)
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: ctx.window.currentUid === "" ? "还没选设备"
                    : ctx.window.linkBusy ? "正在接通"
                    // 切设备时旧画面已经作废、新机器的第一帧还没到 ——
                    // 以前这里什么都不显示（frameCount 非 0 ⇒ 走不到空态），
                    // 屏幕上是上一台的残影，看着像"切了没用"。
                    : backend.frameCount > 0 ? "正在接入这台设备"
                    : "等画面"
                color: th.op
                font.pixelSize: 13
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: ctx.window.currentUid === "" ? "在左边选一台"
                    : ctx.window.linkBusy ? "连上就有画面"
                    : backend.frameCount > 0 ? "第一帧马上就到"
                    : "这台设备还没发来第一帧"
                color: th.fg3
                font.pixelSize: 12
            }
        }

        // 静态区角标：被控端画面没在动时（老师看的是静止的投影/待机界面），
        // 我们就不白烧重绘 —— 但必须说清楚"是没动，不是卡了"，否则用户以为掉线。
        Text {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 9
            visible: backend.screenStatic && ctx.window.frameReady && backend.frameSource !== "vnc"
            text: "画面未变化 · 已暂停刷新"
            // 固定灰，刻意不跟主题走：这行浮在**远程画面**上，底色是对方的桌面（不可控），
            // 用主题色反而可能在浅色画面上消失。
            color: "#8A8A8A"
            font.pixelSize: 12
        }

        // ── 远控输入：绝对坐标「指哪打哪」─────────────────────────
        // ⚠️ 画面是 Image.PreserveAspectFit：窗口比例和被控端屏幕比例不一致时，
        //   画面上下/左右会留黑边。旧写法拿「鼠标位置 ÷ 画面区尺寸」当归一化坐标，
        //   等于把黑边也算进去了 —— 点黑边上的位置操作会整体落到机器边缘，
        //   越靠边偏得越离谱（这就是"我点这儿、光标跑那儿"）。
        //   现在先把画面区换算成**内接矩形**，只认矩形内的比例，矩形外夹到边界。
        //   坐标是绝对的（0~1 → 被控端屏幕像素），不做任何相对位移累积，
        //   所以拖到哪就到哪，不存在越用越飘。
        MouseArea {
            id: screenArea
            anchors.fill: parent
            enabled: backend.frameCount > 0

            // 鼠标键 → 协议里的 button：以前只有左键，右键/中键远控过去被当左键按，
            //   右键菜单、中键自动滚全指望它。
            // ⚠️ 2026-10-10：这三个原先声明在**外层 Rectangle** 上，而 QML 的非限定名
            //   只查「自身对象 → 组件根」两级 —— MouseArea 的手柄够不到外层兄弟元素的
            //   函数，于是每次点画面都刷 `ReferenceError: normOf is not defined`，
            //   远控点击整个失效。移到 MouseArea 自身（唯一使用者）即根治。
            function btnOf(b) {
                if (b === Qt.RightButton) return "right"
                if (b === Qt.MiddleButton) return "middle"
                return "left"
            }
            readonly property rect fitRect: {
                var fw = backend.frame.width, fh = backend.frame.height
                var w = screenArea.width, h = screenArea.height
                if (fw <= 0 || fh <= 0 || w <= 0 || h <= 0)
                    return Qt.rect(0, 0, w, h)
                var s = Math.min(w / fw, h / fh)
                var iw = fw * s, ih = fh * s
                return Qt.rect((w - iw) / 2, (h - ih) / 2, iw, ih)
            }
            // 画面内像素 → 被控端屏幕归一化坐标（0~1，绝对位置）
            // ⚠️ 2026-10-11：画面区尺寸还没算出来时 fitRect 宽高是 0，
            //    除零得到 NaN，传到 C++ 转 int 就是 INT_MIN —— 日志里刷几百行
            //    `操控→ move 在 (-2147483648%, -2147483648%)`，光标被甩到教室机角上。
            //    所以这里返回 null，调用处直接不发（C++ 侧也有一道 isfinite 兜底）。
            function normOf(mx, my) {
                var r = fitRect
                if (!(r.width > 0) || !(r.height > 0)) return null
                return [Math.min(1, Math.max(0, (mx - r.x) / r.width)),
                        Math.min(1, Math.max(0, (my - r.y) / r.height))]
            }
            // 左 / 中 / 右键全收：这三键在教室机上都用得上
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.CrossCursor

            onPressed: function (mouse) {
                screenArea.forceActiveFocus()   // 点击画面 → 键盘焦点给画面区（远控键盘生效）
                var n = normOf(mouse.x, mouse.y)
                if (!n) return
                backend.sendPointer("down", n[0], n[1], btnOf(mouse.button))
            }
            // 只在**按住时**才发移动：悬停绝不能往教室机灌鼠标移动
            // （曾经用 hoverEnabled + 自记 dragging，结果悬停就发指令、把日志刷爆）
            onPositionChanged: function (mouse) {
                if (mouse.buttons & (Qt.LeftButton | Qt.MiddleButton | Qt.RightButton)) {
                    var n = normOf(mouse.x, mouse.y)
                    if (!n) return
                    backend.sendPointer("move", n[0], n[1])
                }
            }
            onReleased: function (mouse) {
                var n = normOf(mouse.x, mouse.y)
                if (!n) return
                backend.sendPointer("up", n[0], n[1], btnOf(mouse.button))
            }

            // ── 远控键盘（2026-10-17 补齐）：点击画面获得焦点后按键即下发 ──
            // Qt.Key_* → Windows 虚拟键码（VK）。只映射常用键；特殊键（Ctrl+Alt+Del 等）
            // 由 QML 按 vk 组合逐键 down/up 下发，被控端 SendInput 单键注入。
            function vkOf(qk) {
                // 字母（Qt.Key_A..Key_Z = 0x41..0x5A，与 VK_A..VK_Z 相同）
                if (qk >= Qt.Key_A && qk <= Qt.Key_Z) return qk
                // 数字（Qt.Key_0..9 = 0x30..0x39，与 VK_0..9 相同）
                if (qk >= Qt.Key_0 && qk <= Qt.Key_9) return qk
                // 功能键 F1..F24 = VK_F1..F24
                if (qk >= Qt.Key_F1 && qk <= Qt.Key_F24)
                    return qk - Qt.Key_F1 + 0x70
                // 方向键：Qt 是 0x01000012..15，VK_LEFT..DOWN = 0x25..0x28
                if (qk === Qt.Key_Left) return 0x25
                if (qk === Qt.Key_Up) return 0x26
                if (qk === Qt.Key_Right) return 0x27
                if (qk === Qt.Key_Down) return 0x28
                // 常用编辑/控制键
                var map = {
                    [Qt.Key_Escape]: 0x1B, [Qt.Key_Tab]: 0x09, [Qt.Key_Backtab]: 0x09,
                    [Qt.Key_Backspace]: 0x08, [Qt.Key_Delete]: 0x2E, [Qt.Key_Insert]: 0x2D,
                    [Qt.Key_Home]: 0x24, [Qt.Key_End]: 0x23, [Qt.Key_PageUp]: 0x21,
                    [Qt.Key_PageDown]: 0x22, [Qt.Key_Return]: 0x0D, [Qt.Key_Enter]: 0x0D,
                    [Qt.Key_Space]: 0x20, [Qt.Key_CapsLock]: 0x14, [Qt.Key_NumLock]: 0x90,
                    [Qt.Key_ScrollLock]: 0x91, [Qt.Key_Pause]: 0x13, [Qt.Key_Print]: 0x2C,
                    [Qt.Key_Shift]: 0x10, [Qt.Key_Control]: 0x11, [Qt.Key_Alt]: 0x12,
                    [Qt.Key_Meta]: 0x5B, [Qt.Key_Context1]: 0x5D, [Qt.Key_SysReq]: 0x2C
                }
                return map[qk] !== undefined ? map[qk] : 0
            }
            focus: true
            Keys.onPressed: function (event) {
                var vk = vkOf(event.key)
                if (vk <= 0) { event.accepted = false; return }
                // 修饰键 + 普通键逐个 down（组合键由多次 onPressed 自然完成）
                backend.sendKey(vk, "down")
                event.accepted = true
            }
            Keys.onReleased: function (event) {
                var vk = vkOf(event.key)
                if (vk <= 0) { event.accepted = false; return }
                backend.sendKey(vk, "up")
                event.accepted = true
            }
        }
    }

    // 稿 .viewfoot：左边「已收 N 帧 · 连接正常」，右边音量。
    // 帧数原来挤在标题行里跟设备名抢位置，挪下来之后也顺手满足了
    // 规则 4（同一个数一屏只说一遍）。
    // 「连接正常」是术语换日常词的产物：云端 WS 通着就说这句，不说协议状态。
    RowLayout {
        Layout.fillWidth: true
        spacing: 14
        Text {
            visible: ctx.window.currentUid !== ""
            text: "已收 " + backend.frameCount + " 帧"
            color: th.fg3; font.pixelSize: 12
        }
        Text {
            visible: ctx.window.currentUid !== ""
            text: backend.connected ? "连接正常" : "连不上云端"
            color: th.fg3; font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        Text {
            // 只显示被控端确认过的音量，没确认过就空着（见 remoteVolume 注释）
            visible: ctx.window.remoteVolume >= 0
            text: ctx.window.remoteVolume + "%"
            color: th.fg3; font.pixelSize: 12
        }
    }
}

// ───── 右：动作（配角）─────
// 以前 20 个按钮平铺在一个网格里，找"关机"要把整栏扫一遍，
// 而它旁边就坐着"截图"——误点代价和它完全不在一个量级。
// 现在按「点了会怎样」分三组：**电源**（不可逆，独占整行、视觉更重）
// / **看看**（只读，安全）/ **让它做事**（会改远端状态，成对的一左一右）。
//
// ⚠️ 2026-10-08 改成**可滚动**（Flickable 包一层）。原因：这栏是固定高度的一列，
//    而 1280x720 这类教室屏/投影很常见 —— 窗口根本放不下 680 的最小高度
//    （再加标题栏和任务栏），窗口底部的标签栏被切在屏幕外，
//    表现就是"界面跑到屏幕外面去了"。改成可滚之后窗口能压到更矮，
//    内容放不下就滚，而不是顶出去。
//    内层 ColumnLayout 的 Layout.* 一律失效（它现在的父是 Flickable，不是定位器），
//    所以宽高都写在它自己身上 —— 别再往它身上加 Layout.xxx。
Flickable {
    Layout.preferredWidth: 280
    Layout.maximumWidth: 300
    Layout.fillHeight: true
    contentWidth: width
    contentHeight: rightCol.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    ColumnLayout {
    id: rightCol
    width: parent.width
    spacing: 10

    // 按钮外观只有这一份：以前"文件"是描边加粗、"音量"没特殊处理、
    // 其余是普通描边，三套样式早就漂了，改个圆角得改三处。
    //
    // 注意 Loader 的坑：Loader **不会**把自身尺寸让给被加载项，被加载项的
    // width/height 必须显式绑到 loader 上。否则 Rectangle 用的是自己那句
    // `width: ...` 算出 0（或者更糟：算出一个比容器大得多的值被裁掉），
    // 表现就是"按钮框还在、字没了"——而且 QML 一句报错都不给。
    Component {
        id: actBtn

        Rectangle {
            id: btnBody
        // d = **一条声明**（ActionDefs.js 的归一化对象，U4 起直接传它）。
        // 门控两套分开、别合并：role 看人（backend.mayDo），caps 看机器
        // （被控端上报的指令能力清单）。wide/heavy 由使用处注入。
        property var d: null
            property bool wide: false
            property bool heavy: false
            property bool pressed2: false

            // 宽度往上问，别自己拍。见 Btn 里那段注释：写死具体值会让
            // "跟着容器变"这件事失效，而 Loader 那条路又是单向绑不回来的。
            width: parent ? parent.width : 94
            // ── 2026-10-18 MD3：动作按钮从 30px 描边方块升级为 40px tonal 胶囊 ──
            // 旧样式（th.stroke 描边 + th.hover 底）与概览页被改掉的快捷按钮同源，
            // 是"一眼老样子"的主因之一。现按 MD3 tonal 按钮：
            //   · 高度 30→40（MD3 标准按钮高），radius 全圆
            //   · heavy（不可逆操作）用 filled error 语义（cError 底 + cOnError 字）
            //   · 普通动作用 tonal（cSecondaryCont 底 + cOnSecondaryCont 字）
            //   · 状态层 hover 8% / press 12% 叠 onSecondaryContainer（MD3 规范）
            //   · 未实现（灰）保持留白 + 淡描边（不引诱点击，语义不变）
            height: 40
            radius: 20
            property bool md3Heavy: heavy
            color: {
                if (!capOkAll(d) && !permOk(d ? d.role : "")) return "transparent"
                if (heavy) return th.cError
                return th.cSecondaryCont
            }
            border.color: (!capOkAll(d) && !permOk(d ? d.role : "")) ? th.cOutlineVar : "transparent"
            border.width: 1
            // MD3 状态层：onSecondaryContainer 或 onError 叠加
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: heavy ? th.cOnError : th.cOnSecondaryCont
                opacity: pressed2 ? 0.12 : (ma.containsMouse ? 0.08 : 0)
                Behavior on opacity { NumberAnimation { duration: th ? th.durShort : 200 } }
            }

            Text {
                anchors.centerIn: parent
                text: d ? d.title : ""
                color: {
                    if (!capOkAll(d) && !permOk(d ? d.role : "")) return th.cOnSurfaceVar
                    if (heavy) return th.cOnError
                    return th.cOnSecondaryCont
                }
                font.pixelSize: 13; font.weight: Font.Medium
            }

            MouseArea {
                id: ma
                anchors.fill: parent
                // t = 这功能压根没做；capOk = 这台被控端上报的能力清单里认不认这条指令；
                // permOk = 当前身份（管理员/教师）做不做得了。
                // 三者都置灰：没做的留白、不支持的照样看得见（灰着），
                // 没权限的也灰着 —— 但 tooltip 得说清"为什么灰"（下面那段），
                // 否则灰按钮看起来跟坏了的一样。
                enabled: capOkAll(d) && permOk(d ? d.role : "")
                hoverEnabled: capOkAll(d)
                // 以前鼠标移上去还是箭头，看不出这东西能点
                cursorShape: (capOkAll(d) && permOk(d ? d.role : ""))
                             ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                onPressed: pressed2 = true
                onReleased: pressed2 = false
                onCanceled: pressed2 = false
                onClicked: if (d) ctx.window.runActionById(d.id)
            }

            ToolTip {
                // 不支持 / 没权限的也显示 tooltip（不然灰按钮 Why 都不问，像坏了），
                // 文案分五种情况：不可逆 / 没选机器 / 这台机器不支持 / 没权限 / 正常
                visible: ma.containsMouse && (!capOkAll(d) || !permOk(d ? d.role : ""))
                delay: 500
                text: !permOk(d ? d.role : "") ? "要管理员身份才做得到（顶栏账户那里能切）"
                    : heavy ? "点了立刻执行，没法撤销"
                    : ctx.window.currentUid === "" ? "先在左边选一台设备"
                    : !capOkAll(d)
                      ? ("这台被控端不支持：它上报的能力里没有 " + ctx.registry.capMissing(d).join(" / "))
                    : ""
            }
        }
    }

    // 每个按钮外面都套一层 Loader 会重复四行样版代码（尺寸 + 两个开关 + 模型），
    // 所以统一走这个内联组件：传 d/wide/heavy，它负责把尺寸同步给 Loader。
    //
    // ⚠️ 两个坑一起踩过，这里都绕开了：
    //   1) Loader 不会把自身尺寸让给被加载项 → 必须显式 width/height，
    //      否则矩形算出 0 尺寸，表现是"框还在、字没了"，QML 一句报错都没有；
    //   2) Binding 是**单向、不回传**的 → 直接把 `width: it.width` 绑到被加载项上，
    //      "被加载项想跟着容器变宽"这件事永远传不回来（表达式把它覆盖掉了）。
    //      所以宽度用 `it.parent ? it.parent.width : ...` 让被加载项**自己往上问**。
    //
    // ⚠️ 这个文件里**不许**再把内联组件起名为 Btn：
    // 内联组件的声明作用域是**整个文档**，不是"写在哪个 Column 里"——
    // 曾经这里叫 `component Btn: Loader`，于是把 import "components" 带来的
    // 真·通用按钮（components/Btn.qml）整篇遮蔽掉：右栏之外所有 Btn 都变成了
    // 这个 Loader，没有 onClicked 信号，设置页一加载就报
    // "Cannot assign to non-existent property onClicked"，整页白屏。
    // 右栏这套自己用 ActBtn（Action 的缩写），和公共组件 Btn 各归各位。
    // 右栏三组的组名：以前三组各写一遍 Text（字号/颜色/上间距都可能漂）。
    // 抽成一个内联组件后，改一处三组一起变。
    component GroupTitle: Text {
        leftPadding: 0
        topPadding: 0
        rightPadding: 0
        bottomPadding: 0
        text: ""
        color: th.fg4
        font.pixelSize: 12
    }

    component ActBtn: Loader {
        id: ld
        property var d: null          // 一条声明（ActionDefs.js）
        property bool wide: false
        property bool heavy: false
        sourceComponent: actBtn
        width: wide ? 196 : (196 - 7) / 2
        height: 30
        onLoaded: {
            // 用 item 而不是 it：Loader 只暴露 item 这个属性，
            // `it` 不是绑定名，写它会得到 "ReferenceError: it is not defined"
            // （而且是在 onLoaded 里抛，按钮就那么空着，很难往这上面想）
            item.d = Qt.binding(function () { return ld.d })
            item.wide = Qt.binding(function () { return ld.wide })
            item.heavy = Qt.binding(function () { return ld.heavy })
        }
    }

    // ── 右栏动作区（U4：全部由声明派生）─────────────────────────
    //    改造前这里是 5 段手写 QML：通知（独立 1 条）+ 电源 3 + 看看 5 +
    //    任务 1 + 批量 2，每组一个 Repeater model 字面数组；而同一批动作在
    //    命令面板里还有**另一份**（38 条）。改一处忘一处就静默不一致。
    //    现在清单只有 qml/ActionDefs.js 一处：分组名、组内顺序、文案、
    //    权限/能力门控、宽窄（wide/heavy）全从声明来。
    //
    //    布局由「组内是否全 wide」决定：全 wide 竖排（电源/任务/批量 各占整行），
    //    否则 2 列网格（看看）。这与改造前逐组手写的结果一致。
    Repeater {
        model: ctx.window.rightGroups()
        delegate: Column {
            spacing: 6
            GroupTitle { text: modelData.name }
            Grid {
                columns: modelData.wide ? 1 : 2
                columnSpacing: 7
                rowSpacing: 7
                Repeater {
                    model: modelData.items
                    delegate: ActBtn {
                        d: modelData
                        wide: modelData.wide
                        heavy: modelData.heavy
                    }
                }
            }
        }
    }

    Item { Layout.fillHeight: true }

    // 稿 .logbox：贴右栏底部、上边一条分隔线、里面放最近几条回执。
    // 原来这儿是个"最近一次结果"的框：一是缺那条分隔线（右栏动作和结果糊成一片），
    // 二是概览页明明已经在攒 results 流水，这边却只留一条 —— 同样的数据存了两处。
    Column {
        Layout.fillWidth: true
        spacing: 9
        // Column 是定位器，没有 padding：想要"离上面远点"得走 Layout 的边距
        Layout.topMargin: 12

        Rectangle { width: parent.width; height: 1; color: th.sepline }

        Repeater {
            model: ctx.window.results.slice(0, 3)
            delegate: Row {
                spacing: 9
                width: parent.width
                SvgIcon {
                    name: modelData.ok ? "check" : "x"
                    tint: modelData.ok ? th.inv : th.op
                    width: 12
                    height: 12
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    width: parent.width - 30
                    text: modelData.uid + " · " + modelData.action
                    color: modelData.ok ? th.op : th.fg3
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            visible: ctx.window.results.length === 0
            text: "还没有操作"
            color: th.fg4
            font.pixelSize: 12
        }
    }   // 回执流水（logbox）
}       // 右栏动作（ColumnLayout，装在 Flickable 里，见上面的说明）
}       // 右栏 Flickable
}       // 三栏 RowLayout
}
