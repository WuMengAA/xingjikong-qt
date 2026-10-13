// MD3 卡片：elevated（containerLow 色阶）/ filled（containerHigh）/ outlined（描边）。
// 圆角 12，内边距 16。用法：Md3Card { md3: …; variant: "filled" } { 子项 }
import QtQuick

Rectangle {
    id: root
    property var md3: ({})
    property string variant: "filled"   // elevated | filled | outlined
    default property alias content: inner.data

    implicitWidth: 200
    implicitHeight: inner.implicitHeight + 32
    radius: md3.rM
    color: variant === "filled" ? md3.cContHigh
                                : (variant === "elevated" ? md3.cContLow : md3.cSurface)
    border.width: variant === "outlined" ? 1 : 0
    border.color: md3.cOutlineVar

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: 16
        implicitHeight: childrenRect.height
    }
}
