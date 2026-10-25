// 班级工作台 · 中栏「班级展板」—— 真·常亮展示视图（2026-10-25 重做 + 功能补全）
//
// 这里曾经是一张「规划中」说明卡：点中栏「展板」段什么都看不到，用户报「按钮按了
// 没反应」。现在是有内容、能操作的旁观视图：
//   · 主区：已发布内容（ClassBoardNoticePane：通知/作业卡，多条自动轮播、圆点可跳）
//   · 右区：本班设备实时画面（只读网格：无 MouseArea、不选中、不下发）
//   · 顶栏：班名 + 实时时钟 + 「发布到展板」+「全屏展示 / 退出展示」
//
// 为什么自带网格而不复用 ClassDeviceGrid：那件带"点了选中"的交互语义（手型 + hover
// + 选中描边），展板是给大屏看的、不该能点 —— 复用会再次制造"点了没反应"。
// 「发布到展板」不是装饰：它把页级发布弹窗打开（真发请求，落到本班设备 + 事件流）。
import QtQuick
import "../../island"           // SvgIcon（FreshBtn 由 ClassBoardNoticePane 之外的子件自带）
import "../../components/fresh" // FreshBtn
import "../../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: pane

    property var deviceModel: null    // ClassDeviceModel
    property var evModel: null        // ClassEventModel
    property bool subscribed: false
    property string className: ""
    property string classCode: ""
    /** 宿主是否已进入页内全屏展示（隐藏左右栏）；本件只据此改按钮文案。 */
    property bool expanded: false

    /** 请求宿主切换页内全屏（展开 / 收起）。 */
    signal toggleExpanded()
    /** 请求宿主打开发布中心（页级模态，宿主持有）。 */
    signal publishRequested()

    /** 已发布内容（最新在前）。显式依赖 revision：事件增删都要重算 —— 只绑 count 会
     *  在"快照重置且条数不变"时漏更新。 */
    readonly property var notices: {
        if (!pane.evModel) return []
        var dep = pane.evModel.revision   // eslint-disable-line no-unused-vars
        return pane.evModel.recentOfKind("publish", 12)
    }

    // ── 顶部时钟：只展示、不参与业务；15s 走一次，跨分钟即更新 ──
    property string clockText: "--:--"
    function tickClock() {
        const d = new Date()
        const p = function (n) { return (n < 10 ? "0" : "") + n }
        pane.clockText = p(d.getHours()) + ":" + p(d.getMinutes())
    }
    Component.onCompleted: pane.tickClock()
    Timer { interval: 15000; running: true; repeat: true; onTriggered: pane.tickClock() }

    /** 窄栏只保内容（设备画面是附加信息，不够宽就不挤）。 */
    readonly property bool showDevices: pane.width >= 620
    readonly property int devicePaneWidth: 232

    Column {
        anchors.fill: parent
        spacing: 12

        // ── 顶栏：标题 + 班名 | 时钟 + 发布 + 全屏 ──
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
                    tint: Md3.frGreen || "#22C55E"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "班级展板"
                    font.pixelSize: Md3.titleM || 16
                    font.weight: Font.Medium
                    color: Md3.frInk || "#111827"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: pane.className !== ""
                          ? pane.className
                          : (pane.classCode !== "" ? pane.classCode : "未选择班级")
                    font.pixelSize: Md3.body13 || 13
                    color: Md3.frInk2 || "#6B7280"
                    elide: Text.ElideRight
                    width: Math.max(48, pane.width - 400)
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                Text {
                    text: pane.clockText
                    font.pixelSize: Md3.titleM || 16
                    font.family: "monospace"
                    color: Md3.frInk2 || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
                FreshBtn {
                    text: "发布到展板"
                    icon: "plus"
                    variant: "primary"
                    enabled: pane.subscribed
                    tip: pane.subscribed ? "向本班发布通知 / 作业（展板与设备同步）" : "先选择并订阅一个班级"
                    onClicked: pane.publishRequested()
                }
                FreshBtn {
                    text: pane.expanded ? "退出展示" : "全屏展示"
                    variant: "tonal"
                    onClicked: pane.toggleExpanded()
                }
            }
        }

        // ── 主区：已发布内容（主）+ 设备画面（次，只读）──
        Row {
            width: parent.width
            height: Math.max(120, parent.height - head.height - 12)
            spacing: 12

            ClassBoardNoticePane {
                width: pane.showDevices ? parent.width - pane.devicePaneWidth - 12 : parent.width
                height: parent.height
                notices: pane.notices
            }

            // 只读网格：无点击语义，格子点了没有任何反应是"设计如此"。
            // 包一层 Item：空态文案不能写在 GridView 里（会挂到 contentItem，
            // 没 delegate 时该容器高度为 0 ⇒ 文案跑到左上角而不是居中）。
            Item {
                id: devBox
                visible: pane.showDevices
                width: pane.showDevices ? pane.devicePaneWidth : 0
                height: parent.height

                GridView {
                    id: board
                    anchors.fill: parent
                    clip: true
                    model: pane.deviceModel
                    cellHeight: 132
                    readonly property int cols: 2
                    cellWidth: Math.floor(width / cols)

                    delegate: ClassBoardTile {
                        width: board.cellWidth
                        height: 132
                        uid: model.uid
                        name: model.name
                        online: model.online
                        hasThumb: model.hasThumb
                        thumbRev: model.thumbRev
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: board.count === 0
                    text: pane.subscribed ? "本班暂无设备" : "未选择班级"
                    font.pixelSize: Md3.bodyS || 12
                    color: Md3.cOutline || "#79747E"
                }
            }
        }
    }
}
