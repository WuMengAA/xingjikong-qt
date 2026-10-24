// MD3 文本框（filled）：浮动标签 + 聚焦底线 2dp + 错误态。
// 铁律：不设 background —— 暗色下系统白底白字（#94 事故教训），用 Rectangle 自绘底。
import QtQuick

Item {
    id: root
    property var md3: ({})
    property string text: ""
    property string placeholder: ""
    property string helper: ""
    property bool errorMode: false
    property bool password: false
    signal editingFinished()

    implicitWidth: 280
    implicitHeight: 68

    Rectangle {
        anchors.fill: parent
        radius: md3.rXS
        color: md3.cSurfaceVar
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            color: root.errorMode ? md3.cError
                                  : (input.activeFocus ? md3.cPrimary : md3.cOnSurfaceVar)
        }
        Text {
            id: lbl
            x: 16
            y: (input.activeFocus || input.text !== "") ? 8 : 22
            font.pixelSize: (input.activeFocus || input.text !== "") ? md3.labelS : md3.bodyL
            color: root.errorMode ? md3.cError
                                  : (input.activeFocus ? md3.cPrimary : md3.cOnSurfaceVar)
            text: root.placeholder
            Behavior on y { NumberAnimation { duration: md3.durShort; easing.type: Easing.Bezier; easing.bezierCurve: md3.easeStd } }
            Behavior on font.pixelSize { NumberAnimation { duration: md3.durShort } }
        }
        TextInput {
            id: input
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 10
            font.pixelSize: md3.bodyL
            color: md3.cOnSurface
            echoMode: root.password ? TextInput.Password : TextInput.Normal
            // 双向同步（2026-10-24 修复）：此前 TextInput 只有下面那行 onTextChanged，
            // 外部预填 root.text（如改名行预填当前班名）**根本不显示**到可见输入框 ——
            // 没有任何绑定从 root.text 指向 input.text。
            text: root.text
            onTextChanged: root.text = text
            onEditingFinished: root.editingFinished()
        }
    }
    Text {
        visible: root.helper !== ""
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.top: parent.bottom
        anchors.topMargin: 4
        font.pixelSize: md3.bodyS
        color: root.errorMode ? md3.cError : md3.cOnSurfaceVar
        text: root.helper
    }

    /** 显式设值（双写）。用户键入会摧毁 input.text↔root.text 的绑定（Qt 语义：
     *  对绑定目标做原生赋值即消绑定），此后外部只改 root.text 不再同步到可见
     *  输入框 —— 需要"打开时预填/清空"的调用方（班级管理输入行等）必须走这个
     *  函数，不能只赋 root.text。 */
    function setText(t) { input.text = t; root.text = t }
}
