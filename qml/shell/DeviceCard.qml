//
//   ① 卡片加 Level-1 柔和投影（零依赖偏移矩形，仿 SideSheet，不引 QtQuick.Effects）；
//   ③ 选中态用主色薄雾（primary @ 0.06）+ 主色描边，不再是"纯白 + 1px 灰边"的平片感；
//   ④ 在线设备左侧加一道终末地工业青（#2FB8C6）细规，作"数据在流"的冷调点缀（不抢紫色主色）；
//   ⑤ 版本号等宽字（技术读数）。
//   真帧订阅与在线/离线文案逻辑**完全不变**（纪律：不退回点阵占位）。
import QtQuick
import "../island"          // SvgIcon
import "../components/theme"   // Theme 设计令牌单例（pragma Singleton）

Item {
    id: root

    property string uid: ""
    property string deviceName: ""
    property bool online: false
    property string version: ""
    property int lastSeenAgoSec: -1
    property bool selected: false

    signal clicked()

    implicitWidth: 132
    implicitHeight: 168

    property real _stateOpacity: 0

    // ── 卡片本体（须先于阴影声明，阴影用 z 压在它身后）──
    Rectangle {
        id: cardBg
        anchors.fill: parent
        radius: Theme.radius.lg
        // 无边框（QtShadcn 铁律 1）：选中态靠**底色反白**表达，
        // 原写法 selected ? 1.5 : 1 + 描边色 已删。
        color: root.selected ? Theme.color.accent : Theme.color.background
        z: 1
    }

    // ── Level-1 柔和投影（零依赖，两层偏移矩形近似，不引 QtQuick.Effects）──
    Rectangle {
        anchors.fill: cardBg
        anchors.topMargin: 6
        radius: Theme.radius.lg
        color: Qt.rgba(0, 0, 0, 0.05)
        z: 0
    }
    Rectangle {
        anchors.fill: cardBg
        anchors.topMargin: 2
        radius: Theme.radius.lg
        color: Qt.rgba(0, 0, 0, 0.04)
        z: 0
    }

    // 选中态主色薄雾（状态层之上、内容之下的半透 primary wash）
    Rectangle {
        anchors.fill: cardBg
        radius: Theme.radius.lg
        color: Theme.color.primary
        opacity: root.selected ? 0.06 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
        z: 2
    }

    Rectangle {
        id: stateLayer
        anchors.fill: cardBg
        radius: Theme.radius.lg
        color: Theme.color.textPrimary
        opacity: root._stateOpacity
        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
        z: 2
    }

    // ── 终末地工业青 · 在线设备的"数据在流"左缘细规 ──
    // 2026-10-22：硬编码 #2FB8C6 → 令牌 cAccent（零硬编码纪律）
    Rectangle {
        visible: root.online
        width: 3
        radius: 1.5
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.topMargin: 18
        anchors.bottomMargin: 18
        anchors.leftMargin: 5
        color: Theme.color.accent
        z: 3
    }

    // 卡片内容：统一 24px 内边距（规格，留白去贴边）
    Item {
        anchors.fill: parent
        anchors.margins: 24
        z: 4

        Column {
            anchors.fill: parent
            spacing: 8

            // 缩略图区：真帧优先，没帧时压一块稳定的底色（不闪）
            Rectangle {
                width: parent.width
                height: 72
                radius: Theme.radius.sm
                color: Theme.color.surfaceVariant
                clip: true

                Image {
                    id: thumb
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    cache: false
                    visible: root.online
                    property int tick: 0
                    source: root.uid !== "" ? ("image://frames/" + root.uid + "?t" + tick) : ""
                }
                Connections {
                    target: backend
                    function onThumbnailChanged() { thumb.tick++ }
                }

                Text {
                    anchors.centerIn: parent
                    visible: !root.online
                    text: "未开机"
                    font.pixelSize: 12
                    color: Theme.color.textSecondary
                }

                // 在线徽标点（右下角，与卡片状态一致，不编造帧率数字）
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: root.online ? Theme.color.success : Theme.color.textSecondary
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                }
            }

            Text {
                width: parent.width
                text: root.deviceName !== "" ? root.deviceName : root.uid
                font.pixelSize: 13
                font.weight: Font.Medium
                color: Theme.color.textPrimary
                elide: Text.ElideRight
            }

            // 状态行：在线给版本号（等宽技术字），离线给"多久没见"——云端真字段，没有就不画。
            Text {
                width: parent.width
                text: {
                    if (root.online) return root.version !== "" ? root.version : "在线"
                    if (root.lastSeenAgoSec < 0) return "离线"
                    return root.lastSeenAgoSec < 60 ? "刚断线"
                           : Math.floor(root.lastSeenAgoSec / 60) + " 分前"
                }
                font.pixelSize: 12
                font.family: (root.online && root.version !== "") ? "monospace" : ""
                color: Theme.color.textSecondary
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        id: cardMa
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onEntered: root._stateOpacity = 0.08
        onExited: root._stateOpacity = 0
        onPressed: root._stateOpacity = 0.10
        onReleased: root._stateOpacity = (cardMa.containsMouse ? 0.08 : 0)
        onClicked: root.clicked()
        z: 5
    }
}
