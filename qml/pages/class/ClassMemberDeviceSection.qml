// 班级工作台 · 成员管理「设备」段 · QtShadcn 规范（2026-10-29 拆分 + 迁移）
//
// 职责：只管**设备的去留**（换到别班 / 移出本班）。设备不算"人"，与人员段分开。
// 数据与写操作全由父级经信号发起，本件不碰 backend。
//
// ── 关键坑（原注释保留）────────────────────────────────────────────────
// · delegate 里必须 `model.<role>`，**不许 `modelData.xxx`**（#475 真数据才炸的坑）；
// · 展开态 `rowExpanded` 必须声明在 delegate 根作用域 —— 放进子组件里，
//   Flow 那边引用不到（QML 作用域不向内穿透子组件实例）。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · 保留 `Flow` 做目标班按钮的自然换行（GridLayout 会变等宽格）
import QtQuick
import QtQuick.Layouts
import "../../components"             // AppButton
import "../../components/theme"       // Theme 令牌

Item {
    id: sec

    property var deviceModel: null
    /** 可换到的目标班（父级已排除本班）。 */
    property var targets: []

    signal bindRequested(string code, string uid)
    signal unbindRequested(string uid)

    implicitHeight: col.implicitHeight

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Theme.spacing.sm

        Text {
            Layout.fillWidth: true
            text: "设备：点行内「换到…」选目标班，或「移出」让它暂时不属于任何班。"
            font.pixelSize: 13
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 260          // 父级可覆写；此处给稳妥默认
            clip: true
            model: sec.deviceModel
            spacing: Theme.spacing.sm - 2
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                required property var model
                // 展开态必须在 delegate 根作用域（见文件头）
                property bool rowExpanded: false
                width: ListView.view.width
                height: rowCol.implicitHeight + Theme.spacing.md
                radius: Theme.radius.sm
                color: Theme.color.surfaceVariant

                ColumnLayout {
                    id: rowCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacing.sm
                    spacing: Theme.spacing.xs + 2

                    // 设备名 + 在线点 + 版本
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.sm
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: (model.online === true) ? Theme.color.success
                                                           : Theme.color.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: (model.name && model.name !== "") ? model.name : model.uid
                            font.pixelSize: 13
                            color: Theme.color.textPrimary
                            elide: Text.ElideRight
                        }
                        Text {
                            text: model.version || ""
                            font.pixelSize: 12
                            color: Theme.color.textSecondary
                        }
                    }

                    // 动作行
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.sm
                        AppButton {
                            text: "换到…"
                            variant: "ghost"
                            size: "sm"
                            onClicked: rowExpanded = !rowExpanded
                        }
                        AppButton {
                            text: "移出本班"
                            variant: "ghost"
                            size: "sm"
                            onClicked: sec.unbindRequested(model.uid)
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // 目标班按钮横排（点即换班，班数通常个位数，无需弹窗）
                    Flow {
                        Layout.fillWidth: true
                        Layout.preferredHeight: rowExpanded ? implicitHeight : 0
                        visible: rowExpanded
                        spacing: Theme.spacing.sm

                        Repeater {
                            model: sec.targets
                            delegate: AppButton {
                                required property var modelData
                                text: modelData.name || modelData.code
                                variant: "secondary"
                                size: "sm"
                                onClicked: sec.bindRequested(modelData.code, model.uid)
                            }
                        }
                        Text {
                            visible: sec.targets.length === 0
                            text: "没有别的班可换（先去右栏「新建班级」）"
                            font.pixelSize: 13
                            color: Theme.color.textSecondary
                        }
                    }
                }
            }
        }
    }
}
