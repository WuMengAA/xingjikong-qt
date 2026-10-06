// 星集控 · 通用空态
//
// 「没数据」是这个应用最常见的界面（还没登录、没设备、没操作记录、没选机器）。
// 以前每处自己写两行 Text：字号有 11/12 两种、颜色有 op/fg3/fg4 三种，
// 同一屏上三个空态长得像三套东西。现在统一：标题 12 / 说明 11 / 行距 6 / 居中。
//
// 两行文案是硬要求：只写"暂无设备"会被读成"坏了"；再补一句"屏幕上的机器会自动出现"
// 才说得清接下来会发生什么（应用该替用户把这件事讲明白，而不是让人干等）。
import QtQuick
import QtQuick.Controls

Item {
    id: box
    property var theme: ({ })
    property alias title: title.text
    property alias note: note.text
    property alias action: actionLoader.sourceComponent  // 可选：空态里的一颗按钮

    Column {
        anchors.centerIn: parent
        spacing: 6
        width: box.width

        Text {
            id: title
            anchors.horizontalCenter: parent.horizontalCenter
            text: box.title
            color: box.theme && box.theme.op !== undefined ? box.theme.op : "#C8C8C8"
            font.pixelSize: 12
        }
        Text {
            id: note
            anchors.horizontalCenter: parent.horizontalCenter
            text: box.note
            color: box.theme && box.theme.fg3 !== undefined ? box.theme.fg3 : "#5A5A5A"
            font.pixelSize: 11
        }
        Loader {
            id: actionLoader
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
