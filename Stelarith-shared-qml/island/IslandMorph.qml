import QtQuick 2.15
import "."

// 灵动岛 · 6 形态渲染层（2026-10-27 收口蓝图）
//
// 正交叠加在 faceModel 的 6 功能切面之上：切面是导航、形态是状态表达。
// 本层只在 morphMode !== "none" 时接管收起态胶囊的内容（由父层 islandMorphOn 控制 visible）。
//
// ⚠️ 铁律（§10.9）：本文件**不解析 JSON、不联网、不算业务** —— 所有数据来自
//    bridge.morphData（C++ 算好的强类型字典），只画 + 把按钮点击转成 bridge.morphAction(cmd)。
//
// 形态：progress(进度环) / countdown(倒计时) / alert(拦截告警) / marquee(跑马灯)
//       / audio(声波) / dock(快捷坞) / notice(静态通知)
// 优先级（C++ recomputeMorph 单点裁决）：countdown > alert > notice > marquee > progress > audio

Item {
    id: morph

    // ⚠️ 同 IslandRoot：不能声明名为 bridge 的属性，会自指成 null。
    readonly property var bridgeRef: (typeof bridge !== "undefined" && bridge) ? bridge : null
    readonly property string mode: bridgeRef ? (bridgeRef.morphMode || "none") : "none"
    readonly property var   mdata: bridgeRef ? (bridgeRef.morphData || {}) : ({})
    readonly property bool  expanded: root._expanded
    readonly property var   th: root.th
    // 强调色（不依赖主题是否暴露 accent，避免 strokeStyle 取 undefined）
    readonly property string accent: morph.th.light ? "#1a73e8" : "#4f9dff"
    readonly property string track: morph.th.light ? "rgba(0,0,0,0.12)" : "rgba(255,255,255,0.16)"
    property real _beat: 0           // 声波动画节拍（Timer 推进，供竖线高度绑定）

    function iconForMode(m) {
        if (m === "progress")   return "download"
        if (m === "countdown")  return "clock"
        if (m === "alert")      return "shield"
        if (m === "marquee")    return "megaphone"
        if (m === "audio")      return "mic"
        if (m === "dock")       return "zap"
        if (m === "notice")     return "bell"
        return "bell"
    }
    function formatMMSS(sec) {
        sec = Math.max(0, Math.floor(sec))
        var h = Math.floor(sec / 3600)
        var m = Math.floor((sec % 3600) / 60)
        var s = sec % 60
        var mm = (m < 10 ? "0" : "") + m
        var ss = (s < 10 ? "0" : "") + s
        return h > 0 ? (h + ":" + mm + ":" + ss) : (mm + ":" + ss)
    }
    function collapsedText() {
        switch (morph.mode) {
        case "progress":  return "接收中 " + Math.round((morph.mdata.bytes||0)/(morph.mdata.total||1)*100) + "%"
        case "countdown": return morph.formatMMSS(morph.mdata.remainSec||0)
        case "alert":     return morph.mdata.title || "已拦截应用"
        case "marquee":   return "通知"
        case "audio":     return "语音对讲中"
        case "dock":      return "快捷操作"
        case "notice":    return morph.mdata.title || "收到通知"
        }
        return ""
    }

    // 弹性过渡：形态切换 / 展开收起都走 OutBack，呼应灵动岛「果冻感」。
    Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    // 声波竖线动画节拍：仅在 audio 形态推进，驱动竖线高度重算
    Timer {
        interval: 80; running: morph.mode === "audio"; repeat: true
        onTriggered: morph._beat += 80
    }

    // ════════════════════════════════════════════════════════════
    // 共用：左侧图标（收起态）
    // ════════════════════════════════════════════════════════════
    SvgIcon {
        id: mIcon
        visible: !morph.expanded && morph.mode !== "progress" && morph.mode !== "audio"
        name: morph.iconForMode(morph.mode)
        tint: morph.th.fg
        px: 22
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        id: mText
        visible: !morph.expanded && mIcon.visible && morph.collapsedText() !== ""
        text: morph.collapsedText()
        color: morph.th.fg
        font.pixelSize: morph.th.fsTitle
        font.weight: Font.Bold
        elide: Text.ElideRight
        anchors.left: mIcon.right
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 42
        anchors.verticalCenter: parent.verticalCenter
    }

    // ════════════════════════════════════════════════════════════
    // 1) 进度环形态（文件下发 / OTA）
    //    收起态：图标绕环形进度；展开态：文件名 + 速度 + 暂停/取消
    // ════════════════════════════════════════════════════════════
    Canvas {
        id: ring
        visible: morph.mode === "progress"
        anchors.fill: parent
        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var cx = 14 + 15, cy = height / 2, r = 13
            var total = morph.mdata.total || 0
            var p = total > 0 ? Math.min(1, (morph.mdata.bytes||0) / total) : 0
            ctx.lineWidth = 3
            ctx.strokeStyle = morph.track
            ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI*2); ctx.stroke()
            ctx.strokeStyle = morph.accent
            ctx.beginPath(); ctx.arc(cx, cy, r, -Math.PI/2, -Math.PI/2 + p*Math.PI*2); ctx.stroke()
        }
        // 进度变化 / 展开态变化都要重绘
        Connections {
            target: morph.bridgeRef
            function onChanged() { ring.requestPaint() }
        }
        Component.onCompleted: ring.requestPaint()

        Text {
            visible: morph.expanded
            text: (morph.mdata.name || "文件") + "\n" +
                  Math.round((morph.mdata.bytes||0)/1048576) + " / " +
                  Math.round((morph.mdata.total||0)/1048576) + " MB  ·  " +
                  (morph.mdata.speedKbps||0) + " KB/s"
            color: morph.th.fg
            font.pixelSize: 13
            wrapMode: Text.WrapAnywhere
            anchors.left: parent.left
            anchors.leftMargin: 42
            anchors.top: parent.top
            anchors.topMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 14
        }
        Row {
            visible: morph.expanded
            spacing: 10
            anchors.left: parent.left
            anchors.leftMargin: 42
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            ActionButton {
                theme: morph.th; light: morph.th.light; role: "primary"
                label: "暂停"; onClicked: morph.bridgeRef.morphAction("pause")
            }
            ActionButton {
                theme: morph.th; light: morph.th.light; role: "secondary"
                label: "取消"; onClicked: morph.bridgeRef.morphAction("cancel")
            }
        }
    }

    // ════════════════════════════════════════════════════════════
    // 2) 倒计时 / 沙漏形态（考试 / 定时锁屏）
    //    收起态：沙漏图标 + MM:SS；展开态：提示文案
    // ════════════════════════════════════════════════════════════
    Text {
        visible: morph.mode === "countdown" && !morph.expanded
        text: morph.formatMMSS(morph.mdata.remainSec||0)
        color: morph.th.fg
        font.pixelSize: 18
        font.weight: Font.Bold
        anchors.left: mIcon.right
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
    }
    Column {
        visible: morph.mode === "countdown" && morph.expanded
        anchors.left: parent.left
        anchors.leftMargin: 42
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 14
        Text {
            text: morph.mdata.label || "倒计时"
            color: morph.th.fg
            font.pixelSize: 15
            font.weight: Font.Bold
        }
        Text {
            text: morph.mdata.sub || ""
            color: morph.th.sub
            font.pixelSize: 13
            topPadding: 4
        }
    }

    // ════════════════════════════════════════════════════════════
    // 3) 拦截告警形态（仅被控端；3 秒后转灰暗）
    // ════════════════════════════════════════════════════════════
    Column {
        visible: morph.mode === "alert" && morph.expanded
        anchors.left: parent.left
        anchors.leftMargin: 42
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 14
        Text {
            text: morph.mdata.title || "已拦截应用"
            color: "#ff6b6b"
            font.pixelSize: 15
            font.weight: Font.Bold
        }
        Text {
            text: (morph.mdata.apps ? morph.mdata.apps.join("、") : "") + "\n" + (morph.mdata.reason || "")
            color: morph.th.sub
            font.pixelSize: 13
            topPadding: 4
            wrapMode: Text.WrapAnywhere
        }
    }

    // ════════════════════════════════════════════════════════════
    // 4) 跑马灯形态（长文本 / 紧急广播）—— 收起态滚动，展开态停滚
    // ════════════════════════════════════════════════════════════
    MarqueeText {
        visible: morph.mode === "marquee"
        text: morph.mdata.text || ""
        theme: morph.th
        paused: morph.expanded       // 展开态停滚看全文
        px: morph.th.fsTitle
        bold: true
        speed: 50                    // 50px/s，防晃眼
        anchors.left: mIcon.right
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 42
        anchors.verticalCenter: parent.verticalCenter
    }
    Item {
        visible: morph.mode === "marquee" && morph.expanded
        anchors.fill: parent
        Text {
            id: marqueeFull
            text: morph.mdata.text || ""
            color: morph.th.fg
            font.pixelSize: 14
            wrapMode: Text.WrapAnywhere
            anchors.left: parent.left
            anchors.leftMargin: 42
            anchors.top: parent.top
            anchors.topMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 44
        }
        ActionButton {
            theme: morph.th; light: morph.th.light; role: "primary"
            label: "确认收到"
            anchors.left: parent.left
            anchors.leftMargin: 42
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            onClicked: morph.bridgeRef.morphAction("confirm")
        }
    }

    // ════════════════════════════════════════════════════════════
    // 5) 声波形态（语音对讲）—— 收起态 3 根跳动竖线
    // ════════════════════════════════════════════════════════════
    Row {
        id: audioBars
        visible: morph.mode === "audio" && !morph.expanded
        spacing: 4
        anchors.left: mIcon.right
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            model: 3
            Rectangle {
                width: 4
                radius: 2
                color: morph.accent
                // 有真实频谱（bridge.spectrumBands 前 3 带）就用它；否则用 _beat 动画兜底
                height: {
                    var b = morph.bridgeRef ? morph.bridgeRef.spectrumBands : []
                    var v = (b && b.length > index) ? b[index] : 0
                    if (!(v > 0)) v = Math.abs(Math.sin(morph._beat/200 + index)) * 0.8 + 0.2
                    return 8 + v * 18
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════════
    // 6) 快捷操作坞形态（展开态横排 4 枚大图标按钮）
    // ════════════════════════════════════════════════════════════
    Row {
        visible: morph.mode === "dock"
        spacing: 18
        anchors.centerIn: parent
        Repeater {
            model: morph.mdata.actions || []
            Item {
                width: 56; height: 56
                SvgIcon {
                    name: modelData.icon || "zap"
                    tint: morph.th.fg
                    px: 28
                    anchors.centerIn: parent
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: morph.bridgeRef.morphAction(modelData.cmd || "")
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════════
    // 7) 静态通知形态（notice）—— 展开态完整文案 + 确认
    // ════════════════════════════════════════════════════════════
    Column {
        visible: morph.mode === "notice" && morph.expanded
        anchors.left: parent.left
        anchors.leftMargin: 42
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 14
        Text {
            text: morph.mdata.title || "通知"
            color: morph.th.fg
            font.pixelSize: 15
            font.weight: Font.Bold
        }
        Text {
            text: morph.mdata.desc || ""
            color: morph.th.sub
            font.pixelSize: 13
            topPadding: 4
            wrapMode: Text.WrapAnywhere
        }
        ActionButton {
            theme: morph.th; light: morph.th.light; role: "primary"
            label: "知道了"
            onClicked: morph.bridgeRef.morphAction("confirm")
        }
    }
}
