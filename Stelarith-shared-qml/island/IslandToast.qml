import QtQuick 2.15
import "."

// 灵动岛 · 瞬时提示（统一布局规范 · 基元五）
//
// MD3 Snackbar 风，替代各 face 手搓的「黑块 toast」与裸「知道了」硬提示（截图糙点之一）。
// 跨深浅主题都走**固定深色表面 + 浅字**，保证与任何岛背景都有对比；不依赖 th.bg
// （否则会和岛背景同色融掉）。时长走 th.durMid 淡入淡出，默认自动隐藏。
//
// 用法：
//   IslandToast {
//       id: toast
//       th: root.th
//       anchors.horizontalCenter: parent.horizontalCenter
//       anchors.bottom: parent.bottom
//       anchors.bottomMargin: 12
//   }
//   toast.show("已清空历史")          // 可选第二参 ms 覆盖默认时长
//   toast.show("已清空", 2000)       // 带行动按钮：
//   toast.actionLabel = "撤销"
//   toast.actionClicked.connect(() => { ... })
Item {
    id: toast
    property var th: null
    property string text: ""
    property string actionLabel: ""   // 可选行动按钮（如 "撤销"）
    property bool open: false
    signal actionClicked()

    visible: toast.open
    opacity: toast.open ? 1 : 0
    Behavior on opacity {
        NumberAnimation { duration: toast.th ? toast.th.durMid : 200; easing.type: Easing.OutQuad }
    }

    implicitWidth: box.width
    implicitHeight: box.height

    Rectangle {
        id: box
        height: 36
        radius: height / 2
        // 固定深色表面（跨深浅可读，反色于浅岛/融于深岛但描边+浅字保证对比）
        color: "#323232"
        width: Math.max(
                   txt.implicitWidth
                   + (toast.actionLabel !== "" ? (act.implicitWidth + 32) : 0)
                   + 32,
                   120)

        Text {
            id: txt
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: toast.text
            color: "#FAFAFA"
            font.pixelSize: 13
        }

        Text {
            id: act
            visible: toast.actionLabel !== ""
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: toast.actionLabel
            color: toast.th ? toast.th.accent : "#D0BCFF"
            font.pixelSize: 13
            font.weight: Font.Medium
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: { toast.actionClicked(); toast.hide() }
            }
        }
    }

    Timer { id: hideTimer; repeat: false; onTriggered: toast.open = false }

    function show(msg, ms) {
        toast.text = msg
        toast.open = true
        hideTimer.interval = ms || (toast.th ? toast.th.durXLong * 3 : 1800)
        hideTimer.restart()
    }
    function hide() { toast.open = false }
}
