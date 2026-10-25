// 班级工作台 · 上手引导（2026-10-25 用户点单「班级创建繁琐，没有引导」）
//
// 未订阅（还没建班 / 还没选班）时出现在右栏顶部，把"从零到设备入班"一次讲清：
//   ① 建班 / 选班   ② 生成 6 位接入码   ③ 设备首启输码入班
// 只做引导，不藏业务真源：真正的写操作仍走 ClassAdminPane（班级管理面板）；
// 本件只多一个「新建班级」快捷入口（开 ClassCreateDialog，省得再找按钮）。
//
// 步骤①的文案按"有没有班"分两种：没班 → 先建；有班 → 点左栏选一个。
import QtQuick
import "../../components/fresh"   // FreshCard / FreshBtn
import "../../island"             // SvgIcon
import "../../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: card

    property int classCount: 0

    signal createRequested()

    implicitHeight: col.implicitHeight + 28

    FreshCard {
        anchors.fill: parent

        Column {
            id: col
            width: parent.width
            spacing: 10

            Row {
                width: parent.width
                spacing: 8

                SvgIcon {
                    name: "rocket"
                    px: 16
                    tint: Md3.frGreen || "#22C55E"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "上手三步"
                    font.pixelSize: Md3.body13
                    font.weight: Font.Medium
                    color: Md3.frInk || "#111827"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Repeater {
                model: card.classCount === 0
                       ? [ { n: "1", t: "新建班级（点下面的按钮）" },
                           { n: "2", t: "选中班级 → 生成 6 位接入码" },
                           { n: "3", t: "设备首启 → 输入接入码 → 自动入班" } ]
                       : [ { n: "1", t: "在左栏点一个班级" },
                           { n: "2", t: "右栏「生成班级接入码」拿 6 位码" },
                           { n: "3", t: "设备首启 → 输入接入码 → 自动入班" } ]

                delegate: Row {
                    required property var modelData
                    width: parent.width
                    spacing: 8

                    Rectangle {
                        width: 20
                        height: 20
                        radius: 10
                        color: Md3.frGreenSoft || "#E9FBF0"
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            anchors.centerIn: parent
                            text: modelData.n
                            font.pixelSize: Md3.bodyS
                            font.weight: Font.Medium
                            color: Md3.frGreenD || "#16A34A"
                        }
                    }
                    Text {
                        text: modelData.t
                        width: parent.width - 28
                        font.pixelSize: Md3.bodyS
                        color: Md3.frInk2 || "#6B7280"
                        wrapMode: Text.WordWrap
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            FreshBtn {
                text: "新建班级"
                variant: "tonal"
                width: parent.width
                onClicked: card.createRequested()
            }
        }
    }
}
