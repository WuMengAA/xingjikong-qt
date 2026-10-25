// MD3 对话框：scrim（50% 黑）+ 居中卡片（圆角 28）+ 进场缩放淡入动效。
// actions: [{text, role}]，role = cancel（text 按钮）/ confirm（filled）/ danger（filled error）。
//
// 2026-10-22 按《全局 UI 构建红线》（AGENTS.md §9）升级：
//   ① scrim 32% → **50% 黑**（#80000000）：模态遮罩按规范口径，压暗背后内容；
//   ② 补**进场动效**：scrim 淡入 + 卡片 0.85→1.0 缩放淡入（OutCubic，200ms）——
//      规范「弹窗弹出必须伴随背景变暗和微小的缩放动画，禁止生硬闪现」。
//      退出仍即时隐藏（visible: opened 控制）——退场动画留待有点单再做，先补进场。
// 动效时长走令牌 durShort（200ms）；md3 缺省 ({}) 时回落 200，不让动画静默失效。
import QtQuick
import "."          // 同目录的 Md3Btn（QML 同目录类型需显式导入才稳）
import "../theme"

Item {
    id: root
    property bool opened: false
    property string title: ""
    property string text: ""
    property var actions: []
    signal actionClicked(int index, string role)

    /** 动效时长：令牌优先，({}) 兜底 200（规范值）。 */
    readonly property int animDur: (Md3 && Md3.durShort !== undefined) ? Md3.durShort : 200

    visible: opened
    anchors.fill: parent

    // 进场：opened 置 true 时播放（restart 可处理连续开关）
    onOpenedChanged: if (root.opened) openAnim.restart()
    ParallelAnimation {
        id: openAnim
        NumberAnimation {
            target: scrim; property: "opacity"
            from: 0; to: 1; duration: root.animDur; easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: dialogCard; property: "scale"
            from: 0.85; to: 1.0; duration: root.animDur; easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: dialogCard; property: "opacity"
            from: 0; to: 1; duration: root.animDur; easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5)
        MouseArea { anchors.fill: parent }   // 挡背后点击（MD3 模态）
    }
    Rectangle {
        id: dialogCard
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 312)
        height: dialogCol.implicitHeight + 48
        radius: Md3.rXL
        color: Md3.cContHigh
        Column {
            id: dialogCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 16
            Text {
                text: root.title
                font.pixelSize: Md3.headlineS
                color: Md3.cOnSurface
                width: parent.width
            }
            Text {
                text: root.text
                font.pixelSize: Md3.bodyM
                color: Md3.cOnSurfaceVar
                width: parent.width
                wrapMode: Text.Wrap
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 24
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            spacing: 8
            Repeater {
                model: root.actions
                delegate: Md3Btn {
                    required property var modelData
                    required property int index
                    text: modelData.text
                    variant: modelData.role === "cancel" ? "text"
                           : (modelData.role === "danger" ? "error" : "filled")
                    onClicked: root.actionClicked(index, modelData.role)
                }
            }
        }
    }
}
