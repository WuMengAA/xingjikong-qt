// 班级工作台 · 成员管理「班级人员」段 · QtShadcn 规范（2026-10-29 拆分 + 迁移）
//
// 职责：只管「人（账号）」的成员管理 UI —— 表头 / 添加行 / 人员清单 / 空态。
// 数据与写操作全由父级（ClassMemberPane）经信号发起，本件不碰 backend。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · 角色选择：原为「只读 TextInput 伪装下拉 + 浮层三档按钮」，现改为**常驻 AppChip 三档**
//   —— 少一层浮层状态（visible/点外收起），且用上规范组件。属有意简化，非纯换皮。
import QtQuick
import QtQuick.Layouts
import "../../components"             // AppCard / AppButton / AppChip / AppInput
import "../../components/theme"       // Theme 令牌
import "../../island"                 // SvgIcon

Item {
    id: sec

    property var memberModel: []
    property string selRole: "teacher"
    property bool loading: false
    /** 输入框当前文本（本件持有，避免父级来回同步）。 */
    property string draftUid: ""

    signal reloadRequested()
    signal addRequested(string uid, string role)
    signal removeRequested(string id)
    signal rolePicked(string role)

    implicitHeight: col.implicitHeight

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Theme.spacing.sm

        // ── 表头：标题 + 刷新 ────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.sm
            SvgIcon {
                name: "users"
                px: 14
                tint: Theme.color.textSecondary
            }
            Text {
                Layout.fillWidth: true
                text: "班级人员（" + sec.memberModel.length + "）"
                font.pixelSize: 13
                font.weight: Font.Medium
                color: Theme.color.textSecondary
                elide: Text.ElideRight
            }
            AppButton {
                text: "刷新"
                variant: "ghost"
                size: "sm"
                enabled: !sec.loading
                onClicked: sec.reloadRequested()
            }
        }

        // ── 添加行：账号 + 角色 + 添加 ───────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.sm

            AppInput {
                Layout.fillWidth: true
                placeholder: "老师账号 / uid"
                text: sec.draftUid
                onTextEdited: sec.draftUid = text
            }
            AppButton {
                text: "添加"
                variant: "secondary"
                enabled: sec.draftUid.trim() !== ""
                onClicked: {
                    sec.addRequested(sec.draftUid.trim(), sec.selRole)
                    sec.draftUid = ""
                }
            }
        }

        // ── 角色三档（常驻，替代原浮层）───────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.sm
            Text {
                text: "角色："
                font.pixelSize: 12
                color: Theme.color.textSecondary
            }
            Repeater {
                model: ["owner", "teacher", "viewer"]
                delegate: AppChip {
                    required property string modelData
                    text: modelData
                    selected: sec.selRole === modelData
                    onClicked: sec.rolePicked(modelData)
                }
            }
            Item { Layout.fillWidth: true }
        }

        // ── 人员清单（可变高，最长留 5 行）────────────────────────────
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(152, Math.max(0, sec.memberModel.length * 26))
            clip: true
            model: sec.memberModel
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            visible: sec.memberModel.length > 0

            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 24
                radius: Theme.radius.sm - 4
                color: Theme.color.surfaceVariant

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing.sm
                    anchors.rightMargin: Theme.spacing.sm
                    spacing: Theme.spacing.sm

                    Text {
                        Layout.fillWidth: true
                        text: (modelData && modelData.id) ? String(modelData.id) : "?"
                        font.pixelSize: 13
                        color: Theme.color.textPrimary
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.preferredWidth: 64
                        text: (modelData && modelData.role) ? String(modelData.role) : ""
                        font.pixelSize: 13
                        font.weight: String(modelData && modelData.role) === "owner" ? Font.Bold : Font.Normal
                        color: String(modelData && modelData.role) === "owner"
                               ? Theme.color.primary
                               : (String(modelData && modelData.role) === "teacher"
                                  ? Theme.color.textSecondary : Theme.color.textSecondary)
                    }
                    AppButton {
                        text: "移出"
                        variant: "ghost"
                        size: "sm"
                        onClicked: {
                            if (modelData && modelData.id !== undefined)
                                sec.removeRequested(String(modelData.id))
                        }
                    }
                }
            }
        }

        // ── 空态 / 加载中 ─────────────────────────────────────────────
        Text {
            Layout.fillWidth: true
            visible: sec.memberModel.length === 0 && !sec.loading
            text: "还没有成员。填上面的账号后点「添加」—— 那人下次登录即可进入本班。"
            font.pixelSize: 13
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: sec.loading
            text: "拉取成员清单中…"
            font.pixelSize: 13
            color: Theme.color.textSecondary
        }
    }
}
