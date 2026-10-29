import QtQuick 2.15
import "../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一）

// 广播站 · 排期中心（右侧滑出 SideSheet · 2026-10-22）
//
// 设计约定：
//   · 2026-10-30 用户点单「全部换新」：原来本件自带一套「现代蓝白」调色板（本地 `st` 对象，
//     品牌蓝 primary、浅蓝白 surface），与被控端其余页面**是两套视觉语言**。
//     现改为共享库 `Theme` 单一真源：品牌蓝取消（改用 `Theme.accentStrong`，被控端置中性反白），
//     边框全部删除（去边框规范，靠底色差分界），本地 `st` 对象已删。
//   · 纯 QtQuick 原语实现 —— 被控端部署**不含 QtQuick.Controls 运行库**
//     （AgentPlaySchedulePage 已踩过：引 Controls 会整页解析失败），故不复用 Controls。
//   · 数据来自上下文属性 `voicehub`（AgentShell 已注入）：
//       voicehub.schedule（QVariantList，每条 = 一首排期曲）
//       voicehub.scheduleCount / voicehub.controlEnabled / voicehub.canRequest
//       voicehub.requestSong(title, artist, platform, musicId)
//       voicehub.controlFinished(bool, msg) 信号
//     voicehub.schedule 每条字段：id sequence played isPlaying broadcastPosition
//       playDate title artist cover durationSeconds musicPlatform musicId songId
//
// 交互：
//   · 顶栏「广播站」按钮 → opened=true（由 AgentShell 控制）
//   · 三 Tab：排期 / 投稿 / 搜索（SegmentedButton）
//   · 排期 Tab：数据驱动日期条 + 当日排期列表（每首带封面/标题/歌手/时长/平台徽标/状态 Chip），拖拽排序
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
        if (it.playing) return { text: "在播", bg: Theme.color.surfaceVariant, fg: Theme.color.textPrimary }
        if (it.played)  return { text: "已播", bg: Theme.color.surfaceVariant,       fg: Theme.color.textSecondary }
        return { text: "未播", bg: Theme.color.surfaceVariant, fg: Theme.color.textSecondary }
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
        color: Theme.color.scrim
        opacity: root.opened ? 1 : 0
        visible: root.opened || scrimFade.running
        Behavior on opacity { NumberAnimation { id: scrimFade; duration: Theme.motion.normal; easing.type: Easing.Bezier; easing.bezierCurve: [0.2,0,0,1] } }
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
        Behavior on x { NumberAnimation { id: drawerSlide; duration: Theme.motion.normal; easing.type: Easing.Bezier; easing.bezierCurve: [0.05,0.7,0.1,1.0] } }

        // 左侧软阴影（无外部依赖的近似）
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -10
            color: Theme.shadow.soft
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.color.surface
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
                    height: 1; color: Theme.color.surfaceVariant
                }
                // 标题 + 状态
                Column {
                    anchors.left: parent.left; anchors.leftMargin: Theme.spacing.lg
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        text: "广播站 · 排期中心"
                        font.pixelSize: 22; font.weight: Font.Medium
                        color: Theme.color.textPrimary
                    }
                    Text {
                        id: connHint
                        font.pixelSize: 12
                        color: Theme.color.textSecondary
                        text: {
                            if (!voicehub) return "未连接"
                            if (voicehub.baseUrl === "") return "未配置服务地址"
                            if (voicehub.canRequest) return "已连接 · 点歌通道就绪"
                            return "已连接 · 点歌通道未授权"
                        }
                    }
                }
                // 关闭按钮
                Item {
                    id: closeBtn
                    width: 40; height: 40
                    anchors.right: parent.right; anchors.rightMargin: Theme.spacing.md
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        anchors.fill: parent; radius: Theme.radius.full
                        color: closeMa.pressed ? Qt.rgba(0,0,0,0.10) : (closeMa.containsMouse ? Qt.rgba(0,0,0,0.06) : "transparent")
                    }
                    Text { anchors.centerIn: parent; text: "✕"; font.pixelSize: 18; color: Theme.color.textSecondary }
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
                    anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: Theme.spacing.lg
                    text: "⚠ 未配置 VoiceHub 服务地址，排期为空。请在设置页填写。"
                    font.pixelSize: 12; color: Theme.color.warning
                }
            }

            // Segmented 控制（三 Tab）
            Item {
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: Theme.spacing.lg; anchors.rightMargin: Theme.spacing.lg
                height: 52
                Rectangle {
                    anchors.fill: parent; anchors.margins: Theme.spacing.md
                    radius: Theme.radius.full
                    color: Theme.color.surfaceVariant
                    Row {
                        anchors.fill: parent
                        spacing: 0
                        Repeater {
                            model: ["排期", "投稿", "搜索"]
                            delegate: Item {
                                width: parent.width / 3; height: parent.height
                                property int idx: index
                                Rectangle {
                                    anchors.fill: parent; radius: Theme.radius.full
                                    color: root.tabIndex === idx ? Theme.accentStrong : "transparent"
                                }
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData
                                    font.pixelSize: 14; font.weight: root.tabIndex === idx ? Font.Medium : Font.Normal
                                    color: root.tabIndex === idx ? Theme.accentStrongForeground : Theme.color.textPrimary
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
                        anchors.margins: Theme.spacing.lg
                        spacing: Theme.spacing.md

                        // 正在播放迷你条
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right
                            height: voicehub && voicehub.playing ? 56 : 0
                            visible: height > 0
                            radius: Theme.radius.md
                            color: Theme.color.surfaceVariant
                            Row {
                                anchors.fill: parent; anchors.leftMargin: Theme.spacing.md; anchors.rightMargin: Theme.spacing.md
                                spacing: Theme.spacing.md
                                // 小等号动画
                                Item { width: 18; height: 18; anchors.verticalCenter: parent.verticalCenter
                                    Repeater {
                                        model: 3
                                        delegate: Rectangle {
                                            width: 3; height: 6 + (index*4)
                                            radius: 2; color: Theme.accentStrong
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
                                    Text { text: "正在播放：" + (voicehub ? voicehub.title : ""); font.pixelSize: 14; font.weight: Font.Medium; color: Theme.color.textPrimary; elide: Text.ElideRight; width: schedFlick.width - 140 }
                                    Text { text: voicehub ? voicehub.artist : ""; font.pixelSize: 12; color: Theme.color.textPrimary; opacity: 0.8; elide: Text.ElideRight; width: schedFlick.width - 140 }
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
                                spacing: Theme.spacing.xs
                                Repeater {
                                    model: root.dateChips
                                    delegate: Item {
                                        width: 56; height: 40
                                        property bool sel: modelData === root.selectedDate
                                        property bool tdy: isTodayStr(modelData)
                                        Rectangle {
                                            anchors.fill: parent; radius: Theme.radius.full
                                            color: sel ? Theme.accentStrong : Theme.color.surfaceVariant
                                        }
                                        Column {
                                            anchors.centerIn: parent; spacing: 1
                                            Text { text: fmtChip(modelData); font.pixelSize: 12; font.weight: sel ? Font.Medium : Font.Normal; color: sel ? Theme.accentStrongForeground : Theme.color.textPrimary }
                                            Text { text: tdy ? "今天" : " "; font.pixelSize: 11; color: sel ? Theme.accentStrongForeground : Theme.color.textSecondary }
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
                            font.pixelSize: 14; font.weight: Font.Medium; color: Theme.color.textSecondary
                        }

                        // 排期列表（可拖拽排序 · 仅当日，按日隔离）
                        ListView {
                            id: schedList
                            anchors.left: parent.left; anchors.right: parent.right
                            height: Math.max(120, dayModel.count * 84 + 8)
                            model: dayModel
                            spacing: Theme.spacing.sm
                            clip: true
                            displaced: Transition { NumberAnimation { properties: "y"; duration: 180; easing.type: Easing.OutCubic } }
                            delegate: Component {
                                id: schedDelegate
                                Rectangle {
                                    id: card
                                    property int myIndex: index
                                    width: ListView.view.width
                                    height: 76
                                    radius: Theme.radius.md
                                    // 卡片浮在抽屉面板(surface)之上 ⇒ surfaceVariant。
                                    // ⚠️ 不用 background：那是**页面**底，在面板上会变成一块凹陷的黑井，
                                    //    浅色主题下还会与面板撞色（2026-10-30 截图实证）。
                                    color: Theme.color.surfaceVariant
                                    opacity: held ? 0.72 : 1
                                    Behavior on opacity { NumberAnimation { duration: 120 } }
                                    property bool held: ma.drag.active

                                    // 封面或占位
                                    Rectangle {
                                        id: cover
                                        anchors.left: parent.left; anchors.leftMargin: Theme.spacing.sm
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 52; height: 52; radius: Theme.radius.sm
                                        color: Theme.color.surfaceVariant
                                        Image {
                                            anchors.fill: parent; source: model.cover; fillMode: Image.PreserveAspectCrop
                                            visible: model.cover !== ""
                                            layer.enabled: true
                                        }
                                        Text {
                                            anchors.centerIn: parent
                                            text: (model.title && model.title.length) ? model.title.charAt(0) : "♪"
                                            font.pixelSize: 22; font.weight: Font.Medium
                                            color: Theme.color.textPrimary
                                            visible: model.cover === ""
                                        }
                                    }

                                    // 标题 / 歌手
                                    Column {
                                        anchors.left: cover.right; anchors.leftMargin: Theme.spacing.md
                                        anchors.right: statusChip.left; anchors.rightMargin: Theme.spacing.sm
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 3
                                        Text { text: model.title; font.pixelSize: 14; font.weight: Font.Medium; color: Theme.color.textPrimary; elide: Text.ElideRight; width: parent.width }
                                        Text { text: model.artist + (model.artist ? " · " : "") + platformName(model.platform) + " · " + fmtDur(model.dur); font.pixelSize: 12; color: Theme.color.textSecondary; elide: Text.ElideRight; width: parent.width }
                                    }

                                    // 状态 Chip
                                    Rectangle {
                                        id: statusChip
                                        anchors.right: parent.right; anchors.rightMargin: Theme.spacing.md
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 44; height: 24; radius: Theme.radius.full
                                        color: statusOf(model).bg
                                        Text { anchors.centerIn: parent; text: statusOf(model).text; font.pixelSize: 11; color: statusOf(model).fg }
                                    }

                                    // 拖拽手柄提示
                                    Text {
                                        anchors.right: parent.right; anchors.rightMargin: Theme.spacing.md
                                        anchors.bottom: parent.bottom; anchors.bottomMargin: 4
                                        text: "⇕"
                                        font.pixelSize: 11; color: Theme.color.textSecondary
                                        visible: false // 占位，拖动即触发
                                    }

                                    // 状态层
                                    Rectangle {
                                        anchors.fill: parent; radius: Theme.radius.md
                                        color: Theme.color.textPrimary
                                        opacity: ma.pressed ? 0.10 : (ma.containsMouse ? 0.06 : 0)
                                    }

                                    MouseArea {
                                        id: ma
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        drag.target: card
                                        drag.axis: Drag.YAxis
                                        drag.minimumY: 0
                                        drag.maximumY: schedList.contentHeight - height
                                        // 2026-10-23 修复：Qt 6 起 Drag.active 只读（QQuickDrag 的
                                        // Q_PROPERTY 无 WRITE，赋值抛 TypeError），「onPressAndHold 里
                                        // drag.active=true 启动拖拽」的旧写法从未生效过（10-22 起潜伏）。
                                        // 改为 MouseArea 内置移动拖拽（移动超阈值自动开始）。
                                        property real pressY: 0
                                        onPressed: pressY = card.y
                                        onReleased: (mouse) => { // Qt6.12：注入 mouse 形参已废弃（松手即打 deprecation qWarning），按官方形式改箭头函数显式形参，逻辑等价
                                            // 落点换算（2026-10-23 修复，探针实证）：
                                            // 不能用 card.y+height/2 调 indexAt——拖拽中 card 与相邻行
                                            // 视觉重叠，按 card 中心查恒命中自己被拖动的那一行
                                            // （idx==myIndex，表现为「拖了却从不换序」）；按鼠标点调
                                            // indexAt 同样命中重叠的被拖动卡。故用「鼠标释放点」按行高
                                            // 算术换算目标行；行高取 card.height + schedList.spacing，
                                            // 与 ListView 排布同源，不写死魔数。
                                            var idx = -1
                                            if (!root.saving) {
                                                var mp = ma.mapToItem(schedList, mouse.x, mouse.y)
                                                idx = Math.floor((schedList.contentY + mp.y)
                                                                / (card.height + schedList.spacing))
                                                idx = Math.max(0, Math.min(dayModel.count - 1, idx))
                                            }
                                            if (idx >= 0 && idx !== myIndex) {
                                                dayModel.move(myIndex, idx, 1)
                                                // 只把「当日」这一组按新视觉顺序 1..N 提交；
                                                // 其它日期根本不在 dayModel 里，故不会被污染。
                                                var pairs = []
                                                for (var k = 0; k < dayModel.count; ++k) {
                                                    var it = dayModel.get(k)
                                                    pairs.push({ id: it.sid, sequence: k + 1 })
                                                }
                                                if (voicehub && voicehub.canRequest) {
                                                    root.saving = true
                                                    voicehub.reorderSchedule(pairs)
                                                } else {
                                                    snack.show("未配置 API Key，顺序仅在本地面板内生效", false)
                                                }
                                            } else {
                                                card.y = pressY // 原地放下/保存中被拦：放回原列
                                            }
                                        }
                                    }
                                }
                            }

                            Text {
                                anchors.fill: parent; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                visible: dayModel.count === 0
                                text: "该日暂无排期"
                                font.pixelSize: 14; color: Theme.color.textSecondary
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            text: "提示：拖动曲目即可调整顺序，松手保存到 VoiceHub（仅对当日生效，需 API Key 具备 schedules:write 权限）"
                            font.pixelSize: 11; color: Theme.color.textSecondary
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
                        anchors.margins: Theme.spacing.lg
                        spacing: Theme.spacing.md

                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right
                            radius: Theme.radius.lg
                            color: Theme.color.surfaceVariant
                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.spacing.lg
                                spacing: Theme.spacing.md

                                Text { text: "点歌投稿"; font.pixelSize: 16; font.weight: Font.Medium; color: Theme.color.textPrimary }

                                // 歌名
                                Column { spacing: 4; anchors.left: parent.left; anchors.right: parent.right
                                    Text { text: "歌名 *"; font.pixelSize: 12; color: Theme.color.textSecondary }
                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; height: 40; radius: Theme.radius.sm; color: Theme.color.surface
                                        TextInput { id: tiTitle; anchors.fill: parent; anchors.leftMargin: Theme.spacing.md; anchors.rightMargin: Theme.spacing.md; verticalAlignment: Text.AlignVCenter; font.pixelSize: 14; color: Theme.color.textPrimary; clip: true }
                                        Text { anchors.left: parent.left; anchors.leftMargin: Theme.spacing.md; anchors.verticalCenter: parent.verticalCenter; text: "输入歌名"; font.pixelSize: 14; color: Theme.color.textSecondary; visible: tiTitle.text === "" }
                                    }
                                }
                                // 歌手
                                Column { spacing: 4; anchors.left: parent.left; anchors.right: parent.right
                                    Text { text: "歌手"; font.pixelSize: 12; color: Theme.color.textSecondary }
                                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; height: 40; radius: Theme.radius.sm; color: Theme.color.surface
                                        TextInput { id: tiArtist; anchors.fill: parent; anchors.leftMargin: Theme.spacing.md; anchors.rightMargin: Theme.spacing.md; verticalAlignment: Text.AlignVCenter; font.pixelSize: 14; color: Theme.color.textPrimary; clip: true }
                                        Text { anchors.left: parent.left; anchors.leftMargin: Theme.spacing.md; anchors.verticalCenter: parent.verticalCenter; text: "选填"; font.pixelSize: 14; color: Theme.color.textSecondary; visible: tiArtist.text === "" }
                                    }
                                }
                                // 平台选择
                                Column { spacing: 4; anchors.left: parent.left; anchors.right: parent.right
                                    Text { text: "来源平台"; font.pixelSize: 12; color: Theme.color.textSecondary }
                                    Row { spacing: Theme.spacing.sm; anchors.left: parent.left; anchors.right: parent.right
                                        Repeater {
                                            id: platRep
                                            model: [ {k:"netease",n:"网易云"}, {k:"qq",n:"QQ音乐"}, {k:"kugou",n:"酷狗"}, {k:"local",n:"本地"} ]
                                            delegate: Item {
                                                width: 72; height: 32
                                                property string pk: modelData.k
                                                Rectangle { anchors.fill: parent; radius: Theme.radius.full; color: reqPlatform === pk ? Theme.accentStrong : Theme.color.surfaceVariant}
                                                Text { anchors.centerIn: parent; text: modelData.n; font.pixelSize: 12; color: reqPlatform === pk ? Theme.accentStrongForeground : Theme.color.textSecondary }
                                                MouseArea { anchors.fill: parent; onClicked: reqPlatform = pk }
                                            }
                                        }
                                    }
                                }

                                // 提交按钮
                                Rectangle {
                                    anchors.left: parent.left; anchors.right: parent.right; height: 44; radius: Theme.radius.full
                                    color: (voicehub && voicehub.canRequest && tiTitle.text.trim() !== "") ? Theme.accentStrong : Theme.color.surfaceVariant
                                    Text { anchors.centerIn: parent; text: "提交点歌"; font.pixelSize: 14; font.weight: Font.Medium; color: (voicehub && voicehub.canRequest && tiTitle.text.trim() !== "") ? Theme.accentStrongForeground : Theme.color.textSecondary }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            if (!voicehub) return
                                            if (!voicehub.canRequest) { snack.show("点歌通道未就绪（检查地址与 API Key）", false); return }
                                            if (tiTitle.text.trim() === "") { snack.show("歌名不能为空", false); return }
                                            voicehub.requestSong(tiTitle.text, tiArtist.text, reqPlatform, "")
                                            tiTitle.text = ""; tiArtist.text = ""
                                        }
                                    }
                                }
                                Text {
                                    anchors.left: parent.left; anchors.right: parent.right
                                    text: "投稿会提交到 VoiceHub 后台排队；批准后在排期 Tab 可见。"
                                    font.pixelSize: 11; color: Theme.color.textSecondary; wrapMode: Text.Wrap
                                }
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right
                            radius: Theme.radius.md; color: Theme.color.surfaceVariant
                            height: 64
                            Text {
                                anchors.fill: parent; anchors.margins: Theme.spacing.md
                                text: "班级投稿列表：待 VoiceHub 开放「投稿查询」接口后接入。当前可先通过点歌表单投稿。"
                                font.pixelSize: 12; color: Theme.color.textSecondary; wrapMode: Text.Wrap
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
                        anchors.margins: Theme.spacing.lg
                        spacing: Theme.spacing.md

                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right; height: 40; radius: Theme.radius.full
                            color: Theme.color.surfaceVariant
                            TextInput { id: searchInput; anchors.fill: parent; anchors.leftMargin: Theme.spacing.md + 24; anchors.rightMargin: Theme.spacing.md; verticalAlignment: Text.AlignVCenter; font.pixelSize: 14; color: Theme.color.textPrimary; clip: true }
                            Text { anchors.left: parent.left; anchors.leftMargin: Theme.spacing.md; anchors.verticalCenter: parent.verticalCenter; text: "🔍"; font.pixelSize: 14; color: Theme.color.textSecondary }
                            Text { anchors.left: parent.left; anchors.leftMargin: Theme.spacing.md + 24; anchors.verticalCenter: parent.verticalCenter; text: "搜索已加载的排期"; font.pixelSize: 14; color: Theme.color.textSecondary; visible: searchInput.text === "" }
                        }

                        Text {
                            anchors.left: parent.left
                            text: "（本地过滤当前已加载排期；全曲库搜索接口待 VoiceHub 开放）"
                            font.pixelSize: 11; color: Theme.color.textSecondary; wrapMode: Text.Wrap; width: parent.width
                        }

                        Column {
                            anchors.left: parent.left; anchors.right: parent.right
                            spacing: Theme.spacing.sm
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
                                    height: 48; radius: Theme.radius.md; color: Theme.color.surfaceVariant
                                    Row {
                                        anchors.fill: parent; anchors.leftMargin: Theme.spacing.md; anchors.rightMargin: Theme.spacing.md; spacing: Theme.spacing.md
                                        // 纯 Row 无 Layouts 依赖：标题按「行宽 - 歌手宽 - 间距」约束，启用省略号
                                        Text { id: sTitle; text: modelData.title; font.pixelSize: 14; color: Theme.color.textPrimary; elide: Text.ElideRight; width: parent.width - sArtist.width - Theme.spacing.md }
                                        Text { id: sArtist; text: modelData.artist; font.pixelSize: 12; color: Theme.color.textSecondary; elide: Text.ElideRight }
                                    }
                                }
                            }
                            Text {
                                anchors.left: parent.left
                                visible: scheduleModel.count === 0
                                text: "暂无可搜索的排期数据"
                                font.pixelSize: 14; color: Theme.color.textSecondary
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
        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.spacing.lg
        width: Math.min(360, parent.width - 2*Theme.spacing.lg); height: 44
        radius: Theme.radius.md
        color: Theme.color.textPrimary
        Text { id: snackMsg; anchors.fill: parent; anchors.margins: Theme.spacing.md; verticalAlignment: Text.AlignVCenter; text: ""; font.pixelSize: 14; color: Theme.color.background; elide: Text.ElideRight }
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
