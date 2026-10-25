// 班级课表 · 工具栏（标题 / 群名 / 导入 + 刷新 / 选群条）· 2026-10-25 拆件
//
// 拆件原因：ClassTimetablePane 已 287 行（红线 200）。工具栏是纯展示 + 两个意图信号，
// 抽出来之后 Pane 只留状态与网络回执。
//
// 选群条只在档案含多个课表群时出现（真实档案：1班课表群 / 8班课表群）——
// 群不猜，必须由人点选：猜错就是把别班课表挂到这个班。
import QtQuick
import "../../components/fresh"  // FreshBtn
import "../../components/theme"  // Md3 令牌（pragma Singleton）
import "../../island"            // SvgIcon

Column {
    id: bar

    property string subtitle: ""
    property bool canImport: false
    property var groups: []

    signal importRequested()
    signal refreshRequested()
    signal groupPicked(string groupId)

    width: parent ? parent.width : 0
    spacing: 6

    // ── 标题行 ──
    Item {
        width: parent.width
        height: 30

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            SvgIcon {
                name: "clock"; px: 16; tint: Md3.frGreen || "#22C55E"
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: "课表"
                font.pixelSize: Md3.titleM
                font.weight: Font.Medium
                color: Md3.frInk || "#111827"
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: bar.subtitle
                font.pixelSize: Md3.bodyS
                color: Md3.frInk2 || "#6B7280"
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            FreshBtn {
                text: "导入档案"; icon: "upload"; variant: "tonal"
                enabled: bar.canImport
                tip: "选择 ClassIsland 档案（Default.json），选群后导入本班"
                onClicked: bar.importRequested()
            }
            FreshBtn {
                text: "刷新"; variant: "ghost"
                enabled: bar.canImport
                onClicked: bar.refreshRequested()
            }
        }
    }

    // ── 选群条（档案含多个班才出现）──
    Column {
        width: parent.width
        visible: bar.groups.length > 1
        spacing: 6

        Text {
            width: parent.width
            text: "这份档案有 " + bar.groups.length + " 个课表群，选一个导入本班："
            font.pixelSize: Md3.bodyS
            color: Md3.frInk2 || "#6B7280"
        }
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: bar.groups
                Rectangle {
                    width: gTxt.implicitWidth + 24
                    height: 28
                    radius: 14
                    color: gMa.containsMouse ? (Md3.cContHigh || "#ECE6F0") : (Md3.frLine || "#EEF0F3")
                    Text {
                        id: gTxt
                        anchors.centerIn: parent
                        text: (modelData.name || "") + " · " + (modelData.days || 0) + "天"
                        font.pixelSize: Md3.body13
                        color: Md3.frInk || "#111827"
                    }
                    MouseArea {
                        id: gMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: bar.groupPicked(modelData.id)
                    }
                }
            }
        }
    }
}
