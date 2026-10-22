import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// 2026-10-18 起 MD3 布局组件（Md3Card / Md3Btn / Md3CheckBox…）。
// 真源在 Stelarith-shared-qml/ui/md3/，构建时由 CMake copy_directory 落到
// qml/Stelarith/components/md3/ —— 源码树下 qmllint 解析不到（磁盘加载模式），
// 以「构建 + 运行」验证为准，与岛、与集控页同套路。
import "../components/md3"
import "../island"         // SvgIcon（唯一真源在共享岛目录，构建期拷到 qml/island/）

// 班级看板（整屏页 · page 4）· V1.0 P1
//
// 蓝图对应：「管理员视图：全局调度台」——多卡片式班级桌面，卡片直接给
// 班级名 / 在线设备数 / 状态拼图，不点进班级也能在右侧做快捷操作，
// 支持多选班级后底部升起批量操作栏。
//
// ── 千人千面为什么不用写权限判断 ─────────────────────────────────────
// 老师与管理员看到不同数量的班级卡片，**不是本页判断出来的**：
// 云端行级过滤（RBAC 第二批，2026-10-20 端到端 19/19 验证）已经在下发
// 设备表时按身份 scope 过滤过 —— backend.devices 里就只有这个人该看的班。
// 本页只负责「把拿到的东西分组摆出来」，不许再猜一次身份。
// （真要加新能力点（cap）时走 perms.can，不要在这里抄一份角色名单。）
//
// ── 数据来源与「不编造」边界 ─────────────────────────────────────────
// 班级名：backend.classes（云端班级目录，含 code/name）。
// 设备：backend.devices（uid / name / classCode / online / version）。
// 缩略图：**有真源**，用 FrameImageProvider 的 uid 通道 `image://frames/<uid>?t<tick>`
//   （main.cpp:253-261：id 为 "f" = 主画面 / "cam" = 摄像头轨 / 其余 = 该 uid 的缩略帧）。
//   ⚠️ 更正记录（2026-10-20）：我此前凭印象断言「没有每台一张的缩略图通道」，
//      于是用"状态点阵"占位 —— 那是**没查清就下的错误结论**。实际通道存在，
//      这里已改回真帧渲染。订阅由宿主统一发起（见 shell/MainWindow.qml），本页只负责取帧与刷新。
//
// ctx 形状与集控页同一份契约（Main.qml 的 _pageCtx）：
//   ctx.window.picked / runActionById / goPage / deviceFilter …
Item {
    id: page

    property var ctx

    // 设计令牌：宿主经 ctx 传入（与 ControlPage.qml 同一写法，不写 fallback 实例）。
    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

    // 多选模式（页内状态）：开了才显示卡片上的复选框与底部批量条。
    property bool multi: false
    // 已勾选的班级 code（页内状态，与集控页的 picked（设备 uid）是**两回事**：
    // 这里选的是班级，执行前才展开成设备 uid 交给宿主）。
    property var pickedCodes: []

    Rectangle { anchors.fill: parent; color: th.body }

    // ── 班级卡片数据：把设备表按 classCode 分组 ───────────────────────
    // 绑定会自动依赖函数里读到的 backend.devices / backend.classes，
    // 云端一刷新列表就重算，不需要手动通知。
    function classCards() {
        var devs = (backend && backend.devices) ? backend.devices : []
        var cs = (backend && backend.classes) ? backend.classes : []
        var byCls = {}
        var i, c, d, g
        for (i = 0; i < devs.length; ++i) {
            d = devs[i]
            if (!d) continue
            c = d.classCode || ""
            if (!byCls[c]) byCls[c] = { code: c, total: 0, online: 0, uids: [], dots: [] }
            g = byCls[c]
            g.total++
            if (d.online !== false) g.online++      // 老版本云端不带 online 时按在线处理
            g.uids.push(d.uid)
            g.dots.push(d.online !== false)
        }
        var arr = []
        for (i = 0; i < cs.length; ++i) {
            c = cs[i].code
            g = byCls[c]
            arr.push({
                code: c,
                name: (cs[i].name || cs[i].code || "未命名班级"),
                total: g ? g.total : 0,
                online: g ? g.online : 0,
                uids: g ? g.uids : [],
                dots: g ? g.dots : []
            })
            if (g) delete byCls[c]
        }
        // 设备表里有、但班级目录里没有的码（离线台账 / 尚未登记）：也要列出来，
        // 不能静默丢掉 —— 丢掉的表现是「明明上线了，看板上却没有这个班」。
        for (c in byCls) {
            g = byCls[c]
            arr.push({
                code: c,
                name: (c === "" ? "未分班" : c),
                total: g.total, online: g.online, uids: g.uids, dots: g.dots
            })
        }
        return arr
    }

    // ── 批量：把勾选的班级展开成设备 uid，交给宿主的唯一执行入口 ──────
    // ⚠️ 刻意**不自己发指令**：执行只在 Main.qml 的 runActionById 一处，
    //    权限（permOk）、能力（capMissing）、危险动作二次确认都在那里，
    //    本页再写一遍就会变成第二套门控（历史上命令面板与右栏各写一份，
    //    静默不一致过）。这里只负责「把选择翻译成 uid 列表」。
    function applyBulk(actionId) {
        var cards = page.classCards()
        var uids = []
        for (var i = 0; i < cards.length; ++i) {
            if (page.pickedCodes.indexOf(cards[i].code) < 0) continue
            for (var j = 0; j < cards[i].uids.length; ++j) uids.push(cards[i].uids[j])
        }
        if (uids.length === 0) return
        ctx.window.picked = uids          // 与集控页共用同一份多选台账
        ctx.window.runActionById(actionId)
    }

    function togglePick(code) {
        var a = page.pickedCodes.slice()
        var k = a.indexOf(code)
        if (k >= 0) a.splice(k, 1); else a.push(code)
        page.pickedCodes = a
    }

    function enterClass(code) {
        // 班级筛选写回云端侧真源（与集控页班级网格同一个 setter），
        // 详情页直接消费 backend.currentClass，不靠页面间传参。
        backend.setCurrentClass(code)
        ctx.window.goPage(5)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── 顶栏：标题 + 统计 + 多选开关 ────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                text: "班级看板"
                color: th.fg
                font.pixelSize: 16
                font.weight: Font.Medium
            }
            Text {
                // 统计只报真数：班级数 / 设备总数 / 在线数，全部从设备表派生
                text: {
                    var cards = page.classCards()
                    var t = 0, on = 0
                    for (var i = 0; i < cards.length; ++i) { t += cards[i].total; on += cards[i].online }
                    return cards.length + " 个班 · " + t + " 台设备 · " + on + " 台在线"
                }
                color: th.fg3
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }

            Md3Btn {
                md3: th
                text: page.multi ? "退出多选" : "进入多选模式"
                variant: page.multi ? "filled" : "tonal"
                onClicked: {
                    page.multi = !page.multi
                    if (!page.multi) page.pickedCodes = []
                }
            }
        }

        // ── 主区：班级卡片网格 ─────────────────────────────────────
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: 232
            cellHeight: 132
            // 列数不写死：GridView 按 cellWidth 自己排满一行，宽屏多列、窄屏少列，
            // 写死列数会让宽屏右侧空一大块、窄屏挤出横向滚动条。
            model: page.classCards()

            /**
             * 缩略帧刷新计数器：后端 `emit thumbnailChanged()` 时递增，
             * 逼 Image 重新取帧（source 字符串不变 Qt 不会重读）。
             * ⚠️ 订阅不在这里做 —— 本页可能被 Loader 嵌进骨架，
             *    订阅统一归宿主（MainWindow），避免两个视图抢同一份订阅互相退订。
             */
            property int tick: 0
            Connections {
                target: backend
                function onThumbnailChanged() { grid.tick++ }
            }

            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight

                Md3Card {
                    anchors.fill: parent
                    anchors.margins: 8
                    md3: th
                    variant: "filled"

                    // 多选模式下的勾选框（只在这一层出现，常态不占地方）
                    Md3CheckBox {
                        id: chk
                        visible: page.multi
                        md3: th
                        checked: page.pickedCodes.indexOf(modelData.code) >= 0
                        anchors.top: parent.top
                        anchors.left: parent.left
                        onToggled: page.togglePick(modelData.code)
                    }

                    Column {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: page.multi ? 26 : 0
                        spacing: 6

                        Text {
                            width: parent.width
                            text: modelData.name
                            color: th.cOnSurface
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Text {
                            // 在线比例：分母是全班台账数（离线也计入），
                            // 老师要看的是「全班多少台、现在几台开着」
                            text: modelData.online + " / " + modelData.total + " 台在线"
                            color: th.cOnSurfaceVar
                            font.pixelSize: 12
                        }

                        // 缩略图（真帧：image://frames/<uid>，tick 由外层 grid 统一递增）
                        Rectangle {
                            width: parent.width
                            height: 72
                            radius: th ? th.rS : 8
                            color: th ? th.cCont : "#F3EDF7"
                            clip: true
                            Image {
                                anchors.fill: parent
                                fillMode: Image.PreserveAspectCrop
                                cache: false
                                visible: modelData.online !== false
                                source: modelData.uid ? ("image://frames/" + modelData.uid + "?t" + grid.tick) : ""
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: modelData.online === false
                                text: "未开机"
                                font.pixelSize: 12
                                color: th ? th.cOutline : "#79747E"
                            }
                        }
                        Text {
                            visible: modelData.total === 0
                            text: "这个班还没有设备"
                            color: th.cOutline
                            font.pixelSize: 12
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (page.multi) page.togglePick(modelData.code)
                        else page.enterClass(modelData.code)
                    }
                }
            }
        }

        // ── 空态：一个班都没有 ─────────────────────────────────────
        Column {
            visible: page.classCards().length === 0
            Layout.fillWidth: true
            topPadding: 40
            spacing: 6
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "还没有班级"
                color: th.fg
                font.pixelSize: 14
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "设备绑定班级之后会出现在这里"
                color: th.fg3
                font.pixelSize: 12
            }
        }

        // ── 批量操作条：勾了班才升起 ───────────────────────────────
        // 按钮清单来自 ActionDefs.js 里 bulk:true 的那几条（宿主 bulkList() 投影），
        // 与集控页左栏「已选 N 台」那一列同源，不再手写第二份按钮表。
        Rectangle {
            visible: page.multi && page.pickedCodes.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            radius: th.rM
            color: th.cContHigh
            border.color: th.cOutlineVar
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Text {
                    text: "已选 " + page.pickedCodes.length + " 个班"
                    color: th.cOnSurface
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                Item { Layout.fillWidth: true }

                Repeater {
                    model: ctx.window.bulkList()
                    delegate: Md3Btn {
                        md3: th
                        text: modelData.title
                        variant: "tonal"
                        radius: 12
                        onClicked: page.applyBulk(modelData.id)
                    }
                }

                Md3Btn {
                    md3: th
                    text: "取消选择"
                    variant: "text"
                    onClicked: page.pickedCodes = []
                }
            }
        }
    }
}
