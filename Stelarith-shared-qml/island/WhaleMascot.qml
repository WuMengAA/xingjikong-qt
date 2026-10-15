import QtQuick 2.15

// 鲸鱼娘（2026-10-11 · dsh-whale-widget 移植版）
//
// 从 dsh-whale-widget 移植到被控端灵动岛右下角。
// 与上一版的区别（上一版只在展开态左上角）：
//   · 位置 → 灵动岛**右下角**，所有形态（空闲/收起/展开/居中/全屏）都在
//   · 点击 → rua 动画 + 按压音效（ya1.wav）+ 气泡 "rua～"
//   · 长按 → 松开音效（ya2.wav）+ 随机台词气泡
//   · 通知来时 → 气泡冒出通知标题（"标题复读机"），气泡指向鲸鱼
//   · 鲸鱼呼吸浮动 + rua 弹性动画保留
//
// ⚠️ 不对外绑 bubble：组件内部点击/通知会写 bubble，外部再绑表达式 = 双向写
//    = Binding loop。台词交组件自管（管理端踩过这个坑，照搬）。
//
// 音效走 C++ WhaleAudio（winmm PlaySound，不依赖 Qt Multimedia）：
//   bridge.whaleClicked()       → 播 ya1.wav
//   bridge.whaleLongPressed()   → 播 ya2.wav
Item {
    id: whale

    property var theme
    property bool light: theme ? theme.light : false
    property string imagePath: "qrc:/whale/dsniang1.png"
    // 鲸鱼尺寸：比上一版大一点（dsh 默认 scale=1.5，这里取 44px）
    property int px: 44
    property real floatAmplitude: 3        // 呼吸幅度
    property int speakMs: 3000             // 气泡默认存活
    property string bubble: ""             // ⚠️ 不要外部绑表达式
    // 桥引用（通知标题复读机用）
    property var bridgeRef: null
    // 是否显示鲸鱼（空闲态也可以显示，但更小更淡）
    property bool showWhale: true

    readonly property bool bubbleVisible: bubble !== ""

    // 随机台词池（长按时从这里抽一条）
    readonly property var phrases: [
        "rua～",
        "咕噜～",
        "我在呢！",
        "有什么事？",
        "鲸鲸～",
        "收到收到！",
        "嗯哼？",
        "blub blub～",
        "在在在！",
        "鲸鱼在线～"
    ]

    implicitWidth: px
    implicitHeight: px

    signal clicked()
    signal longPressed()

    // 长按是否已触发（区分短按和长按）
    // ⚠️ 不能用 longPressTimer.running 判短按：
    //    onReleased 先于 onClicked 执行，已经 stop() 了 timer，
    //    到 onClicked 时 running 恒 false ⇒ 短按全废。
    property bool _longFired: false

    function speak(text) {
        whale.bubble = text
        bubbleTimer.interval = whale.speakMs
        bubbleTimer.restart()
    }
    function rua() {
        ruaAnim.start()
        speak("rua～")
    }
    function speakTitle(title) {
        // "标题复读机"：通知来时冒出标题文字
        if (title && title.length > 0) {
            speak(title)
            bubbleTimer.interval = Math.max(whale.speakMs, title.length * 200)
            bubbleTimer.restart()
        }
    }
    function randomPhrase() {
        var idx = Math.floor(Math.random() * phrases.length)
        return phrases[idx]
    }

    // ── 通知标题复读机 ────────────────────────────────────────
    // bridge.whaleBubble 变了（= 新通知上屏）→ 鲸鱼冒出标题
    Connections {
        target: whale.bridgeRef
        function onChanged() {
            if (!whale.bridgeRef) return
            var b = whale.bridgeRef.whaleBubble
            if (b && b.length > 0) {
                whale.speakTitle(b)
            }
        }
        // 忽略 ignoreUnknownSignals
    }

    // ── 头像 ────────────────────────────────────────────────
    // ⚠️ 不用 anchors.centerIn + SequentialAnimation on y：
    //    anchors.centerIn 绑定 y，动画也写 y，同一属性两个来源。
    //    改为 x 手算居中，y 交给呼吸动画。
    Image {
        id: avatar
        x: (parent.width - width) / 2
        width: whale.px
        height: whale.px
        source: whale.imagePath
        fillMode: Image.PreserveAspectFit
        smooth: true
        transformOrigin: Item.Bottom

        // 呼吸浮动（y 由动画驱动，不用 anchors）
        SequentialAnimation on y {
            running: true
            loops: Animation.Infinite
            NumberAnimation {
                from: whale.height / 2 - whale.px / 2 - whale.floatAmplitude
                to:   whale.height / 2 - whale.px / 2 + whale.floatAmplitude
                duration: 1600
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                from: whale.height / 2 - whale.px / 2 + whale.floatAmplitude
                to:   whale.height / 2 - whale.px / 2 - whale.floatAmplitude
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
            to: 1.15; duration: 120; easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: avatar; property: "scale"
            to: 1.0; duration: 260; easing.type: Easing.OutBack
        }
    }

    // ── 点击 + 长按 ────────────────────────────────────────────
    // ⚠️ 鲸鱼 MouseArea 独立于岛的 gesture MouseArea：
    //    鲸鱼自己管 press/release/longPress，事件不该冒泡到岛的"展开/收起"。
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        preventStealing: true       // 防止岛的拖拽手势偷走鲸鱼的 press
        propagateComposedEvents: false

        // 长按检测
        Timer {
            id: longPressTimer
            interval: 800
            repeat: false
            onTriggered: {
                whale._longFired = true
                if (whale.bridgeRef && whale.bridgeRef.whaleLongPressed)
                    whale.bridgeRef.whaleLongPressed()
                whale.ruaAnim.start()
                whale.speak(whale.randomPhrase())
                whale.longPressed()
            }
        }

        onPressed: {
            whale._longFired = false
            longPressTimer.start()
        }

        onReleased: {
            longPressTimer.stop()
        }

        onClicked: {
            // 长按已处理过就不重复
            if (!whale._longFired) {
                // 短按 → rua + 按压音
                if (whale.bridgeRef && whale.bridgeRef.whaleClicked)
                    whale.bridgeRef.whaleClicked()
                whale.rua()
                whale.clicked()
            }
        }
    }

    // ── 台词气泡（通知标题复读机 / rua 台词）──────────────────
    // 气泡指向鲸鱼（从鲸鱼头顶冒出）。
    // ⚠️ 宽度上限不能取 parent.width * 0.8 —— parent 是 WhaleMascot 自己，
    //    parent.width = px = 28~44px，气泡比鲸鱼还窄。
    //    气泡要承载通知标题，宽度由内容决定，加一个绝对上限 280px 防溢出岛外。
    Rectangle {
        id: bubbleBox
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.top
        anchors.bottomMargin: 2
        visible: whale.bubbleVisible
        width: Math.min(bubbleLabel.implicitWidth + 16, 280)
        height: bubbleLabel.implicitHeight + 8
        radius: height / 2
        color: whale.light ? "#FFFFFF" : "#2A2A2A"
        border.width: 1
        border.color: whale.light ? "#E0E0E0" : "#3C3C3C"
        opacity: whale.bubbleVisible ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 180; easing.type: Easing.OutQuad }
        }
        Behavior on height { NumberAnimation { duration: 120 } }
        Behavior on width  { NumberAnimation { duration: 120 } }

        // 气泡小尾巴（指向鲸鱼）
        Canvas {
            anchors.top: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: 8; height: 5
            onPaint: {
                var c = getContext("2d")
                c.reset(); c.beginPath()
                c.moveTo(0, 0); c.lineTo(width, 0); c.lineTo(width / 2, height)
                c.closePath()
                c.fillStyle = whale.light ? "#FFFFFF" : "#2A2A2A"
                c.fill()
            }
        }

        Text {
            id: bubbleLabel
            anchors.centerIn: parent
            text: whale.bubble
            color: whale.light ? "#161616" : "#FAFAFA"
            font.pixelSize: 12
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            elide: Text.ElideRight
            maximumLineCount: 2
        }
    }

    Timer {
        id: bubbleTimer
        interval: whale.speakMs
        repeat: false
        onTriggered: whale.bubble = ""
    }
}
