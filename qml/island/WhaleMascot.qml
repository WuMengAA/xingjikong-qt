import QtQuick 2.15

// 鲸鱼娘（清单 #1：管理端独有，被控端补上）
//
// 素材：qrc:/whale/DSniang1.png（256KB） + qrc:/whale/rua.gif（95KB）
//       cp 自 Stelarith-viewer-qt/resources/whale/（源未动）
// 行为对齐 viewer/qml/components/WhaleMascot.qml（127 行）：
//   · 呼吸浮动：y 从 -amp 到 +amp 无限循环
//   · 点击 rua：scale 1.0→1.12(120ms) → 1.0(260ms, OutBack)，同时气泡 "rua～"
//   · 气泡淡入淡出 + 定时收起
//   · speak(text) / rua() 两个对外方法
//
// ⚠️ 不对外绑 bubble：组件内部点击会写 bubble，外部再绑表达式 = 双向写同一属性
//    = Binding loop（QML 规矩）。台词交组件自管 —— 管理端踩过这个坑，照搬。
Item {
    id: whale

    property var theme
    property bool light: theme ? theme.light : false
    property string imagePath: "qrc:/whale/DSniang1.png"
    property int px: 40                    // 头像边长
    property real floatAmplitude: 3        // 呼吸幅度
    property int speakMs: 3000             // 气泡默认存活
    property string bubble: ""             // ⚠️ 不要外部绑表达式

    readonly property bool bubbleVisible: bubble !== ""

    implicitWidth: px
    implicitHeight: px

    signal clicked()

    function speak(text) {
        whale.bubble = text
        bubbleTimer.interval = whale.speakMs
        bubbleTimer.restart()
    }
    function rua() {
        ruaAnim.start()
        speak("rua～")
    }

    // ── 头像 ────────────────────────────────────────────────
    Image {
        id: avatar
        anchors.centerIn: parent
        width: whale.px
        height: whale.px
        source: whale.imagePath
        fillMode: Image.PreserveAspectFit
        smooth: true
        transformOrigin: Item.Bottom

        // 呼吸浮动
        SequentialAnimation on y {
            running: true
            loops: Animation.Infinite
            NumberAnimation {
                from: parent.height / 2 - whale.px / 2 - whale.floatAmplitude
                to:   parent.height / 2 - whale.px / 2 + whale.floatAmplitude
                duration: 1600
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: parent.height / 2 - whale.px / 2 + whale.floatAmplitude
                to:   parent.height / 2 - whale.px / 2 - whale.floatAmplitude
                duration: 1600
                easing.type: Easing.InOutSine
            }
        }
    }

    // rua 弹一下
    SequentialAnimation {
        id: ruaAnim
        NumberAnimation {
            target: avatar; property: "scale"
            to: 1.12; duration: 120; easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: avatar; property: "scale"
            to: 1.0; duration: 260; easing.type: Easing.OutBack
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            whale.rua()
            whale.clicked()
        }
    }

    // ── 台词气泡 ────────────────────────────────────────────
    Rectangle {
        id: bubbleBox
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top
        anchors.bottomMargin: 2
        visible: whale.bubbleVisible
        width: bubbleLabel.implicitWidth + 14
        height: bubbleLabel.implicitHeight + 6
        radius: height / 2
        color: whale.light ? "#FFFFFF" : "#2A2A2A"
        border.width: 1
        border.color: whale.light ? "#E0E0E0" : "#3C3C3C"
        opacity: whale.bubbleVisible ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
        }

        Text {
            id: bubbleLabel
            anchors.centerIn: parent
            text: whale.bubble
            color: whale.light ? "#161616" : "#FAFAFA"
            font.pixelSize: 11
        }
    }

    Timer {
        id: bubbleTimer
        interval: whale.speakMs
        repeat: false
        onTriggered: whale.bubble = ""
    }
}
