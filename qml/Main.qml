import QtQuick 2.15
import QtQuick.Window 2.15

// 批次 1/2 独立宿主：把 Showcase.qml 装进一个窗口直接看。
// 跑法：
//   D:/QTqwq/6.12.0/msvc2022_64/bin/qml.exe -I qml qml/Main.qml
//
// 批次 3 接入被控端后，本文件仍作回归探针（不进 exe、不参与打包）。
Window {
    id: win
    visible: true
    width: showcase.implicitWidth
    height: showcase.implicitHeight
    color: "#0E0E0E"
    title: "被控端灵动岛 · 渲染探针"

    Showcase {
        id: showcase
        anchors.fill: parent
    }
}
