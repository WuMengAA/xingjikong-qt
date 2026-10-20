// 首次启动引导 · 错误提示对话框（2026-10-20）
// 用户明确要求：绑定失败必须在**当前页**弹一个友好的提示，不能崩、不能白屏。
// 这里包一层共享 Md3Dialog（不自造对话框外观），只统一「一个按钮 + 一句能照着做的话」口径。
import QtQuick
import "../components/md3" as M

Item {
    id: root
    property var tk: ({})
    property bool opened: false
    property string title: "这一步没成功"
    property string text: ""
    property string buttonText: "我知道了"
    signal dismissed()

    visible: opened
    z: 1000

    M.Md3Dialog {
        anchors.fill: parent
        md3: root.tk
        opened: root.opened
        title: root.title
        text: root.text
        actions: [{ text: root.buttonText, role: "confirm" }]
        onActionClicked: {
            root.opened = false;
            root.dismissed();
        }
    }
}
