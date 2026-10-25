// 星集控 · 手机端我的页（2026-10-13 · MD3 迁移批）
// 页 3：账户（OAuth 登录态）/ 身份 / 外观（深浅三档）/ 最近告警 / 录制回放 / 版本。
// recSummary / recentEvents / recBusy 由 Main 传入（数据在 Main 汇总）。
// MD3：分组卡片 + filter chip 选择 + filled/tonal 按钮 + 字阶。
import QtQuick
import "../components/md3"
import "../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: page
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
            anchors.margins: 16
            spacing: 12

            // ── 账户（OAuth 登录态）──
            Text { text: "账户"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
            Md3Card {
                variant: "filled"
                width: parent.width
                Row {
                    width: parent.width
                    spacing: 12
                    // 头像（首字母圆）
                    Rectangle {
                        width: 40; height: 40; radius: 20
                        anchors.verticalCenter: parent.verticalCenter
                        color: Md3.cPrimaryCont
                        Text {
                            anchors.centerIn: parent
                            text: backend.accountName ? backend.accountName.charAt(0) : "?"
                            font.pixelSize: Md3.titleM
                            font.weight: Font.Medium
                            color: Md3.cOnPrimaryCont
                        }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text {
                            text: backend.accountName ? backend.accountName : "未登录"
                            color: Md3.cOnSurface
                            font.pixelSize: Md3.titleM
                            font.weight: Font.Medium
                        }
                        Text {
                            text: backend.accountBusy ? "登录中…" : (backend.role === "admin" ? "管理员" : "教师")
                            color: Md3.cOnSurfaceVar
                            font.pixelSize: Md3.bodyS
                        }
                    }
                    Item { width: Math.max(4, parent.width - 220); height: 1 }
                    // 登录按钮带 busy 态（OAuth 浏览器回呼等待时禁用）
                    Md3Btn {
                        anchors.verticalCenter: parent.verticalCenter
                        text: backend.accountBusy ? "等待登录…" : "网站账号登录"
                        variant: "filled"
                        enabled: !backend.accountBusy
                        onClicked: backend.loginWithSite()
                    }
                }
            }

            // ── 身份（MD3 filter chip）──
            Text { text: "身份"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
            Row {
                spacing: 8
                Md3Chip {
                    text: "管理员"
                    selected: backend.role === "admin"
                    onClicked: backend.setRole("admin")
                }
                Md3Chip {
                    text: "教师"
                    selected: backend.role !== "admin"
                    onClicked: backend.setRole("teacher")
                }
            }

            // ── 外观（E4 三档 · MD3 filter chip）──
            Text { text: "外观"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
            Row {
                spacing: 8
                Repeater {
                    model: [
                        { k: "浅色", m: 0 },
                        { k: "深色", m: 1 },
                        { k: "跟随系统", m: 2 }
                    ]
                    delegate: Md3Chip {
                        text: modelData.k
                        selected: backend.themeMode === modelData.m
                        onClicked: backend.themeMode = modelData.m
                    }
                }
            }

            // ── 最近告警（/api/events 前 8 条）──
            Text { text: "最近告警"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
            Md3Card {
                variant: "filled"
                width: parent.width
                Column {
                    width: parent.width
                    spacing: 10
                    Repeater {
                        model: page.recentEvents.slice(0, 8)
                        delegate: Row {
                            width: parent.width
                            spacing: 10
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                anchors.verticalCenter: parent.verticalCenter
                                color: (modelData.level === "error") ? Md3.cError : Md3.cOk
                            }
                            Column {
                                width: parent.width - 18
                                Text {
                                    text: modelData.msg
                                    color: Md3.cOnSurface
                                    font.pixelSize: Md3.bodyM
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                                Text {
                                    text: (modelData.uid ? modelData.uid + " · " : "") + modelData.time
                                    color: Md3.cOutline
                                    font.pixelSize: Md3.labelS
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                            }
                        }
                    }
                    Text {
                        visible: page.recentEvents.length === 0
                        text: "暂无事件"
                        color: Md3.cOutline
                        font.pixelSize: Md3.bodyS
                    }
                }
            }

            // ── 监控摘要 · 录制回放 ──
            Text { text: "监控摘要 · 录制回放"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
            Md3Card {
                variant: "filled"
                width: parent.width
                Column {
                    width: parent.width
                    spacing: 10
                    Md3Btn {
                        text: page.recBusy ? "拉取中…" : "拉取录制列表"
                        variant: "tonal"
                        enabled: !page.recBusy
                        onClicked: { page.recBusy = true; backend.fetchRecordings() }
                    }
                    Repeater {
                        model: page.recSummary
                        delegate: Row {
                            width: parent.width
                            spacing: 8
                            Text { text: modelData.uid; color: Md3.cOnSurface; font.pixelSize: Md3.bodyM; width: parent.width * 0.4; elide: Text.ElideRight }
                            Text { text: modelData.count + " 个录制"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.bodyS }
                            Text { text: modelData.last; color: Md3.cOutline; font.pixelSize: Md3.labelS }
                        }
                    }
                    Text {
                        visible: page.recSummary.length === 0 && !page.recBusy
                        text: "暂无录制（或被控端未开录制）"
                        color: Md3.cOutline
                        font.pixelSize: Md3.bodyS
                    }
                }
            }

            // ── 版本 ──
            Text { text: "版本"; color: Md3.cOnSurfaceVar; font.pixelSize: Md3.labelM }
            Text { text: "v" + backend.version; color: Md3.cOnSurface; font.pixelSize: Md3.bodyM }
        }
    }
}
