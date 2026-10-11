// 星集控 · 手机端主界面（2026-10-11 · 手机 App 并入管理端 S2）
//
// 用户裁决：手机 App 不新建工程，使用管理端 viewer-qt 同款代码。
// 本文件 = 手机端独立布局（复用管理端全部 components + viewerbackend），
// 桌面 Main.qml 照旧不动（避免与 WorkBuddy 未提交的集控重构冲突）。
//
// 设计（设计文档 §1/§4 同 mobile 版）：
//   · 底部四页：概览 / 设备 / 监控 / 我的
//   · 轻指令：ping / 关机（确认弹窗）/ 锁屏 / 重启（管理员）
//   · 监控页：RtcVideoSurface 实时画面（viewerbackend rtcFrameReady 接线）
//   · 响应式：width 自适应（竖屏 320~430，桌面窗口拉窄也能用）
// 不做：远控 / 语音对讲 / 广播 / 课表编辑 / 班级批量（桌面专属）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

ApplicationWindow {
    id: root
    width: Math.min(430, Math.max(320, Screen.width))
    height: Math.min(930, Math.max(640, Screen.height))
    visible: true
    title: "星集控 · 手机端"
    color: root.th.win
    // ⚠️ Fluent 风格下 ApplicationWindow 背景色由系统控制，这里显式铺一层
    background: Rectangle { color: root.th.win }

    Theme { id: theme; dark: root.darkMode }
    readonly property var th: theme
    property bool darkMode: false

    // 关机确认目标（轻指令共用弹窗）
    property string confirmTarget: ""
    property string confirmAction: "shutdown"

    // backend 由 main.cpp 注入（ViewerBackend 实例，同一连接）

    // ── 顶部条 ──
    Rectangle {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 52
        color: root.th.panel
        border.color: root.th.stroke
        border.width: 1

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: "星集控"
            color: root.th.fg
            font.pixelSize: 17
            font.weight: Font.Medium
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Rectangle {
                width: 6; height: 6; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                color: backend.connected ? root.th.ok : root.th.err
            }
            Text {
                text: backend.loggedIn ? (backend.connected ? "已连接" : "重连中…") : "未登录"
                color: root.th.fg3
                font.pixelSize: 11
            }
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
                anchors.margins: 14
                spacing: 10

                // 概览卡（复用 Card 组件样式：面板+圆角）
                Rectangle {
                    width: parent.width
                    height: 84
                    radius: root.th.rCard
                    color: root.th.panel
                    border.color: root.th.stroke
                    border.width: 1
                    Row {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            Text { text: "教室在线"; color: root.th.fg3; font.pixelSize: 12 }
                            Text { text: backend.onlineCount + " 台"; color: root.th.fg; font.pixelSize: 22; font.weight: Font.Medium }
                        }
                        Item { width: 1; height: 1; Layout.fillWidth: false }
                        Column {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            Text { text: "设备总数"; color: root.th.fg3; font.pixelSize: 12 }
                            Text { text: (root.devices ? root.devices.length : 0) + " 台"; color: root.th.fg; font.pixelSize: 18 }
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    height: 84
                    radius: root.th.rCard
                    color: root.th.panel
                    border.color: root.th.stroke
                    border.width: 1
                    Row {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 8
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            Text { text: "连接状态"; color: root.th.fg3; font.pixelSize: 12 }
                            Text {
                                text: backend.loggedIn ? (backend.connected ? "云端就绪" : "连接断开") : "未登录"
                                color: backend.connected ? root.th.ok : root.th.err
                                font.pixelSize: 18; font.weight: Font.Medium
                            }
                        }
                        Item { width: 1; height: 1 }
                        Column {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            Text { text: "账户"; color: root.th.fg3; font.pixelSize: 12 }
                            Text { text: backend.accountName ? backend.accountName : "未登录"; color: root.th.fg; font.pixelSize: 13; elide: Text.ElideRight }
                        }
                    }
                }
                Text {
                    text: "手机端为管理端伴侣：查看教室状态、发轻指令。\n远控 / 语音 / 广播 / 课表请用桌面管理端。"
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
            model: root.devices
            delegate: Item {
                width: deviceList.width
                height: 68
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    radius: root.th.rCard
                    color: root.th.panel
                    border.color: root.th.stroke
                    border.width: 1
                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: (modelData.online !== false) ? root.th.ok : root.th.err
                        }
                        Column {
                            width: parent.width - 150
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            Text {
                                text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                                color: root.th.fg; font.pixelSize: 14; elide: Text.ElideRight; width: parent.width
                            }
                            Text {
                                text: (modelData.version ? "v" + modelData.version : "") + " · " + modelData.uid
                                color: root.th.fg3; font.pixelSize: 11; elide: Text.ElideRight; width: parent.width
                            }
                        }
                        // 轻指令：ping / 关机（行内小按钮）
                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            Rectangle {
                                width: 40; height: 26; radius: 13
                                color: root.th.hover; border.color: root.th.stroke; border.width: 1
                                Text { anchors.centerIn: parent; text: "ping"; color: root.th.acc; font.pixelSize: 11 }
                                MouseArea { anchors.fill: parent; onClicked: backend.sendAction("ping", {}) }
                            }
                            Rectangle {
                                width: 40; height: 26; radius: 13
                                color: root.th.err
                                Text { anchors.centerIn: parent; text: "关机"; color: "#fff"; font.pixelSize: 11 }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: { root.confirmTarget = modelData.uid; root.confirmAction = "shutdown"; confirmDialog.open() }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── 页 2：监控（实时画面）──
        Item {
            // 设备选择横条
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 44
                color: root.th.body
                Row {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    Text { text: "看哪台："; color: root.th.fg3; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    ListView {
                        id: watchList
                        orientation: ListView.Horizontal
                        clip: true
                        width: parent.width - 48
                        height: 34
                        anchors.verticalCenter: parent.verticalCenter
                        model: root.devices
                        delegate: Rectangle {
                            height: 30
                            width: Math.max(64, devLbl.implicitWidth + 16)
                            radius: 15
                            anchors.verticalCenter: parent.verticalCenter
                            color: (backend.currentUid === modelData.uid) ? root.th.inv : root.th.hover
                            border.color: (backend.currentUid === modelData.uid) ? root.th.inv : root.th.stroke
                            border.width: 1
                            Text {
                                id: devLbl
                                anchors.centerIn: parent
                                text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                                color: (backend.currentUid === modelData.uid) ? root.th.win : root.th.fg3
                                font.pixelSize: 11
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: { backend.currentUid = modelData.uid; backend.sendAction("camera_start", {}) }
                            }
                        }
                    }
                }
            }
            // 实时画面
            RtcVideoSurface {
                id: vidSurf
                anchors.top: parent.top
                anchors.topMargin: 50
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 14
                theme: root.th
            }
            // 停止画面按钮
            Rectangle {
                anchors.top: parent.top
                anchors.topMargin: 50 + 8
                anchors.right: parent.right
                anchors.rightMargin: 14
                width: 64; height: 26; radius: 13
                color: root.th.hover; border.color: root.th.stroke; border.width: 1
                Text { anchors.centerIn: parent; text: "停止"; color: root.th.fg3; font.pixelSize: 11 }
                MouseArea { anchors.fill: parent; onClicked: backend.sendAction("camera_stop", {}) }
            }
        }

        // ── 页 3：我的 ──
        Flickable {
            contentHeight: mineCol.height + 24
            clip: true
            Column {
                id: mineCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 10

                Text { text: "账户"; color: root.th.fg3; font.pixelSize: 12 }
                Rectangle {
                    width: parent.width; height: 60; radius: root.th.rCard
                    color: root.th.panel; border.color: root.th.stroke; border.width: 1
                    Row {
                        anchors.fill: parent; anchors.margins: 16; spacing: 10
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text { text: backend.accountName ? backend.accountName : "未登录"; color: root.th.fg; font.pixelSize: 15; font.weight: Font.Medium }
                            Text { text: backend.role === "admin" ? "管理员" : "教师"; color: root.th.fg3; font.pixelSize: 12 }
                        }
                        Item { width: 1; height: 1 }
                        Rectangle {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 96; height: 30; radius: 15
                            color: root.th.inv
                            Text { anchors.centerIn: parent; text: "网站账号登录"; color: root.th.win; font.pixelSize: 12 }
                            MouseArea { anchors.fill: parent; onClicked: backend.loginWithSite() }
                        }
                    }
                }

                Text { text: "身份"; color: root.th.fg3; font.pixelSize: 12; anchors.topMargin: 6 }
                Row {
                    spacing: 8
                    Rectangle {
                        width: 110; height: 34; radius: 17
                        color: (backend.role === "admin") ? root.th.inv : root.th.hover
                        border.color: root.th.stroke; border.width: 1
                        Text { anchors.centerIn: parent; text: "管理员"; color: (backend.role === "admin") ? root.th.win : root.th.fg3; font.pixelSize: 12 }
                        MouseArea { anchors.fill: parent; onClicked: backend.setRole("admin") }
                    }
                    Rectangle {
                        width: 110; height: 34; radius: 17
                        color: (backend.role !== "admin") ? root.th.inv : root.th.hover
                        border.color: root.th.stroke; border.width: 1
                        Text { anchors.centerIn: parent; text: "教师"; color: (backend.role !== "admin") ? root.th.win : root.th.fg3; font.pixelSize: 12 }
                        MouseArea { anchors.fill: parent; onClicked: backend.setRole("teacher") }
                    }
                }

                Text { text: "外观"; color: root.th.fg3; font.pixelSize: 12; anchors.topMargin: 6 }
                Row {
                    spacing: 8
                    Rectangle {
                        width: 110; height: 34; radius: 17
                        color: !root.darkMode ? root.th.inv : root.th.hover
                        border.color: root.th.stroke; border.width: 1
                        Text { anchors.centerIn: parent; text: "浅色"; color: !root.darkMode ? root.th.win : root.th.fg3; font.pixelSize: 12 }
                        MouseArea { anchors.fill: parent; onClicked: root.darkMode = false }
                    }
                    Rectangle {
                        width: 110; height: 34; radius: 17
                        color: root.darkMode ? root.th.inv : root.th.hover
                        border.color: root.th.stroke; border.width: 1
                        Text { anchors.centerIn: parent; text: "深色"; color: root.darkMode ? root.th.win : root.th.fg3; font.pixelSize: 12 }
                        MouseArea { anchors.fill: parent; onClicked: root.darkMode = true }
                    }
                }

                Text { text: "版本"; color: root.th.fg3; font.pixelSize: 12; anchors.topMargin: 6 }
                Text { text: "v" + backend.version; color: root.th.fg; font.pixelSize: 13 }
            }
        }
    }

    // ── 底部标签 ──
    Row {
        id: tabs
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 54
        property int currentIndex: 0
        // 四个标签（用 Repeater 避免重复）
        Repeater {
            model: ["概览", "设备", "监控", "我的"]
            delegate: Item {
                width: tabs.width / 4
                height: tabs.height
                Rectangle {
                    anchors.fill: parent
                    color: (tabs.currentIndex === index) ? root.th.cream : root.th.panel
                    border.color: root.th.sepline
                    border.width: (index === 0) ? 1 : 0
                }
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: (tabs.currentIndex === index) ? root.th.inv : root.th.fg3
                    font.pixelSize: 13
                    font.weight: (tabs.currentIndex === index) ? Font.Medium : Font.Normal
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: tabs.currentIndex = index
                }
            }
        }
    }

    // ── 关机/锁屏确认弹窗 ──
    Dialog {
        id: confirmDialog
        anchors.centerIn: parent
        modal: true
        title: "确认操作"
        standardButtons: Dialog.NoButton
        contentItem: Column {
            spacing: 12
            Text {
                text: "对 " + root.confirmTarget + " 执行「" + (root.confirmAction === "shutdown" ? "关机" : "锁屏") + "」？"
                color: root.th.fg; font.pixelSize: 14; wrapMode: Text.Wrap; width: 240
            }
            Row {
                spacing: 10
                Rectangle {
                    width: 110; height: 34; radius: 17; color: root.th.hover
                    border.color: root.th.stroke; border.width: 1
                    Text { anchors.centerIn: parent; text: "取消"; color: root.th.fg3; font.pixelSize: 12 }
                    MouseArea { anchors.fill: parent; onClicked: confirmDialog.close() }
                }
                Rectangle {
                    width: 110; height: 34; radius: 17; color: root.th.err
                    Text { anchors.centerIn: parent; text: "确认"; color: "#fff"; font.pixelSize: 12 }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            backend.currentUid = root.confirmTarget
                            backend.sendAction(root.confirmAction, {})
                            root.toast("已下发「" + root.confirmAction + "」→ " + root.confirmTarget)
                            confirmDialog.close()
                        }
                    }
                }
            }
        }
    }

    // ── 轻提示（复用灵动岛思路的简单 toast）──
    function toast(s) {
        tipText.text = s
        tipTimer.restart()
    }
    Rectangle {
        id: tipBox
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: tabs.top
        anchors.bottomMargin: 12
        width: tipText.implicitWidth + 24
        height: 30
        radius: 15
        color: root.th.inv
        visible: tipText.text !== ""
        z: 10
        Text {
            id: tipText
            anchors.centerIn: parent
            color: root.th.win
            font.pixelSize: 12
            text: ""
        }
    }
    Timer {
        id: tipTimer
        interval: 2000
        onTriggered: tipText.text = ""
    }

    // ── 设备表刷新（下拉/自动）──
    Timer {
        interval: 30000
        repeat: true
        running: true
        onTriggered: backend.requestDevices()
    }

    Component.onCompleted: {
        backend.requestDevices()
        // 实时画面接线：viewerbackend rtcFrameReady → RtcVideoSurface
        backend.rtcFrameReady.connect(function (img) { vidSurf.onFrame(img) })
    }
}
