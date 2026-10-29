// 班级设备面板 · 编辑区 · QtShadcn 规范（2026-10-29 拆分 + 迁移）
//
// ⚠️ B0 铁律（本件继承原文件的约束）：**纯声明式、零逻辑**
//   —— 禁 Math.*、禁三元、禁 JS function、禁 console.log、禁通信。
//   开合态与选中态一律用 `State.when` 表达，不写条件表达式。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · **删 2 处 border**（卡片描边 + 内部分隔线）—— 规范禁边框，改由 surfaceVariant 底分区
// · `Md3.cSurface` → `Theme.color.surface`；`Md3.cOutline` → `Theme.color.textSecondary`
// · `Md3Btn` → `AppButton`（班级改绑按钮，variant 仍由 C++ 的 classOptions 给）
// · 保留 `Flow`（班级数不定，需自然换行）
import QtQuick
import QtQuick.Layouts
import "../../components"             // AppButton
import "../../components/theme"       // Theme 令牌

Item {
    id: panel

    /** 内容高度（父级用它驱动展开动画的 height）。 */
    readonly property alias contentHeight: editCol.implicitHeight

    // B0 铁律禁三元：本件在容器里由 anchors.left/right 定宽，implicitWidth 只是兜底。
    implicitWidth: 0
    implicitHeight: editCol.implicitHeight + Theme.spacing.md + Theme.spacing.sm

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.md
        color: Theme.color.surfaceVariant
        // 无边框（规范铁律）

        ColumnLayout {
            id: editCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacing.md - 4
            spacing: Theme.spacing.sm + 2

            Text {
                Layout.fillWidth: true
                text: "所属班级（点一下即改绑，云端立即生效）"
                color: Theme.color.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            // 班级选项与选中态样式都由 C++ 给（variant），QML 不判条件
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacing.sm
                Repeater {
                    model: classCenter.classOptions
                    delegate: AppButton {
                        required property var modelData
                        text: modelData.name
                        // C++ 给的 variant 是 Md3 语义（filled/tonal/outlined/text），
                        // 这里直传给 AppButton；AppButton 对未知 variant 回落到 primary。
                        variant: modelData.variant
                        size: "sm"
                        onClicked: classCenter.rebindSelectedTo(modelData.code)
                    }
                }
            }

            // 原为 1px 分隔线；规范要求分区只靠底色差 ⇒ 改用间距
            Item { Layout.preferredHeight: Theme.spacing.xs }

            Text {
                Layout.fillWidth: true
                text: "学校：本校（云端目前只有班级维度，没有学校表）"
                color: Theme.color.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "设备名：" + classCenter.deviceName +
                      "（设备名由被控端自报，云端不开改名接口；改名在设备端做）"
                color: Theme.color.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}
