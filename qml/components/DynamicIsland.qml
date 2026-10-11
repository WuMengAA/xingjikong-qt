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

    // ── 瞬态提示（2026-10-09 · 一切通知统一走灵动岛）──────────────────────
    // notifyPref（云端状态通知）/ toast / hint 都经 showNote 在此显示，
    // 几秒后自动收起；与 states（常驻状态聚合）正交：note 激活时胶囊显示 note，
    // states 照常重算、互不干扰。被控端侧也是灵动岛统一接管各类瞬态提示。
    property string noteTitle: ""
    property string noteDesc: ""
    property string noteIcon: ""
    property bool noteActive: false
    Timer {
        id: noteTimer
        onTriggered: isl.noteActive = false
    }

    readonly property int n: isl.states ? isl.states.length : 0
    // note 优先：激活时把 note 当主状态（图标用 lucide 名，不走状态键映射）
    readonly property var st: (isl.noteActive
        ? { k: isl.noteIcon || "bell", t: isl.noteTitle, d: isl.noteDesc, p: -1 }
        : (isl.n > 0 ? isl.states[0] : { k: "", t: "", d: "", p: -1 }))
    // 收起态角标：主状态之外还有几项没做完（"锁屏 42/60 +2 🔔" 里的那个 +2）
    readonly property int rest: Math.max(0, isl.n - 1)
    readonly property string mode: ((isl.n === 0 && !isl.noteActive) ? "hidden"
        : (isl.expanded && !isl.noteActive ? "expanded" : "collapsed"))

    property bool expanded: false

    // ── 视觉对齐被控端灵动岛（2026-10-09）──────────────────────────
    // 被控端 IslandOverlay 胶囊配色（半透明浮层，深浅两套）与果冻动画，
    // 这里把管理端胶囊的色相 / 圆角 / 缓动对齐，不改形态逻辑（无通知仍消失）。
    readonly property string islandFg3: isl.theme.dark ? "#C8C8C8" : "#6E6E6E"
    readonly property string islandFg4: isl.theme.dark ? "#909090" : "#8A8A8A"

    // 几何弹性：对齐被控端 QPropertyAnimation(geometry, OutBack, 380ms)
    Behavior on width  { NumberAnimation { duration: 380; easing.type: Easing.OutBack } }
    Behavior on height { NumberAnimation { duration: 380; easing.type: Easing.OutBack } }

    // 尺寸：hidden 彻底消失（不是"看不见"，是真的不占地方）
    width:  isl.mode === "hidden" ? 0 : (isl.mode === "expanded" ? 400 : isl.capW)
    height: isl.mode === "hidden" ? 0 : (isl.mode === "expanded" ? isl.bodyH : 36)
    // capW：收起态胶囊宽。F4 加了时针（46px）后由 268 抬到 320，
    // 保证「图标 + 状态文本 + 时钟 + 展开」四段都排得下（文本区 width 见 capMarquee）。
    readonly property int capW: 320
    // bodyH：展开态总高。F4 把头部从 18 → 42（腾出鲸鱼娘 40px 的位置），
    // 所以头部预算由 44 抬到 68；仍受 300 上限约束（状态多时不无限长）。
    readonly property int bodyH: Math.min(300, 68 + Math.max(1, isl.n) * 34 + 30)

    // 状态键 → 图标名（lucide/morphicons 同款 24×24 stroke SVG，见 components/SvgIcon.qml）
    readonly property var glyph: ({
        offline: "ban", command: "circle-dashed", file: "layout-grid", alert: "triangle", monitor: "circle",
        voice: "music", broadcast: "square", bell: "bell"
    })

    // ── 胶囊本体 ───────────────────────────────────────────────────
    Rectangle {
        id: cap
        anchors.centerIn: parent
        width: isl.width
        height: isl.height
        radius: 24
        // 悬浮层：深色用边框、浅色才需要阴影（5.4「浅色模式用阴影，深色模式用边框」）
        // 对齐被控端胶囊底/边框（深浅两套，半透明浮层质感）
        color: isl.theme.dark ? "#141414E6" : "#FAFAFAEB"
        border.color: isl.theme.dark ? "#3A3A3A" : "#C8C8C8"
        border.width: 1
        opacity: isl.mode === "hidden" ? 0 : 1
        scale: isl.mode === "hidden" ? 0.86 : 1

        // 对齐被控端 fade(220ms, 默认线性) / 几何 scale(OutBack, 380ms)
        Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.Linear } }
        Behavior on scale   { NumberAnimation { duration: 380; easing.type: Easing.OutBack } }

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
                SvgIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: isl.noteActive ? (isl.noteIcon || "bell") : (isl.glyph[isl.st.k] || "circle")
                    tint: isl.theme.fg
                    width: 16
                    height: 16
                }
                // ── F4（2026-10-11）：状态文本走 MarqueeText（超长横向滚动）──
                // 原来用 Text + elide 截断，长状态（如长文件路径/长设备名）看不全。
                // MarqueeText 只在文本宽 > 容器宽时才滚，短文本与原来表现一致。
                MarqueeText {
                    id: capMarquee
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 140     // 给左侧图标/右侧时钟+「展开」留位
                    height: 20
                    text: (isl.noteActive ? "" : (isl.n > 1 ? (isl.n + " 项：") : "")) + isl.st.t
                    light: !isl.theme.dark
                    // note 激活时暂停滚动（提示要立刻看清，别一边滚一边读）
                    paused: isl.noteActive
                }
                Item { width: 4 }
                // ── F4：时钟（HH:mm）── 收起态右侧常显，抬头即知时间
                IslandClock {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 46
                    height: 18
                    timePx: 13
                    light: !isl.theme.dark
                }
                Text {
                    // ⚠️ 2026-10-07 删了 anchors.right: parent.right ——
                    // Row 是定位器，子项不许自己指定 left/right/horizontalCenter/fill/centerIn
                    // （真机日志每次启动刷十几条 "Row will not function"）。
                    // 横向位置交给 Row 自己排；"展开"本来就是这一行的最后一个，删掉也一样在末尾。
                    anchors.verticalCenter: parent.verticalCenter
                    text: isl.n > 0 ? "展开" : ""
                    color: isl.islandFg4
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
                    color: isl.islandFg3
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

            // ── F4（2026-10-11）：鲸鱼娘上岛（展开态左上角）──
            // 用户最初需求"添加鲸鱼娘"。素材 qrc:/whale/DSniang1.png（随 exe 走）。
            // 放展开态而非收起态：收起态只有 36px 高、宽度也紧，塞角色会挤压状态文字。
            // 40px 见方、带呼吸浮动；点一下 rua（组件内自带）。
            Item {
                width: parent.width
                height: 42
                WhaleMascot {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 40
                    light: !isl.theme.dark
                    imagePath: "qrc:/whale/DSniang1.png"
                    // ⚠️ 不在这儿绑 bubble：WhaleMascot 内部点击时自己写 bubble="rua～"，
                    //    外部再绑一个表达式 ⇒ 双向写同一属性 = Binding loop（QML 规矩）。
                    //    台词交给组件自管（点击 rua），父级只给主题 + 素材路径。
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 50
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
                            SvgIcon {
                                // ⚠️ 2026-10-11（F4 复核）：原来这里是裸 Text 直接画
                                //    isl.glyph[...]（图标**名字符串**）⇒ 展开态里出现
                                //    "circle"/"layout-grid"/"triangle" 这样的明文。
                                //    收起态用的是 SvgIcon（画图标），两处不一致；此处改回
                                //    SvgIcon，与收起态同源同风格。
                                anchors.verticalCenter: parent.verticalCenter
                                name: isl.glyph[modelData.k] || "circle"
                                tint: (index === 0) ? (isl.theme.win || "#0A0A0A") : isl.islandFg4
                                width: 16
                                height: 16
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
                                    color: (index === 0) ? (isl.theme.win || "#0A0A0A") : isl.islandFg3
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

    // 瞬态提示：一切通知（云端状态 / toast / hint）统一走这 —— 顶部胶囊、自动收起。
    function showNote(title, desc, icon, ms) {
        isl.noteTitle = title || ""
        isl.noteDesc = desc || ""
        isl.noteIcon = icon || "bell"
        isl.noteActive = true
        noteTimer.interval = (ms > 0 ? ms : 3000)
        noteTimer.restart()
    }

    // 点按钮/别处操作时，主动收回去（别让它赖在屏幕上挡画面）
    function collapse() { isl.expanded = false }
}
