// 星集控 · 手机端我的页 · QtShadcn 规范（2026-10-29 迁移）
// 页 3：账户（OAuth 登录态）/ 身份 / 外观（深浅三档）/ 最近告警 / 录制回放 / 版本。
// recSummary / recentEvents / recBusy 由 Main 传入（数据在 Main 汇总）。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · 头像原用 `cPrimaryCont`（浅紫底）—— Theme 无 container 层级 ⇒ 用 primary 叠 15% 透明派生
// · 字阶走规范 12/13/14/16
import QtQuick
import QtQuick.Layouts
import "../components"           // AppCard / AppButton / AppChip
import "../components/theme"    // Theme 令牌

Item {
    id: page
    property var recSummary: []
    property var recentEvents: []
    property bool recBusy: false

    /** 弱化标签色（分组小标题）。 */
    readonly property color _labelColor: Theme.color.textSecondary

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + Theme.spacing.lg
        clip: true
        ColumnLayout {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing.md
            spacing: Theme.spacing.sm + 4

            // ── 账户（OAuth 登录态）──
            Text { text: "账户"; color: page._labelColor; font.pixelSize: 13 }
            AppCard {
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.sm + 4

                    // 头像（首字母圆）
                    Rectangle {
                        width: 40; height: 40; radius: 20
                        Layout.alignment: Qt.AlignVCenter
                        color: Qt.rgba(Theme.color.primary.r, Theme.color.primary.g,
                                       Theme.color.primary.b, 0.15)
                        Text {
                            anchors.centerIn: parent
                            text: backend.accountName ? backend.accountName.charAt(0) : "?"
                            font.pixelSize: 16
                            font.weight: Font.Medium
                            color: Theme.color.primary
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: backend.accountName ? backend.accountName : "未登录"
                            color: Theme.color.textPrimary
                            font.pixelSize: 16
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: backend.accountBusy ? "登录中…" : (backend.role === "admin" ? "管理员" : "教师")
                            color: Theme.color.textSecondary
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                    // 登录按钮带 busy 态（OAuth 浏览器回呼等待时禁用）
                    AppButton {
                        text: backend.accountBusy ? "等待登录…" : "网站账号登录"
                        variant: "primary"
                        size: "sm"
                        enabled: !backend.accountBusy
                        onClicked: backend.loginWithSite()
                    }
                }
            }

            // ── 身份 ──
            Text { text: "身份"; color: page._labelColor; font.pixelSize: 13 }
            RowLayout {
                spacing: Theme.spacing.sm
                AppChip {
                    text: "管理员"
                    selected: backend.role === "admin"
                    onClicked: backend.setRole("admin")
                }
                AppChip {
                    text: "教师"
                    selected: backend.role !== "admin"
                    onClicked: backend.setRole("teacher")
                }
                Item { Layout.fillWidth: true }
            }

            // ── 外观（E4 三档）──
            Text { text: "外观"; color: page._labelColor; font.pixelSize: 13 }
            RowLayout {
                spacing: Theme.spacing.sm
                Repeater {
                    model: [
                        { k: "浅色", m: 0 },
                        { k: "深色", m: 1 },
                        { k: "跟随系统", m: 2 }
                    ]
                    delegate: AppChip {
                        required property var modelData
                        text: modelData.k
                        selected: backend.themeMode === modelData.m
                        onClicked: backend.themeMode = modelData.m
                    }
                }
                Item { Layout.fillWidth: true }
            }

            // ── 最近告警（/api/events 前 8 条）──
            Text { text: "最近告警"; color: page._labelColor; font.pixelSize: 13 }
            AppCard {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.sm
                    Repeater {
                        model: page.recentEvents.slice(0, 8)
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.spacing.sm
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                Layout.alignment: Qt.AlignVCenter
                                color: (modelData.level === "error") ? Theme.color.destructive
                                                                     : Theme.color.success
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.msg
                                    color: Theme.color.textPrimary
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: (modelData.uid ? modelData.uid + " · " : "") + modelData.time
                                    color: Theme.color.textSecondary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    Text {
                        visible: page.recentEvents.length === 0
                        text: "暂无事件"
                        color: Theme.color.textSecondary
                        font.pixelSize: 13
                    }
                }
            }

            // ── 监控摘要 · 录制回放 ──
            Text { text: "监控摘要 · 录制回放"; color: page._labelColor; font.pixelSize: 13 }
            AppCard {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.sm
                    AppButton {
                        text: page.recBusy ? "拉取中…" : "拉取录制列表"
                        variant: "secondary"
                        enabled: !page.recBusy
                        onClicked: { page.recBusy = true; backend.fetchRecordings() }
                    }
                    Repeater {
                        model: page.recSummary
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: Theme.spacing.sm
                            Text {
                                Layout.preferredWidth: 100
                                text: modelData.uid
                                color: Theme.color.textPrimary
                                font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.count + " 个录制"
                                color: Theme.color.textSecondary
                                font.pixelSize: 13
                            }
                            Text {
                                text: modelData.last
                                color: Theme.color.textSecondary
                                font.pixelSize: 12
                            }
                        }
                    }
                    Text {
                        visible: page.recSummary.length === 0 && !page.recBusy
                        text: "暂无录制（或被控端未开录制）"
                        color: Theme.color.textSecondary
                        font.pixelSize: 13
                    }
                }
            }

            // ── 版本 ──
            Text { text: "版本"; color: page._labelColor; font.pixelSize: 13 }
            Text { text: "v" + backend.version; color: Theme.color.textPrimary; font.pixelSize: 14 }

            Item { Layout.fillHeight: true }
        }
    }
}
