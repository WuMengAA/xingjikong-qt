import QtQuick 2.15
import "."

// 灵动岛 · 状态面（2026-11-01 从 IslandRoot 内联抽出）
//
// 纯排版：rows 由调用方传入（root.faceStatusRows，形如 [{k,v}]）。
// ⚠️ 依赖显式声明成 property（隐式作用域不跟着搬进独立文件）。
Item {
    id: f

    property var th: null
    property var rows: []

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 8

        Repeater {
            model: f.rows
            delegate: Item {
                required property var modelData
                width: parent.width
                height: 22

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.k
                    color: f.th ? f.th.sub : "#6E6E6E"
                    font.pixelSize: 12
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 90
                    horizontalAlignment: Text.AlignRight
                    text: modelData.v
                    color: f.th ? f.th.fg : "#161616"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }
    }
}
