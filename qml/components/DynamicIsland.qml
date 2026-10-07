// 星集控 · 灵动岛（2026-10-06 补）
//
// 设计依据《星集控 — 全量设计文档（整理合并版）》3.8：
//   「灵动岛是**状态聚合层**，不是新功能」——把散落在各处的瞬时状态（连接、批量指令、
//   文件分发、告警、屏幕监控）聚成一个顶部居中的胶囊，空闲彻底消失。
//   位置：TopBar 下方、内容区上方，水平居中；三态 hidden / collapsed / expanded。
//   优先级：连接断开 > 批量指令 > 文件 > 告警 > 监控（语音这一档本项目暂无数据源，不编）。
//
// 分工：状态**由谁有、由谁算**是主界面的事（主界面知道设备表和回执），
//   组件只负责「收得下、排得对、画得像」：states 传进来（已按优先级排好），
//   states[0] 就是主状态，其余在展开态里列出来。
//
// 视觉沿用黑白稿：不引入彩色，唯一强调手段＝反白；动效只走 opacity / scale（5.8 的规矩）。
// ⚠️ 唯一一处破例是那条 3px 高的进度条（3.8.4 明确要求"进度条宽度按 completed/total"）——
//    它是文档点名的元素，且只有一个小矩形、按台数整段跳变，不是每帧重排，代价可以忽略。
import QtQuick

Item {
    id: isl

    property var theme: ({ })      // 主题令牌，由主界面给（和 Card / CommandPalette 同名）
    property var states: ([])      // [{ k, t, d, p }]：k 状态键 / t 标题 / d 详情 / p 进度 0..1

    readonly property int n: isl.states ? isl.states.length : 0
    readonly property var st: (isl.n > 0 ? isl.states[0] : { k: "", t: "", d: "", p: -1 })
    // 收起态角标：主状态之外还有几项没做完（"锁屏 42/60 +2 🔔" 里的那个 +2）
    readonly property int rest: Math.max(0, isl.n - 1)
    readonly property string mode: (isl.n === 0 ? "hidden" : (isl.expanded ? "expanded" : "collapsed"))

    property bool expanded: false

    // 尺寸：hidden 彻底消失（不是"看不见"，是真的不占地方）
    width:  isl.mode === "hidden" ? 0 : (isl.mode === "expanded" ? 400 : isl.capW)
    height: isl.mode === "hidden" ? 0 : (isl.mode === "expanded" ? isl.bodyH : 36)
    readonly property int capW: 268
    readonly property int bodyH: Math.min(300, 44 + Math.max(1, isl.n) * 34 + 30)

    // 状态键 → 符号（黑白稿里图标也只能是描边符号，不能上彩色）
    readonly property var glyph: ({
        offline: "⊘", command: "◐", file: "▦", alert: "▲", monitor: "◉",
        voice: "♪", broadcast: "▣"
    })

    // ── 胶囊本体 ───────────────────────────────────────────────────
    Rectangle {
        id: cap
        anchors.centerIn: parent
        width: isl.width
        height: isl.height
        radius: (isl.mode === "expanded" ? 14 : 18)
        // 悬浮层：深色用边框、浅色才需要阴影（5.4「浅色模式用阴影，深色模式用边框」）
        color: isl.theme.cream || "#141414"
        border.color: isl.theme.card || "#242424"
        border.width: 1
        opacity: isl.mode === "hidden" ? 0 : 1
        scale: isl.mode === "hidden" ? 0.86 : 1

        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on scale   { NumberAnimation { duration: 300; easing.type: Easing.OutBack; easing.overshoot: 1.05 } }

        // 收起态：一个符号 + 一句话；点一下展开
        Item {
            visible: isl.mode !== "expanded"
            anchors.fill: parent
            MouseArea {
                anchors.fill: parent
                cursorShape: isl.n > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (isl.n === 0) return
                    // 展开时先全身退开（layout 里立刻放掉那块地方），再长回来
                    isl.expanded = !isl.expanded
                }
            }
            Row {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: isl.glyph[isl.st.k] || "•"
                    color: isl.theme.fg3 || "#8A8A8A"
                    font.pixelSize: 14
                }
                Text {
                    id: capText
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 78
                    text: (isl.n > 1 ? (isl.n + " 项：") : "") + isl.st.t
                    color: isl.theme.fg || "#FAFAFA"
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                Item { width: 4 }
                Text {
                    // ⚠️ 2026-10-07 删了 anchors.right: parent.right ——
                    // Row 是定位器，子项不许自己指定 left/right/horizontalCenter/fill/centerIn
                    // （真机日志每次启动刷十几条 "Row will not function"）。
                    // 横向位置交给 Row 自己排；"展开"本来就是这一行的最后一个，删掉也一样在末尾。
                    anchors.verticalCenter: parent.verticalCenter
                    text: isl.n > 0 ? "展开" : ""
                    color: isl.theme.fg4 || "#707070"
                    font.pixelSize: 12
                }
            }
            // 进度条只在有进度的状态上出现
            // ⚠️ 2026-10-08 补 anchors.right —— 原来缺了导致外层宽度由子元素 implicit width
            // 决定，而内层 width 又依赖 parent.width，循环依赖解析为 0，进度条永远不可见。
            Rectangle {
                visible: isl.st.p !== undefined && isl.st.p >= 0
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                height: 2
                radius: 1
                color: isl.theme.stroke2 || "#2A2A2A"
                Rectangle {
                    width: parent.width * (isl.st.p >= 0 ? isl.st.p : 0)
                    height: parent.height
                    radius: 1
                    color: isl.theme.fg3 || "#8A8A8A"
                    Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                }
            }
        }

        // 展开态：状态列表 + 一句"再点一下收起"
        Column {
            visible: isl.mode === "expanded"
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6

            Item {
                width: parent.width
                height: 18
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "现在在做什么"
                    color: isl.theme.fg || "#FAFAFA"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "点一下收起"
                    color: isl.theme.fg4 || "#707070"
                    font.pixelSize: 12
                }
                MouseArea { anchors.fill: parent; onClicked: isl.expanded = false }
            }

            // ⚠️ 用 Column + Repeater 而不是 ListView：状态最多 5 条、高度早就算死在 bodyH 里，
            //    ListView 在这里只会多一层滚动语义（状态是"当下这几件事"，不是可翻的清单）。
            Column {
                width: parent.width
                spacing: 6
                Repeater {
                    model: isl.states
                    delegate: Item {
                        width: parent.width
                        height: 34
                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            color: (index === 0) ? (isl.theme.inv || "#F0F0F0") : "transparent"
                        }
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: isl.glyph[modelData.k] || "•"
                                color: (index === 0) ? (isl.theme.win || "#0A0A0A") : (isl.theme.fg4 || "#707070")
                                font.pixelSize: 14
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    text: (isl.n > 1 ? (index + 1) + ". " : "") + (modelData.t || "")
                                    color: (index === 0) ? (isl.theme.win || "#0A0A0A") : (isl.theme.fg || "#FAFAFA")
                                    font.pixelSize: 13
                                }
                                Text {
                                    visible: (modelData.d || "") !== ""
                                    text: modelData.d || ""
                                    color: (index === 0) ? (isl.theme.win || "#0A0A0A") : (isl.theme.fg3 || "#8A8A8A")
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                    width: parent.width - 34
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 点按钮/别处操作时，主动收回去（别让它赖在屏幕上挡画面）
    function collapse() { isl.expanded = false }
}
