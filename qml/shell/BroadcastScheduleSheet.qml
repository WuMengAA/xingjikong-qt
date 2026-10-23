import QtQuick 2.15

// 广播站 · 排期中心（右侧滑出 SideSheet · MD3 蓝白风 · 2026-10-22）
//
// 设计约定：
//   · 走 MD3 规范骨架（形状 / 间距 / 字阶 / 状态层 / 动效），但配色换成
//     「现代蓝色浅色」flavor（primary #2F6BFF / 浅蓝白 surface），贴合 VoiceHub 观感。
//   · 纯 QtQuick 原语实现 —— 被控端部署**不含 QtQuick.Controls 运行库**
//     （AgentPlaySchedulePage 已踩过：引 Controls 会整页解析失败），故不复用 Controls。
//   · 数据来自上下文属性 `voicehub`（AgentShell 已注入）：
//       voicehub.schedule（QVariantList，每条 = 一首排期曲）
//       voicehub.scheduleCount / voicehub.controlEnabled / voicehub.canRequest()
//       voicehub.requestSong(title, artist, platform, musicId)
//       voicehub.controlFinished(bool, msg) 信号
//     voicehub.schedule 每条字段：id sequence played isPlaying broadcastPosition
//       playDate title artist cover durationSeconds musicPlatform musicId songId
//
// 交互：
//   · 顶栏「广播站」按钮 → opened=true（由 AgentShell 控制）
//   · 三 Tab：排期 / 投稿 / 搜索（SegmentedButton）
//   · 排期 Tab：周历横条 + 列表（每首带封面/标题/歌手/时长/平台徽标/状态 Chip），长按拖拽排序
//   · 投稿 Tab：点歌表单 → voicehub.requestSong；提交结果走 controlFinished → Snackbar
//   · 搜索 Tab：本地过滤当前已加载排期（全曲库搜索接口待 VoiceHub 开放）
//
// ⚠️ 持久化状态：排期「拖拽排序」已接 VoiceHub 开放写接口
//   （POST /api/open/schedules/reorder，需 API Key 具备 schedules:write 权限）——
//   松手即把新顺序提交服务端；失败则回滚到服务端真值。
//   「投稿→排期」仍只在本地面板内生效（服务端尚无「投稿直接入排期」接口），面板不伪造「已保存」。

Item {
    id: root
    property bool opened: false
    signal closed()

    // 打开时拉一次最新排期
    onOpenedChanged: if (opened) { syncSchedule(); if (voicehub) voicehub.refreshSchedule() }

    // ── 主题：现代蓝白（MD3 flavor）─────────────────────────────────
    QtObject {
        id: st
        property color primary: "#2F6BFF"
        property color fgPrimary: "#FFFFFF"
        property color primaryContainer: "#E4ECFF"
        property color fgPrimaryContainer: "#0A3ABB"
        property color surface: "#F5F8FE"
        property color fgSurface: "#131A2E"
        property color surfaceVariant: "#E7EDF8"
        property color fgSurfaceVariant: "#5A6478"
        property color outline: "#C5D0E6"
        property color outlineVariant: "#DCE4F2"
        property color containerLowest: "#FFFFFF"
        property color containerLow: "#EEF3FC"
        property color container: "#E6EDFA"
        property color containerHigh: "#DCE6F6"
        property color containerHighest: "#D3DFF2"
        property color inverseSurface: "#2A3147"
        property color inverseOn: "#EEF3FC"
        property color ok: "#1B7A43"
        property color warn: "#A87C0A"
        property color error: "#B3261E"

        property int rXS: 4
        property int rS: 8
        property int rM: 12
        property int rL: 16
        property int rXL: 28
        property int rFull: 999

        property int spXS: 4
        property int spS: 8
        property int spM: 16
        property int spL: 24

        property int durShort: 200
        property int durMedium: 360
        property int durLong: 500
        property real stHover: 0.06
        property real stPress: 0.10

        property int headlineS: 24
        property int titleL: 22
        property int titleM: 16
        property int titleS: 14
        property int bodyL: 16
        property int bodyM: 14
        property int bodyS: 12
        property int labelM: 12
        property int labelS: 11
    }

    // 抽屉宽度：宽屏 600，窄屏取 96% 宽（避免出界）
    readonly property real drawerW: Math.min(600, root.width * 0.96)
    readonly property bool narrow: root.width < 480

    // 当前 Tab：0=排期 1=投稿 2=搜索
    property int tabIndex: 0

    // 投稿默认平台（网易云）
    property string reqPlatform: "netease"

    // 排期按日隔离（2026-10-23）：列表只显示选中日的排期，拖拽只在当日 1..N 重排，
    // 其它日 sequence 不动 —— 彻底消除「整窗多日混排 + 全量重编号」导致的跨日序号污染。
    property string selectedDate: ""        // 当前选中日 YYYY-MM-DD
    property var dateChips: []              // 可用日期（今日 ∪ 数据中出现过的日期），升序
    property bool saving: false             // 是否正在向 VoiceHub 保存新顺序（保存期间禁用再拖）

    // ── 本地排期模型（镜像 voicehub.schedule，便于拖拽重排）──────────────
    ListModel { id: scheduleModel }
    // 选中日的过滤视图：ListView 直接吃这个，拖拽/重排都只在这一日范围内
    ListModel { id: dayModel }

    function syncSchedule() {
        if (!voicehub) return
        var arr = voicehub.schedule || []
        scheduleModel.clear()
        for (var i = 0; i < arr.length; ++i) {
            var s = arr[i]
            scheduleModel.append({
                sid: s.id, seq: s.sequence,
                title: s.title || "(未知曲目)",
                artist: s.artist || "",
                cover: s.cover || "",
                dur: s.durationSeconds || 0,
                platform: s.musicPlatform || "",
                mid: s.musicId || "",
                playing: !!s.isPlaying,
                played: !!s.played,
                pos: (typeof s.broadcastPosition === "number") ? s.broadcastPosition : -1,
                playDate: s.playDate || ""          // 2026-10-23：按日隔离必需
            })
        }
        rebuildDayModel()
    }

    // 本地日期（YYYY-MM-DD），与服务端 playDate 同口径（机房均为中国时区）
    function todayStr() {
        var t = new Date()
        function p(n) { return (n < 10 ? "0" + n : "" + n) }
        return t.getFullYear() + "-" + p(t.getMonth() + 1) + "-" + p(t.getDate())
    }
    function isTodayStr(d) { return d === todayStr() }
    function fmtChip(d) {
        var p = String(d).split("-")
        if (p.length < 3) return d
        return parseInt(p[1], 10) + "/" + parseInt(p[2], 10)
    }

    // 重建日期条 + 当日过滤列表：今日永远在条上（即使无排期），其余取数据中出现的日期，升序去重
    function rebuildDayModel() {
        var seen = {}
        var list = []
        var tday = todayStr()
        if (!seen[tday]) { seen[tday] = true; list.push(tday) }
        for (var i = 0; i < scheduleModel.count; ++i) {
            var d = scheduleModel.get(i).playDate
            if (d && !seen[d]) { seen[d] = true; list.push(d) }
        }
        list.sort()
        root.dateChips = list

        // 选中日失效（数据变了）则回退到今日/首日
        if (root.selectedDate === "" || list.indexOf(root.selectedDate) < 0) {
            root.selectedDate = (list.indexOf(tday) >= 0) ? tday : (list[0] || tday)
        }

        dayModel.clear()
        for (var j = 0; j < scheduleModel.count; ++j) {
            var it = scheduleModel.get(j)
            if (it.playDate === root.selectedDate) {
                dayModel.append({
                    sid: it.sid, seq: it.seq, title: it.title, artist: it.artist,
                    cover: it.cover, dur: it.dur, platform: it.platform, mid: it.mid,
                    playing: it.playing, played: it.played, pos: it.pos, playDate: it.playDate
                })
            }
        }
    }

    function statusOf(it) {
        if (it.playing) return { text: "在播", bg: st.primaryContainer, fg: st.fgPrimaryContainer }
        if (it.played)  return { text: "已播", bg: st.container,       fg: st.fgSurfaceVariant }
        return { text: "未播", bg: st.containerLow, fg: st.fgSurfaceVariant }
    }
    function platformName(p) {
        if (p === "netease") return "网易云"
        if (p === "qq") return "QQ音乐"
        if (p === "kugou") return "酷狗"
        if (p === "local") return "本地"
        return p ? p : "未知"
    }
    function fmtDur(sec) {
        sec = Math.max(0, Math.round(sec))
        var m = Math.floor(sec / 60), s = sec % 60
        return m + ":" + (s < 10 ? "0" + s : s)
    }

    // 排期变化实时同步（voicehub 可能稍后异步到达）
    Connections {
        target: voicehub
        function onScheduleChanged() { if (root.opened) syncSchedule() }
    }

    // ════════════════════════════════════════════════════════════════
    // 遮罩（50% 黑）+ 点击关闭
    // ════════════════════════════════════════════════════════════════
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.opened ? 0.5 : 0
        visible: root.opened || scrimFade.running
        Behavior on opacity { NumberAnimation { id: scrimFade; duration: st.durMedium; easing.type: Easing.Bezier; easing.bezierCurve: [0.2,0,0,1] } }
        MouseArea {
            anchors.fill: parent
            enabled: root.opened
            onClicked: root.closed()
        }
    }

    // ════════════════════════════════════════════════════════════════
    // 抽屉
    // ════════════════════════════════════════════════════════════════
    Item {
        id: drawer
        width: root.drawerW
        height: parent.height
        x: root.opened ? (parent.width - width) : parent.width
        visible: root.opened || drawerSlide.running
        Behavior on x { NumberAnimation { id: drawerSlide; duration: st.durMedium; easing.type: Easing.Bezier; easing.bezierCurve: [0.05,0.7,0.1,1.0] } }

        // 左侧软阴影（无外部依赖的近似）
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -10
            color: "#000000"
            opacity: 0.10
        }

        Rectangle {
            anchors.fill: parent
            color: st.surface
        }

        // ── 抽屉内容 ──
        Column {
            anchors.fill: parent
            spacing: 0

            // 顶栏
            Item {
                width: parent.width
                height: 64
                Rectangle {
                    anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                    height: 1; color: st.outlineVariant
                }
                // 标题 + 状态
                Column {
                    anchors.left: parent.left; anchors.leftMargin: st.spL
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: "广播站 · 排期中心"
                        font.pixelSize: st.titleL; font.weight: Font.Medium
                        color: st.fgSurface
                    }
                    Text {
                        id: connHint
                        font.pixelSize: st.labelM
                        color: st.fgSurfaceVariant
                        text: {
                            if (!voicehub) return "未连接"
                            if (voicehub.baseUrl === "") return "未配置服务地址"
                            if (voicehub.canRequest()) return "已连接 · 点歌通道就绪"
                            return "已连接 · 点歌通道未授权"
                        }
                    }
                }
                // 关闭按钮
                Item {
                    id: closeBtn
                    width: 40; height: 40
                    anchors.right: parent.right; anchors.rightMargin: st.spM
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        anchors.fill: parent; radius: st.rFull
                        color: closeMa.pressed ? Qt.rgba(0,0,0,st.stPress) : (closeMa.containsMouse ? Qt.rgba(0,0,0,st.stHover) : "transparent")
                    }
                    Text { anchors.centerIn: parent; text: "✕"; font.pixelSize: 18; color: st.fgSurfaceVariant }
                    MouseArea {
                        id: closeMa; anchors.fill: parent; hoverEnabled: true
                        onClicked: root.closed()
                    }
                }
            }

            // 服务未配置提示条
            Item {
                width: parent.width; height: (voicehub && voicehub.baseUrl === "") ? 40 : 0
                visible: height > 0
                Text {
                    anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: st.spL
                    text: "⚠ 未配置 VoiceHub 服务地址，排期为空。请在设置页填写。"
                    font.pixelSize: st.labelM; color: st.warn
                }
            }

            // Segmented 控制（三 Tab）
            Item {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: st.spL; anchors.rightMargin: st.spL
                height: 52
                Rectangle {
                    anchors.fill: parent; anchors.margins: st.spM
                    radius: st.rFull
                    color: st.primaryContainer
                    Row {
                        anchors.fill: parent
                        spacing: 0
                        Repeater {
                            model: ["排期", "投稿", "搜索"]
                            delegate: Item {
                                width: parent.width / 3; height: parent.height
                                property int idx: index
                                Rectangle {
                                    anchors.fill: parent; radius: st.rFull
                                    color: root.tabIndex === idx ? st.primary : "transparent"
                                }
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    font.pixelSize: st.bodyM; font.weight: root.tabIndex === idx ? Font.Medium : Font.Normal
                                    color: root.tabIndex === idx ? st.fgPrimary : st.fgPrimaryContainer
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: root.tabIndex = idx
                                }
                            }
                        }
                    }
                }
            }

            // 内容区
            Item {
                width: parent.width
                height: parent.height - 64 - ((voicehub && voicehub.baseUrl === "") ? 40 : 0) - 52
                clip: true

                // ───────── Tab 0：排期 ─────────
                Flickable {
                    id: schedFlick
                    anchors.fill: parent
                    visible: root.tabIndex === 0
                    contentWidth: width
                    contentHeight: schedCol.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: schedCol
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.margins: st.spL
                        spacing: st.spM

                        // 正在播放迷你条
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right
                            height: voicehub && voicehub.playing ? 56 : 0
                            visible: height > 0
                            radius: st.rM
                            color: st.primaryContainer
                            Row {
                                anchors.fill: parent; anchors.leftMargin: st.spM; anchors.rightMargin: st.spM
                                spacing: st.spM
                                // 小等号动画
                                Item { width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter
                                    Repeater {
                                        model: 3
                                        delegate: Rectangle {
                                            width: 3; height: 6 + (index*4)
                                            radius: 2; color: st.primary
                                            x: index * 6
                                            anchors.verticalCenter: parent.verticalCenter
                                            SequentialAnimation on height {
                                                running: true; loops: Animation.Infinite
                                                NumberAnimation { from: 4; to: 16; duration: 380; easing.type: Easing.InOutQuad }
                                                NumberAnimation { from: 16; to: 4; duration: 380; easing.type: Easing.InOutQuad }
                                            }
                                        }
                                    }
                                }
                                Column { anchors.verticalCenter: parent.verticalCenter; spacing: 2
                                    Text { text: "正在播放：" + (voicehub ? voicehub.title : ""); font.pixelSize: st.bodyM; font.weight: Font.Medium; color: st.fgPrimaryContainer; elide: Text.ElideRight; width: schedFlick.width - 140 }
                                    Text { text: voicehub ? voicehub.artist : ""; font.pixelSize: st.labelM; color: st.fgPrimaryContainer; opacity: 0.8; elide: Text.ElideRight; width: schedFlick.width - 140 }
                                }
                            }
                        }

                        // 日期切换条（数据驱动：今日 ∪ 数据中出现过的日期，升序）
                        // 点选某日 → 列表只显示当日排期；拖拽只在当日 1..N 重排。
                        Flickable {
                            anchors.left: parent.left; anchors.right: parent.right
                            height: 48
                            contentWidth: chipRow.width
                            contentHeight: height
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            Row {
                                id: chipRow
                                spacing: st.spXS
                                Repeater {
                                    model: root.dateChips
                                    delegate: Item {
                                        width: 56; height: 40
                                        property bool sel: modelData === root.selectedDate
                                        property bool tdy: isTodayStr(modelData)
                                        Rectangle {
                                            anchors.fill: parent; radius: st.rFull
                                            color: sel ? st.primary : st.containerLow
                                            border.color: sel ? st.primary : (tdy ? st.primary : st.outlineVariant)
                                            border.width: (tdy && !sel) ? 1.5 : 1
                                        }
                                        Column {
                                            anchors.centerIn: parent; spacing: 1
                                            Text { text: fmtChip(modelData); font.pixelSize: st.labelM; font.weight: sel ? Font.Medium : Font.Normal; color: sel ? st.fgPrimary : st.fgSurface }
                                            Text { text: tdy ? "今天" : " "; font.pixelSize: st.labelS; color: sel ? st.fgPrimary : st.fgSurfaceVariant }
                                        }
                                        MouseArea { anchors.fill: parent; onClicked: { root.selectedDate = modelData; rebuildDayModel() } }
                                    }
                                }
                            }
                        }

                        // 列表标题（诚实标注：显示选中日，而非误导性「今日排期」）
                        Text {
                            anchors.left: parent.left
                            text: "排期 · " + fmtChip(root.selectedDate)
                                  + (isTodayStr(root.selectedDate) ? "（今天）" : "")
                                  + "（" + dayModel.count + "）"
                                  + (root.saving ? " · 保存中…" : "")
                            font.pixelSize: st.bodyM; font.weight: Font.Medium; color: st.fgSurfaceVariant
                        }

                        // 排期列表（可长按拖拽排序 · 仅当日，按日隔离）
                        ListView {
                            id: schedList
                            anchors.left: parent.left; anchors.right: parent.right
                            height: Math.max(120, dayModel.count * 84 + 8)
                            model: dayModel
                            spacing: st.spS
                            clip: true
                            displaced: Transition { NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutCubic } }
                            delegate: Component {
                                id: schedDelegate
                                Rectangle {
                                    id: card
                                    property int myIndex: index
                                    width: ListView.view.width
                                    height: 76
                                    radius: st.rM
                                    color: st.containerLowest
                                    border.color: st.outlineVariant
                                    border.width: 1
                                    opacity: held ? 0.72 : 1
                                    Behavior on opacity { NumberAnimation { duration: 120 } }
                                    property bool held: ma.drag.active

                                    // 封面或占位
                                    Rectangle {
                                        id: cover
                                        anchors.left: parent.left; anchors.leftMargin: st.spS
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 52; height: 52; radius: st.rS
                                        color: st.primaryContainer
                                        Image {
                                            anchors.fill: parent; source: model.cover; fillMode: Image.PreserveAspectCrop
                                            visible: model.cover !== ""
                                            layer.enabled: true
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: (model.title && model.title.length) ? model.title.charAt(0) : "♪"
                                            font.pixelSize: 22; font.weight: Font.Medium
                                            color: st.fgPrimaryContainer
                                            visible: model.cover === ""
                                        }
                                    }

                                    // 标题 / 歌手
                                    Column {
                                        anchors.left: cover.right; anchors.leftMargin: st.spM
                                        anchors.right: statusChip.left; anchors.rightMargin: st.spS
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 3
                                        Text { text: model.title; font.pixelSize: st.bodyM; font.weight: Font.Medium; color: st.fgSurface; elide: Text.ElideRight; width: parent.width }
                                        Text { text: model.artist + (model.artist ? " · " : "") + platformName(model.platform) + " · " + fmtDur(model.dur); font.pixelSize: st.labelM; color: st.fgSurfaceVariant; elide: Text.ElideRight; width: parent.width }
                                    }

                                    // 状态 Chip
                                    Rectangle {
                                        id: statusChip
                                        anchors.right: parent.right; anchors.rightMargin: st.spM
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 44; height: 24; radius: st.rFull
                                        color: statusOf(model).bg
                                        Text { anchors.centerIn: parent; text: statusOf(model).text; font.pixelSize: st.labelS; color: statusOf(model).fg }
                                    }

                                    // 拖拽手柄提示
                                    Text {
                                        anchors.right: parent.right; anchors.rightMargin: st.spM
                                        anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                                        text: "⇕"
                                        font.pixelSize: st.labelS; color: st.outline
                                        visible: false // 占位，长按即触发
                                    }

                                    // 状态层
                                    Rectangle {
                                        anchors.fill: parent; radius: st.rM
                                        color: "#000000"
                                        opacity: ma.pressed ? st.stPress : (ma.containsMouse ? st.stHover : 0)
                                    }

                                    MouseArea {
                                        id: ma
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        drag.target: card
                                        drag.axis: Drag.YAxis
                                        drag.minimumY: 0
                                        drag.maximumY: schedList.contentHeight - height
                                        onPressAndHold: if (!root.saving) ma.drag.active = true
                                        onReleased: {
                                            if (ma.drag.active) {
                                                ma.drag.active = false
                                                // 拖到一半时 card.y 已是内容坐标，取中点命中目标行
                                                var idx = schedList.indexAt(schedList.contentX, card.y + height / 2)
                                                if (idx >= 0 && idx !== myIndex) {
                                                    dayModel.move(myIndex, idx, 1)
                                                    // 只把「当日」这一组按新视觉顺序 1..N 提交；
                                                    // 其它日期根本不在 dayModel 里，故不会被污染。
                                                    var pairs = []
                                                    for (var k = 0; k < dayModel.count; ++k) {
                                                        var it = dayModel.get(k)
                                                        pairs.push({ id: it.sid, sequence: k + 1 })
                                                    }
                                                    if (voicehub && voicehub.canRequest()) {
                                                        root.saving = true
                                                        voicehub.reorderSchedule(pairs)
                                                    } else {
                                                        snack.show("未配置 API Key，顺序仅在本地面板内生效", false)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                anchors.fill: parent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                visible: dayModel.count === 0
                                text: "该日暂无排期"
                                font.pixelSize: st.bodyM; color: st.fgSurfaceVariant
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            text: "提示：长按曲目可调整顺序，松手即保存到 VoiceHub（仅对当日生效，需 API Key 具备 schedules:write 权限）"
                            font.pixelSize: st.labelS; color: st.fgSurfaceVariant
                            wrapMode: Text.Wrap; width: parent.width
                        }
                    }
                }

                // ───────── Tab 1：投稿（点歌） ─────────
                Flickable {
                    anchors.fill: parent
                    visible: root.tabIndex === 1
                    contentWidth: width
                    contentHeight: subCol.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: subCol
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.margins: st.spL
                        spacing: st.spM

                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right
                            radius: st.rL
                            color: st.containerLowest
                            border.color: st.outlineVariant; border.width: 1
                            Column {
                                anchors.fill: parent
                                anchors.margins: st.spL
                                spacing: st.spM

                                Text { text: "点歌投稿"; font.pixelSize: st.titleM; font.weight: Font.Medium; color: st.fgSurface }

                                // 歌名
                                Column { spacing: 4; anchors.left: parent.left; anchors.right: parent.right
                                    Text { text: "歌名 *"; font.pixelSize: st.labelM; color: st.fgSurfaceVariant }
                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; height: 40; radius: st.rS; color: st.surface; border.color: st.outline; border.width: 1
                                        TextInput { id: tiTitle; anchors.fill: parent; anchors.leftMargin: st.spM; anchors.rightMargin: st.spM; verticalAlignment: Text.AlignVCenter; font.pixelSize: st.bodyM; color: st.fgSurface; clip: true }
                                        Text { anchors.left: parent.left; anchors.leftMargin: st.spM; anchors.verticalCenter: parent.verticalCenter; text: "输入歌名"; font.pixelSize: st.bodyM; color: st.outline; visible: tiTitle.text === "" }
                                    }
                                }
                                // 歌手
                                Column { spacing: 4; anchors.left: parent.left; anchors.right: parent.right
                                    Text { text: "歌手"; font.pixelSize: st.labelM; color: st.fgSurfaceVariant }
                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; height: 40; radius: st.rS; color: st.surface; border.color: st.outline; border.width: 1
                                        TextInput { id: tiArtist; anchors.fill: parent; anchors.leftMargin: st.spM; anchors.rightMargin: st.spM; verticalAlignment: Text.AlignVCenter; font.pixelSize: st.bodyM; color: st.fgSurface; clip: true }
                                        Text { anchors.left: parent.left; anchors.leftMargin: st.spM; anchors.verticalCenter: parent.verticalCenter; text: "选填"; font.pixelSize: st.bodyM; color: st.outline; visible: tiArtist.text === "" }
                                    }
                                }
                                // 平台选择
                                Column { spacing: 4; anchors.left: parent.left; anchors.right: parent.right
                                    Text { text: "来源平台"; font.pixelSize: st.labelM; color: st.fgSurfaceVariant }
                                    Row { spacing: st.spS; anchors.left: parent.left; anchors.right: parent.right
                                        Repeater {
                                            id: platRep
                                            model: [ {k:"netease",n:"网易云"}, {k:"qq",n:"QQ音乐"}, {k:"kugou",n:"酷狗"}, {k:"local",n:"本地"} ]
                                            delegate: Item {
                                                width: 72; height: 32
                                                property string pk: modelData.k
                                                Rectangle { anchors.fill: parent; radius: st.rFull; color: reqPlatform === pk ? st.primary : st.containerLow; border.color: reqPlatform === pk ? st.primary : st.outlineVariant; border.width: 1 }
                                                Text { anchors.centerIn: parent; text: modelData.n; font.pixelSize: st.labelM; color: reqPlatform === pk ? st.fgPrimary : st.fgSurfaceVariant }
                                                MouseArea { anchors.fill: parent; onClicked: reqPlatform = pk }
                                            }
                                        }
                                    }
                                }

                                // 提交按钮
                                Rectangle {
                                    anchors.left: parent.left; anchors.right: parent.right; height: 44; radius: st.rFull
                                    color: (voicehub && voicehub.canRequest() && tiTitle.text.trim() !== "") ? st.primary : st.containerHigh
                                    Text { anchors.centerIn: parent; text: "提交点歌"; font.pixelSize: st.bodyM; font.weight: Font.Medium; color: (voicehub && voicehub.canRequest() && tiTitle.text.trim() !== "") ? st.fgPrimary : st.fgSurfaceVariant }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            if (!voicehub) return
                                            if (!voicehub.canRequest()) { snack.show("点歌通道未就绪（检查地址与 API Key）", false); return }
                                            if (tiTitle.text.trim() === "") { snack.show("歌名不能为空", false); return }
                                            voicehub.requestSong(tiTitle.text, tiArtist.text, reqPlatform, "")
                                            tiTitle.text = ""; tiArtist.text = ""
                                        }
                                    }
                                }
                                Text {
                                    anchors.left: parent.left; anchors.right: parent.right
                                    text: "投稿会提交到 VoiceHub 后台排队；批准后在排期 Tab 可见。"
                                    font.pixelSize: st.labelS; color: st.fgSurfaceVariant; wrapMode: Text.Wrap
                                }
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right
                            radius: st.rM; color: st.containerLow
                            height: 64
                            Text {
                                anchors.fill: parent; anchors.margins: st.spM
                                text: "班级投稿列表：待 VoiceHub 开放「投稿查询」接口后接入。当前可先通过点歌表单投稿。"
                                font.pixelSize: st.labelM; color: st.fgSurfaceVariant; wrapMode: Text.Wrap
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                // ───────── Tab 2：搜索 ─────────
                Flickable {
                    anchors.fill: parent
                    visible: root.tabIndex === 2
                    contentWidth: width
                    contentHeight: searchCol.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: searchCol
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.margins: st.spL
                        spacing: st.spM

                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right; height: 40; radius: st.rFull
                            color: st.containerLowest; border.color: st.outline; border.width: 1
                            TextInput { id: searchInput; anchors.fill: parent; anchors.leftMargin: st.spM + 24; anchors.rightMargin: st.spM; verticalAlignment: Text.AlignVCenter; font.pixelSize: st.bodyM; color: st.fgSurface; clip: true }
                            Text { anchors.left: parent.left; anchors.leftMargin: st.spM; anchors.verticalCenter: parent.verticalCenter; text: "🔍"; font.pixelSize: st.bodyM; color: st.outline }
                            Text { anchors.left: parent.left; anchors.leftMargin: st.spM + 24; anchors.verticalCenter: parent.verticalCenter; text: "搜索已加载的排期"; font.pixelSize: st.bodyM; color: st.outline; visible: searchInput.text === "" }
                        }

                        Text {
                            anchors.left: parent.left
                            text: "（本地过滤当前已加载排期；全曲库搜索接口待 VoiceHub 开放）"
                            font.pixelSize: st.labelS; color: st.fgSurfaceVariant; wrapMode: Text.Wrap; width: parent.width
                        }

                        Column {
                            anchors.left: parent.left; anchors.right: parent.right
                            spacing: st.spS
                            Repeater {
                                model: {
                                    var q = searchInput.text.trim().toLowerCase()
                                    var out = []
                                    for (var i = 0; i < scheduleModel.count; ++i) {
                                        var it = scheduleModel.get(i)
                                        if (q === "" || (it.title && it.title.toLowerCase().indexOf(q) >= 0) || (it.artist && it.artist.toLowerCase().indexOf(q) >= 0))
                                            out.push(it)
                                    }
                                    return out
                                }
                                delegate: Rectangle {
                                    anchors.left: parent.left; anchors.right: parent.right
                                    height: 48; radius: st.rM; color: st.containerLowest
                                    border.color: st.outlineVariant; border.width: 1
                                    Row {
                                        anchors.fill: parent; anchors.leftMargin: st.spM; anchors.rightMargin: st.spM; spacing: st.spM
                                        // 纯 Row 无 Layouts 依赖：标题按「行宽 - 歌手宽 - 间距」约束，启用省略号
                                        Text { id: sTitle; text: modelData.title; font.pixelSize: st.bodyM; color: st.fgSurface; elide: Text.ElideRight; width: parent.width - sArtist.width - st.spM }
                                        Text { id: sArtist; text: modelData.artist; font.pixelSize: st.labelM; color: st.fgSurfaceVariant; elide: Text.ElideRight }
                                    }
                                }
                            }
                            Text {
                                anchors.left: parent.left
                                visible: scheduleModel.count === 0
                                text: "暂无可搜索的排期数据"
                                font.pixelSize: st.bodyM; color: st.fgSurfaceVariant
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Snackbar ──
    QtObject { id: snack; property bool ok: true
        function show(msg, isOk) { snackMsg.text = msg; snack.ok = isOk; snackBar.visible = true; snackTimer.restart() }
    }
    Timer { id: snackTimer; interval: 3200; onTriggered: snackBar.visible = false }
    Rectangle {
        id: snackBar
        visible: false
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: st.durShort } }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: st.spL
        width: Math.min(360, parent.width - 2*st.spL); height: 44
        radius: st.rM
        color: st.inverseSurface
        Text { id: snackMsg; anchors.fill: parent; anchors.margins: st.spM; verticalAlignment: Text.AlignVCenter; text: ""; font.pixelSize: st.bodyM; color: st.inverseOn; elide: Text.ElideRight }
    }

    // 点歌结果 → Snackbar
    Connections {
        target: voicehub
        function onControlFinished(ok, message) { snack.show(message, ok) }
        function onReorderFinished(ok, message) {
            root.saving = false
            if (ok) {
                // 本地视觉顺序已是拖拽结果；服务端也已落库，无需再改属性。
                // 下一轮 30s 轮询或重开面板会拉到服务端真值，保持一致。
                snack.show("顺序已保存到 VoiceHub（" + fmtChip(root.selectedDate) + "）", true)
            } else {
                snack.show(message, false)
                if (voicehub) voicehub.refreshSchedule() // 失败回滚到服务端真值
            }
        }
    }

    Component.onCompleted: syncSchedule()
}
