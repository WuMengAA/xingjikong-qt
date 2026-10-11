// 星集控 · 手机端我的页（2026-10-11 · 独立 QML 方案）
// 页 3：账户（OAuth 登录态）/ 身份 / 外观（深浅三档）/ 最近告警 / 录制回放 / 版本。
// recSummary / recentEvents / recBusy 由 Main 传入（数据在 Main 汇总）。
import QtQuick

Item {
    id: page
    property var th: ({})
    property var recSummary: []
    property var recentEvents: []
    property bool recBusy: false

    Flickable {
        anchors.fill: parent
        contentHeight: col.height + 24
        clip: true
        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10

            // ── 账户（OAuth 登录态）──
            Text { text: "账户"; color: page.th.fg3; font.pixelSize: 12 }
            Rectangle {
                width: parent.width; height: 60; radius: page.th.rCard
                color: page.th.panel; border.color: page.th.stroke; border.width: 1
                Row {
                    anchors.fill: parent; anchors.margins: 16; spacing: 10
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Text { text: backend.accountName ? backend.accountName : "未登录"; color: page.th.fg; font.pixelSize: 15; font.weight: Font.Medium }
                        Text {
                            text: backend.accountBusy ? "登录中…" : (backend.role === "admin" ? "管理员" : "教师")
                            color: page.th.fg3; font.pixelSize: 12
                        }
                    }
                    // 登录按钮带 busy 态（OAuth 浏览器回呼等待时禁用）
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 96; height: 30; radius: 15
                        color: backend.accountBusy ? page.th.fg4 : page.th.inv
                        Text {
                            anchors.centerIn: parent
                            text: backend.accountBusy ? "等待登录…" : "网站账号登录"
                            color: page.th.win; font.pixelSize: 12
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: !backend.accountBusy
                            onClicked: backend.loginWithSite()
                        }
                    }
                }
            }

            // ── 身份 ──
            Text { text: "身份"; color: page.th.fg3; font.pixelSize: 12; anchors.topMargin: 6 }
            Row {
                spacing: 8
                Rectangle {
                    width: 110; height: 34; radius: 17
                    color: (backend.role === "admin") ? page.th.inv : page.th.hover
                    border.color: page.th.stroke; border.width: 1
                    Text { anchors.centerIn: parent; text: "管理员"; color: (backend.role === "admin") ? page.th.win : page.th.fg3; font.pixelSize: 12 }
                    MouseArea { anchors.fill: parent; onClicked: backend.setRole("admin") }
                }
                Rectangle {
                    width: 110; height: 34; radius: 17
                    color: (backend.role !== "admin") ? page.th.inv : page.th.hover
                    border.color: page.th.stroke; border.width: 1
                    Text { anchors.centerIn: parent; text: "教师"; color: (backend.role !== "admin") ? page.th.win : page.th.fg3; font.pixelSize: 12 }
                    MouseArea { anchors.fill: parent; onClicked: backend.setRole("teacher") }
                }
            }

            // ── 外观（E4 三档）──
            Text { text: "外观"; color: page.th.fg3; font.pixelSize: 12; anchors.topMargin: 6 }
            Row {
                spacing: 8
                Repeater {
                    model: [
                        { k: "浅色", m: 0 },
                        { k: "深色", m: 1 },
                        { k: "跟随系统", m: 2 }
                    ]
                    delegate: Rectangle {
                        width: 100; height: 34; radius: 17
                        color: (backend.themeMode === modelData.m) ? page.th.inv : page.th.hover
                        border.color: page.th.stroke; border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: modelData.k
                            color: (backend.themeMode === modelData.m) ? page.th.win : page.th.fg3
                            font.pixelSize: 12
                            font.weight: (backend.themeMode === modelData.m) ? Font.Medium : Font.Normal
                        }
                        MouseArea { anchors.fill: parent; onClicked: backend.themeMode = modelData.m }
                    }
                }
            }

            // ── 最近告警（/api/events 前 8 条）──
            Text { text: "最近告警"; color: page.th.fg3; font.pixelSize: 12; anchors.topMargin: 6 }
            Rectangle {
                width: parent.width; radius: page.th.rCard
                color: page.th.panel; border.color: page.th.stroke; border.width: 1
                implicitHeight: Math.max(evCol.height + 16, 40)
                Column {
                    id: evCol
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top; anchors.margins: 12
                    spacing: 8
                    Repeater {
                        model: page.recentEvents.slice(0, 8)
                        delegate: Row {
                            width: parent.width; spacing: 8
                            Rectangle {
                                width: 6; height: 6; radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                color: (modelData.level === "error") ? page.th.err : page.th.ok
                            }
                            Column {
                                width: parent.width - 60
                                Text {
                                    text: modelData.msg
                                    color: page.th.fg; font.pixelSize: 13; elide: Text.ElideRight; width: parent.width
                                }
                                Text {
                                    text: (modelData.uid ? modelData.uid + " · " : "") + modelData.time
                                    color: page.th.fg4; font.pixelSize: 10; elide: Text.ElideRight; width: parent.width
                                }
                            }
                        }
                    }
                    Text {
                        visible: page.recentEvents.length === 0
                        text: "暂无事件"
                        color: page.th.fg4; font.pixelSize: 11
                    }
                }
            }

            // ── 监控摘要 · 录制回放 ──
            Text { text: "监控摘要 · 录制回放"; color: page.th.fg3; font.pixelSize: 12; anchors.topMargin: 6 }
            Rectangle {
                width: parent.width; radius: page.th.rCard
                color: page.th.panel; border.color: page.th.stroke; border.width: 1
                implicitHeight: recCol.height + 20
                Column {
                    id: recCol
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top; anchors.margins: 12
                    spacing: 8
                    Rectangle {
                        width: parent.width; height: 30; radius: 15
                        color: page.th.hover; border.color: page.th.stroke; border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: page.recBusy ? "拉取中…" : "拉取录制列表"
                            color: page.th.fg3; font.pixelSize: 12
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: !page.recBusy
                            onClicked: { page.recBusy = true; backend.fetchRecordings() }
                        }
                    }
                    Repeater {
                        model: page.recSummary
                        delegate: Row {
                            width: parent.width; spacing: 8
                            Text { text: modelData.uid; color: page.th.fg; font.pixelSize: 13; width: parent.width * 0.4; elide: Text.ElideRight }
                            Text { text: modelData.count + " 个录制"; color: page.th.fg3; font.pixelSize: 12 }
                            Text { text: modelData.last; color: page.th.fg4; font.pixelSize: 11 }
                        }
                    }
                    Text {
                        visible: page.recSummary.length === 0 && !page.recBusy
                        text: "暂无录制（或被控端未开录制）"
                        color: page.th.fg4; font.pixelSize: 11
                    }
                }
            }

            // ── 版本 ──
            Text { text: "版本"; color: page.th.fg3; font.pixelSize: 12; anchors.topMargin: 6 }
            Text { text: "v" + backend.version; color: page.th.fg; font.pixelSize: 13 }
        }
    }
}
