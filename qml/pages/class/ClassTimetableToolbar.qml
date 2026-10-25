// 班级课表 · 工具栏（标题 / 群名 / 导入 + 刷新 / 选群条）· 2026-10-25 拆件
//
// 拆件原因：ClassTimetablePane 已 287 行（红线 200）。工具栏是纯展示 + 两个意图信号，
// 抽出来之后 Pane 只留状态与网络回执。
//
// 选群条只在档案含多个课表群时出现。**完整档案有 12 个群、而且 12 个群全叫
// 「新课表群」**（1~12 班全校课表）—— 靠名字分不出谁是谁，云端已按「ClassPlans 首次
// 出现顺序」回 label（1班…12班）并标 inferred。所以：
//   · 名字可信（1班课表群/8班课表群）→ 显示群名；
//   · 名字不可信（12 个同名）→ 显示推断班名 + 明确提示"请核对"，绝不假装知道。
// 群不猜：猜错就是把 3 班课表挂到 4 班。
import QtQuick
import "../../components/fresh"  // FreshBtn
import "../../components/theme"  // Md3 令牌（pragma Singleton）
import "../../island"            // SvgIcon

Column {
    id: bar

    property string subtitle: ""
    property bool canImport: false
    property var groups: []
    /** true = 班名是按档案顺序推的（12 群同名），界面必须提示人工核对。 */
    property bool nameInferred: false

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
            wrapMode: Text.WordWrap
            text: bar.nameInferred
                  ? ("这份档案有 " + bar.groups.length + " 个课表群、而且群名都一样："
                     + "下面按档案顺序编号为 1班…" + bar.groups.length + "班，导入前请对照课表核对。")
                  : ("这份档案有 " + bar.groups.length + " 个课表群，选一个导入本班：")
            font.pixelSize: Md3.bodyS
            color: bar.nameInferred ? (Md3.frDanger || "#DC2626") : (Md3.frInk2 || "#6B7280")
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
                        // 推断时群名是 12 个「新课表群」，显示出来只会误导 ⇒ 只显示「X班」
                        text: (modelData.inferred === true
                               ? (modelData.label || "")
                               : (modelData.name || modelData.label || ""))
                              + " · " + (modelData.days || 0) + "天"
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
