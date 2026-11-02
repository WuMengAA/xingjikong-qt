import QtQuick 2.15
import "../../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一）

// 广播社成员 · 独立界面（整页 · 2026-10-23 #广播站移交管理端）
//
// 设计约定：
//   · 纯 QML 实现（被控端部署不含 QtQuick.Controls 运行库，不复用 Controls）。
//     改为共享库 `Theme` 单一真源（品牌蓝取消、边框删除、状态层用前景色叠透明度）。
//   · 数据来自上下文属性 `broadcastClub`（AgentShell::setBroadcastClub 注入）：
//       broadcastClub.loggedIn / station / token
//       broadcastClub.schedule（QVariantList，每条 = 一首排期曲，字段同 voicehub.schedule）
//       broadcastClub.login(u,p) / logout() / refreshSchedules() / reorder(pairs)
//       broadcastClub.loginFinished(ok,msg) / scheduleChanged() / reorderFinished(ok,msg)
//   · 与老师用的 BroadcastScheduleSheet 的区别：
//       - 成员无 voicehub 控制能力（点歌/投稿/搜索），本页只有「排期」单 Tab；
//       - 多一层登录门：未登录只显示登录表单，登录后只看到排期（看不到老师其它功能）；
//       - 排期读写经云端代理（broadcastClub），密钥云端持有，成员令牌只在本地鉴权用。
//
// 交互：
//   · 未登录 → 用户名 + 密码 + 登录；错误走错误条。
//   · 已登录 → 顶栏显示归属 station + 退出按钮；排期 Tab（数据驱动日期条 + 按日隔离列表 +
//     拖拽排序，松手即提交云端）。

Item {
    id: root
    anchors.fill: parent

    // 页入口契约（与 AgentSettingsPage 同口径）
    property var ctx
    // broadcastingClub 上下文属性守卫（模块未注册时避免 ReferenceError）
    readonly property var club: (typeof broadcastClub !== "undefined" && broadcastClub !== null)
        ? broadcastClub : null


    // ── 登录表单状态 ──
    property string uiUser: ""
    property string uiPass: ""
    property bool loggingIn: false

    // 排期按日隔离（复用 BroadcastScheduleSheet 逻辑）
    property string selectedDate: ""
    property var dateChips: []
    property bool saving: false

    ListModel { id: scheduleModel }
    ListModel { id: dayModel }

    function syncSchedule() {
        if (!club) return
        var arr = club.schedule || []
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
                playDate: s.playDate || ""
            })
        }
        rebuildDayModel()
    }

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

    // 登录态 / 排期变化实时同步
    Connections {
        target: club
        function onAuthChanged() {
            if (club && club.loggedIn) { syncSchedule(); club.refreshSchedules() }
        }
        function onScheduleChanged() { syncSchedule() }
        function onLoginFinished(ok, message) {
            loggingIn = false
            if (!ok) loginErr.text = message
            else { root.uiPass = ""; loginErr.text = "" }
        }
        function onReorderFinished(ok, message) {
            root.saving = false
            if (ok) snack.show("顺序已保存到云端（" + fmtChip(root.selectedDate) + "）", true)
            else { snack.show(message, false); if (club) club.refreshSchedules() }
        }
    }

    Component.onCompleted: {
        if (club && club.loggedIn) { syncSchedule(); club.refreshSchedules() }
    }

    // ════════════════════════════════════════════════════════════════
    // 内容
    // ════════════════════════════════════════════════════════════════
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
            Column {
                anchors.left: parent.left; anchors.leftMargin: Theme.spacing.lg
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: "广播社 · 排期中心"
                    font.pixelSize: 22; font.weight: Font.Medium
                    color: Theme.color.textPrimary
                }
                Text {
                    id: subHint
                    font.pixelSize: 12
                    color: Theme.color.textSecondary
                    text: {
                        if (!club) return "模块未就绪"
                        if (!club.loggedIn) return "请使用广播社成员账号登录"
                        return "已登录 · 归属 " + (club.station || "—")
                    }
                }
            }
            // 退出按钮（已登录才显示）
            Item {
                id: logoutBtn
                width: 40; height: 40
                anchors.right: parent.right; anchors.rightMargin: Theme.spacing.md
                anchors.verticalCenter: parent.verticalCenter
                visible: club && club.loggedIn
                Rectangle {
                    anchors.fill: parent; radius: Theme.radius.full
                    color: loMa.pressed ? Qt.rgba(0,0,0,0.10) : (loMa.containsMouse ? Qt.rgba(0,0,0,0.06) : "transparent")
                }
                Text { anchors.centerIn: parent; text: "⏻"; font.pixelSize: 18; color: Theme.color.textSecondary }
                MouseArea {
                    id: loMa; anchors.fill: parent; hoverEnabled: true
                    onClicked: { if (club) club.logout() }
                }
            }
        }

        // 内容区（滚动）
        Item {
            width: parent.width
            height: parent.height - 64
            clip: true

            // ── 未登录：登录表单 ──
            Item {
                anchors.fill: parent
                visible: !club || !club.loggedIn
                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top; anchors.topMargin: Theme.spacing.lg * 2
                    width: Math.min(360, parent.width - 2 * Theme.spacing.lg)
                    spacing: Theme.spacing.md

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "广播社成员登录"
                        font.pixelSize: 16; font.weight: Font.Medium; color: Theme.color.textPrimary
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "登录后可管理本社团广播站排期"
                        font.pixelSize: 12; color: Theme.color.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                    }

                    // 用户名
                    Column { spacing: 4; anchors.left: parent.left; anchors.right: parent.right
                        Text { text: "用户名"; font.pixelSize: 12; color: Theme.color.textSecondary }
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; height: 44; radius: Theme.radius.sm; color: Theme.color.surface
                            TextInput { id: tiUser; anchors.fill: parent; anchors.leftMargin: Theme.spacing.md; anchors.rightMargin: Theme.spacing.md; verticalAlignment: Text.AlignVCenter; font.pixelSize: 14; color: Theme.color.textPrimary; clip: true; text: root.uiUser
                                onTextChanged: root.uiUser = text }
                            Text { anchors.left: parent.left; anchors.leftMargin: Theme.spacing.md; anchors.verticalCenter: parent.verticalCenter; text: "输入用户名"; font.pixelSize: 14; color: Theme.color.textSecondary; visible: tiUser.text === "" }
                        }
                    }
                    // 密码
                    Column { spacing: 4; anchors.left: parent.left; anchors.right: parent.right
                        Text { text: "密码"; font.pixelSize: 12; color: Theme.color.textSecondary }
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; height: 44; radius: Theme.radius.sm; color: Theme.color.surface
                            TextInput { id: tiPass; anchors.fill: parent; anchors.leftMargin: Theme.spacing.md; anchors.rightMargin: Theme.spacing.md; verticalAlignment: Text.AlignVCenter; font.pixelSize: 14; color: Theme.color.textPrimary; clip: true; echoMode: TextInput.Password; text: root.uiPass
                                onTextChanged: root.uiPass = text
                                onAccepted: doLogin() }
                            Text { anchors.left: parent.left; anchors.leftMargin: Theme.spacing.md; anchors.verticalCenter: parent.verticalCenter; text: "输入密码"; font.pixelSize: 14; color: Theme.color.textSecondary; visible: tiPass.text === "" }
                        }
                    }

                    // 错误条
                    Text {
                        id: loginErr
                        anchors.left: parent.left; anchors.right: parent.right
                        text: ""
                        font.pixelSize: 12; color: Theme.color.destructive
                        wrapMode: Text.Wrap
                        visible: text !== ""
                    }

                    // 登录按钮
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; height: 46; radius: Theme.radius.full
                        color: canLogin ? Theme.accentStrong : Theme.color.surfaceVariant
                        Text { anchors.centerIn: parent; text: root.loggingIn ? "登录中…" : "登录"; font.pixelSize: 14; font.weight: Font.Medium; color: canLogin ? Theme.accentStrongForeground : Theme.color.textSecondary }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: doLogin()
                        }
                    }
                }
            }

            // ── 已登录：排期（按日隔离 + 拖拽）──
            Flickable {
                id: schedFlick
                anchors.fill: parent
                visible: club && club.loggedIn
                contentWidth: width
                contentHeight: schedCol.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: schedCol
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: Theme.spacing.lg
                    spacing: Theme.spacing.md

                    // 日期切换条
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

                    // 列表标题
                    Text {
                        anchors.left: parent.left
                        text: "排期 · " + fmtChip(root.selectedDate)
                              + (isTodayStr(root.selectedDate) ? "（今天）" : "")
                              + "（" + dayModel.count + "）"
                              + (root.saving ? " · 保存中…" : "")
                        font.pixelSize: 14; font.weight: Font.Medium; color: Theme.color.textSecondary
                    }

                    // 排期列表（可拖拽，仅当日）
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
                                // 底：卡片浮在页面(background)之上 ⇒ 用 surface。
                                // ⚠️ 不能写 background（与页面同色 ⇒ 卡片整个消失，2026-10-30 截图实证）。
                                color: Theme.color.surface
                                opacity: held ? 0.72 : 1
                                Behavior on opacity { NumberAnimation { duration: 120 } }
                                property bool held: ma.drag.active

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

                                Column {
                                    anchors.left: cover.right; anchors.leftMargin: Theme.spacing.md
                                    anchors.right: statusChip.left; anchors.rightMargin: Theme.spacing.sm
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 3
                                    Text { text: model.title; font.pixelSize: 14; font.weight: Font.Medium; color: Theme.color.textPrimary; elide: Text.ElideRight; width: parent.width }
                                    Text { text: model.artist + (model.artist ? " · " : "") + platformName(model.platform) + " · " + fmtDur(model.dur); font.pixelSize: 12; color: Theme.color.textSecondary; elide: Text.ElideRight; width: parent.width }
                                }

                                Rectangle {
                                    id: statusChip
                                    anchors.right: parent.right; anchors.rightMargin: Theme.spacing.md
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 44; height: 24; radius: Theme.radius.full
                                    color: statusOf(model).bg
                                    Text { anchors.centerIn: parent; text: statusOf(model).text; font.pixelSize: 11; color: statusOf(model).fg }
                                }

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
                                    property real pressY: 0
                                    onPressed: pressY = card.y
                                    onReleased: (mouse) => { // Qt6.12：注入 mouse 形参已废弃（松手即打 deprecation qWarning），按官方形式改箭头函数显式形参，逻辑等价
                                        var idx = -1
                                        if (!root.saving) {
                                            var mp = ma.mapToItem(schedList, mouse.x, mouse.y)
                                            idx = Math.floor((schedList.contentY + mp.y)
                                                            / (card.height + schedList.spacing))
                                            idx = Math.max(0, Math.min(dayModel.count - 1, idx))
                                        }
                                        if (idx >= 0 && idx !== myIndex) {
                                            dayModel.move(myIndex, idx, 1)
                                            var pairs = []
                                            for (var k = 0; k < dayModel.count; ++k) {
                                                var it = dayModel.get(k)
                                                pairs.push({ id: it.sid, sequence: k + 1 })
                                            }
                                            if (club && club.loggedIn) {
                                                root.saving = true
                                                club.reorder(pairs)
                                            } else {
                                                snack.show("未登录，顺序仅在本地面板内生效", false)
                                            }
                                        } else {
                                            card.y = pressY
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
                        text: "提示：拖动曲目即可调整顺序，松手保存到云端（仅对当日生效）。"
                        font.pixelSize: 11; color: Theme.color.textSecondary
                        wrapMode: Text.Wrap; width: parent.width
                    }
                }
            }
        }
    }

    property bool canLogin: (club !== null) && (tiUser.text.trim() !== "") && (tiPass.text !== "") && !loggingIn

    function doLogin() {
        if (!club) return
        if (tiUser.text.trim() === "" || tiPass.text === "") { loginErr.text = "用户名与密码不能为空"; return }
        root.loggingIn = true
        loginErr.text = ""
        club.login(tiUser.text, tiPass.text)
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
}
