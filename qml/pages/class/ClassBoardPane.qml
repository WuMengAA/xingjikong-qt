// 班级工作台 · 中栏「班级展板」—— 真·常亮展示视图（2026-10-25 重做）
//
// 之前这里是一张「规划中」说明卡：用户点中栏「展板」段什么都看不到，
// 反映为「按钮按了没反应」。现在做成可用的**旁观视图**（只展示、不操作）：
//   · 主区：本班设备实时画面（本件自带只读网格：无 MouseArea、不选中、不下发）
//   · 右区：班级动态（复用 ClassTimelinePane；最新在上）
//   · 顶栏：班名 + 实时时钟 + 「全屏展示 / 退出展示」（宿主控制页内展开）
//
// 为什么自带网格而不复用 ClassDeviceGrid：那件带"点了选中"的交互语义（手型光标
// + hover + 选中描边），展板是给大屏看的、不该能点 —— 复用会再次制造"点了没反应"。
import QtQuick
import "../../island"           // SvgIcon
import "../../components/fresh" // FreshBtn

Item {
    id: pane

    property var md3: ({})
    property var deviceModel: null    // ClassDeviceModel
    property var evModel: null        // ClassEventModel
    property bool subscribed: false
    property string className: ""
    property string classCode: ""
    /** 宿主是否已进入页内全屏展示（隐藏左右栏）；本件只据此改按钮文案。 */
    property bool expanded: false

    /** 请求宿主切换页内全屏（展开 / 收起）。 */
    signal toggleExpanded()

    // ── 顶部时钟：只展示、不参与业务；15s 走一次，跨分钟即更新 ──
    property string clockText: "--:--"
    function tickClock() {
        const d = new Date()
        const p = function (n) { return (n < 10 ? "0" : "") + n }
        pane.clockText = p(d.getHours()) + ":" + p(d.getMinutes())
    }
    Component.onCompleted: pane.tickClock()
    Timer { interval: 15000; running: true; repeat: true; onTriggered: pane.tickClock() }

    /** 右区事件栏只在够宽时出现（窄栏先保设备画面）。 */
    readonly property bool showEvents: pane.width >= 560

    Column {
        anchors.fill: parent
        spacing: 12

        // ── 顶栏：标题 + 班名 | 时钟 + 全屏 ──
        Item {
            id: head
            width: parent.width
            height: 32

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                SvgIcon {
                    name: "tv"
                    px: 18
                    tint: md3.frGreen || "#22C55E"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "班级展板"
                    font.pixelSize: md3.titleM
                    font.weight: Font.Medium
                    color: md3.frInk || "#111827"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: pane.className !== ""
                          ? pane.className
                          : (pane.classCode !== "" ? pane.classCode : "未选择班级")
                    font.pixelSize: md3.body13
                    color: md3.frInk2 || "#6B7280"
                    elide: Text.ElideRight
                    width: Math.max(60, pane.width - 360)
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Text {
                    text: pane.clockText
                    font.pixelSize: md3.titleM
                    font.family: "monospace"
                    color: md3.frInk2 || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
                FreshBtn {
                    md3: pane.md3
                    text: pane.expanded ? "退出展示" : "全屏展示"
                    variant: "tonal"
                    onClicked: pane.toggleExpanded()
                }
            }
        }

        // ── 主区：设备画面（只读）+ 班级动态 ──
        Row {
            width: parent.width
            height: Math.max(80, parent.height - head.height - 12)
            spacing: 12

            // 只读网格：无 MouseArea，格子点了没有任何反应是"设计如此"。
            GridView {
                id: board
                width: pane.showEvents ? parent.width - 252 : parent.width
                height: parent.height
                clip: true
                model: pane.deviceModel
                cellHeight: 150
                readonly property int cols: Math.max(2, Math.floor(width / 180))
                cellWidth: Math.floor((width - (cols - 1) * 12) / cols)

                delegate: ClassBoardTile {
                    width: board.cellWidth
                    height: 150
                    md3: pane.md3
                    uid: model.uid
                    name: model.name
                    online: model.online
                    hasThumb: model.hasThumb
                    thumbRev: model.thumbRev
                }
            }

            ClassTimelinePane {
                visible: pane.showEvents
                width: 240
                height: parent.height
                md3: pane.md3
                model: pane.evModel
            }
        }
    }

    // 空态（订了但没设备 / 还没选班）——说人话，不摆假画面
    Column {
        anchors.centerIn: parent
        visible: board.count === 0
        spacing: 8

        SvgIcon {
            name: "monitor"
            px: 36
            tint: md3.cOutline
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: pane.subscribed ? "这个班暂时没有设备" : "从左侧选择一个班级"
            font.pixelSize: md3.body13
            color: md3.cOnSurfaceVar
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
