// 班级工作台 · 中栏「设备网格」（#461-#472）
//
// 数据真源：ClassDeviceModel（typed，#460）—— 直接当 GridView 的 model，
// role 名静态可查；空态用 grid.count（ListView 的 count 随 modelReset/
// rowsInserted 自动刷新，不像 JS 手动调 count() 那样不会重绑）。
//
// 两套空态（诚实优先，不拿占位图糊弄）：
//   · 没订阅 ⇒ 引导去选班；
//   · 订了但零设备 ⇒ 说明这是云端名单口径（设备加班后出现在这里）。
// 选中态是纯 UI 态（selectedUid 由宿主持），动作面板按它决定「单台」按钮
// 点谁 —— 选中与执行为解耦两份：这里永远只画"看起来选中了"。
import QtQuick
import QtQuick.Controls          // ScrollBar
import "../../components/md3"       // （Md3Card 不直接用，网格裸铺；预留令牌件）
import "../../island"               // SvgIcon
import "../../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: pane

    property var model: null      // ClassDeviceModel
    property string selectedUid: ""
    property bool subscribed: false

    signal deviceClicked(string uid)

    // 响应式列数：格子最小 152 宽（144 瓦片 + 8 呼吸），至少 2 列
    readonly property int columns: Math.max(2, Math.floor(pane.width / 160))
    readonly property int cellW: Math.floor((pane.width - (pane.columns - 1) * 12) / pane.columns)

    GridView {
        id: grid
        anchors.fill: parent
        model: pane.model
        clip: true
        cellWidth: pane.cellW
        cellHeight: 200
        cacheBuffer: 200

        delegate: ClassDeviceTile {
            width: pane.cellW
            height: 188
            uid: model.uid
            name: model.name
            online: model.online
            version: model.version
            lastSeenAgoSec: model.lastSeen
            hasThumb: model.hasThumb
            thumbRev: model.thumbRev
            selected: model.uid === pane.selectedUid
            onClicked: pane.deviceClicked(model.uid)
        }

        ScrollBar.vertical: ScrollBar { width: 8 }
    }

    // ── 空态一：还没选班 ──
    Column {
        anchors.centerIn: parent
        visible: grid.count === 0 && !pane.subscribed
        spacing: 10

        SvgIcon {
            name: "users"
            px: 40
            tint: Md3.cOutline
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: "从左侧选择一个班级"
            font.pixelSize: Md3.body13
            color: Md3.cOnSurfaceVar
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    // ── 空态二：订了但没设备 ──
    Column {
        anchors.centerIn: parent
        visible: grid.count === 0 && pane.subscribed
        spacing: 10

        SvgIcon {
            name: "monitor"
            px: 40
            tint: Md3.cOutline
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: "这个班暂时没有设备"
            font.pixelSize: Md3.body13
            color: Md3.cOnSurfaceVar
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            text: "设备加入班级后会自动出现在这里"
            font.pixelSize: Md3.bodyS
            color: Md3.cOutline
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
