// 手机端主界面（2026-10-09 · 设计文档 §1/§4）
// 底部标签四页：概览 / 设备 / 监控 / 我的。轻量操作，不做远控/语音/广播。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: root
    width: 390
    height: 844
    visible: true
    title: "星集控 · 手机端"
    color: root.th.win

    Theme { id: theme }
    readonly property var th: theme.th

    // 关机确认目标 uid（P1 轻指令：关机弹窗共用）
    property string confirmTarget: ""

    // P2 监控摘要（backend.fetchEvents/fetchRecordings 填充）
    property var monitorEvents: []
    property var monitorRecordings: []

    // backend 由 main.cpp 注入（C++ 实例，避免双份连接）

    // ── 顶部条 ──
    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56
        color: root.th.panel
        border.color: root.th.stroke
        border.width: 1

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: "星集控"
            color: root.th.fg
            font.pixelSize: 17
            font.weight: Font.Medium
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: backend.status
            color: backend.authed ? root.th.ok : root.th.fg4
            font.pixelSize: 11
        }
    }

    // ── 页容器 ──
    StackLayout {
        id: pages
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: tabs.top
        currentIndex: tabs.currentIndex

        // ── 页 0：概览 ──
        Flickable {
            contentHeight: overviewCol.height + 24
            clip: true
            Column {
                id: overviewCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 12

                MobileCard {
                    title: "教室在线"
                    sub: "设备总数 " + backend.devices.length + " 台"
                    value: backend.onlineCount + " 在线"
                }
                MobileCard {
                    title: "连接状态"
                    sub: backend.authed ? "已鉴权 · 云端就绪" : "未连接云端"
                    value: backend.authed ? "就绪" : "离线"
                }
                Text {
                    text: "手机端为管理端伴侣：查看教室状态、发轻指令。\n远控 / 语音 / 广播请用桌面管理端。"
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: root.th.fg4
                    font.pixelSize: 11
                }
            }
        }

        // ── 页 1：设备 ──
        ListView {
            id: deviceList
            clip: true
            model: backend.devices
            delegate: Item {
                width: deviceList.width
                height: 64
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    radius: root.th.rCard
                    color: root.th.panel
                    border.color: root.th.stroke
                    border.width: 1
                    Row {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10
                        StatusDot {
                            anchors.verticalCenter: parent.verticalCenter
                            theme: root.th
                            state: modelData.online ? 1 : 0
                        }
                        Column {
                            width: parent.width - 40
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text {
                                text: modelData.uid
                                color: root.th.fg
                                font.pixelSize: 14
                                elide: Text.ElideRight
                                width: parent.width
                            }
                            Text {
                                text: (modelData.name || "未命名") + " · v" + (modelData.version || "-")
                                color: root.th.fg3
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }
                    }
                }
                // 轻指令：ping（点行 ping）/ 关机（显式按钮 + 确认弹窗）
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Rectangle {
                        width: 44; height: 26; radius: 13
                        color: root.th.panel2
                        border.color: root.th.stroke
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "ping"
                            color: root.th.acc
                            font.pixelSize: 11
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: backend.pingDevice(modelData.uid)
                        }
                    }
                    Rectangle {
                        width: 44; height: 26; radius: 13
                        color: root.th.err
                        Text {
                            anchors.centerIn: parent
                            text: "关机"
                            color: "#fff"
                            font.pixelSize: 11
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.confirmTarget = modelData.uid
                                confirmDialog.open()
                            }
                        }
                    }
                }
            }
        }

        // ── 关机确认弹窗（P1：关机必须确认，防误触）──
        Dialog {
            id: confirmDialog
            title: "关机确认"
            modal: true
            anchors.centerIn: parent
            standardButtons: Dialog.No | Dialog.Yes
            onAccepted: {
                if (root.confirmTarget !== "") backend.shutdownDevice(root.confirmTarget)
                root.confirmTarget = ""
            }
            onRejected: root.confirmTarget = ""
            Label {
                text: "确定要关闭设备 " + (root.confirmTarget || "?") + " 吗？\n（关机后需人工重启）"
                wrapMode: Text.Wrap
                width: 240
            }
        }

        // ── 页 2：监控（P2 摘要 + 回放列表，2026-10-10）──
        Flickable {
            contentHeight: monitorCol.height + 24
            clip: true
            Column {
                id: monitorCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 12

                Text {
                    text: "实时事件摘要（最近 30 条）"
                    color: root.th.fg3
                    font.pixelSize: 12
                }
                // 事件列表
                Repeater {
                    model: monitorEvents
                    delegate: MobileCard {
                        title: modelData.type || "事件"
                        sub: (modelData.detail || "") + " · " + (modelData.at || "")
                        value: modelData.level || ""
                    }
                }
                Text {
                    text: "录制回放"
                    color: root.th.fg3
                    font.pixelSize: 12
                }
                Repeater {
                    model: monitorRecordings
                    delegate: MobileCard {
                        title: modelData.name || modelData.file || "录制"
                        sub: (modelData.size || "?") + " · " + (modelData.at || modelData.createdAt || "")
                        value: "→"
                    }
                }
                // 空态
                Text {
                    text: monitorEvents.length === 0 && monitorRecordings.length === 0
                          ? "暂无摘要/回放（设备未录制或未连接云端）"
                          : ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: root.th.fg4
                    font.pixelSize: 11
                }
            }
        }

        // ── 页 3：我的 ──
        Column {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 16
            spacing: 12
            MobileCard { title: "账号"; sub: "站点一户通（OAuth，待接入）"; value: "→" }
            MobileCard { title: "外观"; sub: "深浅色切换"; value: "→" }
            // P3：考试模式开关（破坏性——必须确认）
            MobileCard {
                title: "考试模式"
                sub: "全屏拦截 · 禁通知 · 禁监视（发给全部在线设备）"
                value: backend.onlineCount + " 台在线"
                MouseArea {
                    anchors.fill: parent
                    onClicked: examDialog.open()
                }
            }
        }
    }

    // ── P3 考试模式确认弹窗（破坏性，防误触）──
    Dialog {
        id: examDialog
        title: "考试模式"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.No | Dialog.Yes
        Label {
            text: "开启考试模式将：\n· 全屏拦截被控端操作\n· 禁止一切通知与监视\n· 影响全部 " + backend.onlineCount + " 台在线设备\n\n确认开启？"
            wrapMode: Text.Wrap
            width: 240
        }
        onAccepted: {
            // 发给全部在线设备
            for (var i = 0; i < backend.devices.length; ++i) {
                if (backend.devices[i].online)
                    backend.examMode(backend.devices[i].uid, true)
            }
        }
    }

    // ── 底部标签 ──
    TabBar {
        id: tabs
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 60
        background: Rectangle { color: root.th.panel }

        TabButton { text: "概览" }
        TabButton { text: "设备" }
        TabButton { text: "监控" }
        TabButton { text: "我的" }
    }

    Component.onCompleted: {
        // P2：backend 摘要信号 → 根属性（QML 显示）
        backend.eventsChanged.connect(function(ev) { root.monitorEvents = ev })
        backend.recordingsChanged.connect(function(rec) { root.monitorRecordings = rec })
        backend.start()
    }

    // 切到监控页（index 2）时拉取摘要（P2）
    Connections {
        target: tabs
        function onCurrentIndexChanged() {
            if (tabs.currentIndex === 2 && backend.authed) {
                backend.fetchEvents()
                backend.fetchRecordings()
            }
        }
    }

    // ── Qt 6.12 返回键适配（2026-10-10 · 用户强调的理解点）──
    // 6.12 起 Android 返回键默认 moveTaskToBack() 退后台（不再直接退出）。
    // 手机端语义：返回键 = 退到上一页（非首页）/ 首页再按 = 退后台（默认行为保留）。
    // ⚠️ Keys 只能挂 Item（ApplicationWindow 不是 Item）——挂窗口内容根。
    Item {
        anchors.fill: parent
        Keys.onBackPressed: {
            event.accepted = true
            if (pages.currentIndex !== 0) {
                pages.currentIndex = 0          // 非首页：回首页
                tabs.currentIndex = 0
            }
            // 首页按返回：默认 moveTaskToBack 退后台（不退出应用，保持会话）
        }
    }
}