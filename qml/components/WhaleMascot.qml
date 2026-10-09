// 灵动岛 · DeepSeek 鲸鱼娘角色（2026-10-09 · 用户最初需求"添加鲸鱼娘"独立组件）
//
// 背景：设计文档标记"鲸鱼娘原型 dsh-whale-widget 未被任何端实现"——开放需求。
// 本组件提供 QML 版鲸鱼娘角色：图 + 呼吸浮动 + 气泡说话 + 点击 rua。
//
// 素材（默认引用原型包，接入时拷进 viewer-qt 资源或改 path 指本地）：
//   DSH2.png     2048x1024（横幅，全宽用）
//   DSniang1.png 610x610（方形，角色卡用，推荐）
//   rua.gif      动图（点击互动）
// 音效：D1.mp3 / D2.mp3（可选，接入时决定播不播）
//
// 行为：
//   · 呼吸浮动：y 方向 ±3px 正弦缓动（灵动感，代价可忽略）
//   · 气泡：speak(text) 显示气泡 3s 渐隐；空文本隐藏
//   · 点击：触碰后 rua 一下（瞬时缩放弹一下），可连击
//   · 透明背景：只画图 + 气泡，不占方块
//
// 用法（等 Main.qml 工作区解禁后，可嵌进 DynamicIsland 或独立角落）：
//   WhaleMascot { width: 80; height: 80;
//                 imagePath: "qrc:/assets/DSniang1.png"; light: false }
import QtQuick

Item {
    id: whale

    property string imagePath: "C:/Users/Administrator/.dsh/profiles/web/node_modules/dsh-whale-widget/assets/DSniang1.png"
    property bool light: false
    property real floatAmplitude: 3        // 呼吸浮动幅度 px
    property int speakMs: 3000             // 气泡停留

    // 气泡文字（对外接口）
    property string bubble: ""
    property bool bubbleVisible: bubble !== ""

    // ── 角色图（保持纵横比，透明背景）──
    Image {
        id: img
        anchors.fill: parent
        source: whale.imagePath
        fillMode: Image.PreserveAspectFit
        smooth: true
        cache: false
        antialiasing: true
    }

    // 呼吸浮动：y 正弦缓动（用 NumberAnimation 循环，比手动 tick 省事且平滑）
    NumberAnimation on y {
        from: -whale.floatAmplitude
        to: whale.floatAmplitude
        duration: 2600
        easing.type: Easing.InOutSine
        loops: Animation.Infinite
        running: true
    }

    // rua 互动：点击瞬时放大再回弹
    ParallelAnimation {
        id: ruaAnim
        NumberAnimation { target: img; property: "scale"; from: 1.0; to: 1.12; duration: 120 }
        NumberAnimation { target: img; property: "scale"; from: 1.12; to: 1.0; duration: 260; easing.type: Easing.OutBack }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            ruaAnim.start()
            whale.bubble = "rua～"          // 每次点击冒一句
            bubbleTimer.restart()
        }
    }

    // ── 气泡（右上角冒出）──
    Item {
        anchors.right: parent.right
        anchors.bottom: parent.top
        anchors.rightMargin: -4
        anchors.bottomMargin: 4
        width: bubbleText.width + 16
        height: bubbleText.height + 10
        visible: whale.bubbleVisible && bubbleText.text !== ""
        opacity: bubbleText.text === "" ? 0 : 1

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: whale.light ? "#F0F0F0" : "#2A2A2A"
            border.color: whale.light ? "#D0D0D0" : "#3A3A3A"
            border.width: 1
        }
        Text {
            id: bubbleText
            anchors.centerIn: parent
            text: whale.bubble
            color: whale.light ? "#1E1E1E" : "#F5F5F5"
            font.pixelSize: 11
        }

        // 气泡渐隐
        NumberAnimation on opacity {
            id: bubbleFade
            from: 1.0; to: 0.0
            duration: 400
            easing.type: Easing.InOutQuad
            running: false
        }
    }

    Timer {
        id: bubbleTimer
        interval: whale.speakMs
        onTriggered: {
            bubbleFade.start()
            whale.bubble = ""               // 清空文本让气泡隐藏
        }
    }

    // 对外：说一句话（与 bubble 属性等价，语义更明确）
    function speak(text) {
        whale.bubble = text
        bubbleFade.stop()
        whale.bubbleVisible = true
        bubbleTimer.restart()
    }
    function rua() { ruaAnim.start() }
}