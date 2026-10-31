// 星集控 · 空态块（AppEmptyState）—— 2026-10-31
//
// ── 为什么需要它 ────────────────────────────────────────────────────────
// 被控端好几页在"还没数据"时只剩一行灰字（记录页 / 自检页 / 插件市场），
// 现场看到的是"界面没做出来"，而不是"这里本来就没东西"。空态必须自己说明
// **为什么空** + **怎么让它不空**，这是可读性下限，不是装饰。
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · 无边框、无底色：空态本身就是"留白"，再加底色会和所在卡片抢层次
// · 全部居中：标题 14 Medium / 说明 12 textSecondary；有动作才出按钮
// · 间距 Theme.spacing.sm(8) 的倍数；圆角只在按钮上（由 AppButton 自带）
// · 颜色只用 Theme.color.*，本文件无硬编码 Hex
//
// ── 用法 ────────────────────────────────────────────────────────────────
//   AppCard {
//       AppEmptyState {
//           title: "还没有噪音记录"
//           description: "开启「噪音监测」或「自习模式」后，每次采样都会记到这里。"
//           actionText: "打开记录文件夹"
//           onActionClicked: agentUi.openNoiseFolder()
//       }
//   }
// 本件根是 ColumnLayout ⇒ 直接写在 AppCard 里即可（不需要再包一层）。
// 放进普通 Column 时也没问题：Layout 附加属性在那里被忽略，宽度跟随父级。

import QtQuick
import QtQuick.Layouts
import "theme"

ColumnLayout {
    id: root

    /** 主文案（必填）。 */
    property string title: ""
    /** 说明：空的原因 + 让它不空的办法。空串 = 不占位。 */
    property string description: ""
    /** 动作按钮文案。空串 = 不出按钮（只说明、无可操作时用）。 */
    property string actionText: ""
    /**
     * 动作按钮变体。默认 `solid`（反白实底）。
     * ⚠️ 不要用 `secondary`：空态十有八九长在 `variant:"muted"` 的卡片里，
     *    secondary 的底也是 surfaceVariant ⇒ 同色同形，按钮看着就是一行字
     *    （2026-10-31 截图实证，现场会以为"这里没按钮"）。
     */
    property string actionVariant: "solid"

    signal actionClicked()

    spacing: Theme.spacing.sm

    // 竖直方向给一点呼吸：空态上下各 8，避免贴死在卡片边缘
    Layout.fillWidth: true
    Layout.topMargin: Theme.spacing.sm
    Layout.bottomMargin: Theme.spacing.sm

    Text {
        Layout.fillWidth: true
        text: root.title
        font.pixelSize: 14
        font.weight: Font.Medium
        color: Theme.color.textPrimary
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    Text {
        Layout.fillWidth: true
        visible: root.description !== ""
        text: root.description
        font.pixelSize: 12
        color: Theme.color.textSecondary
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        lineHeight: 1.4
    }

    AppButton {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Theme.spacing.sm
        visible: root.actionText !== ""
        text: root.actionText
        variant: root.actionVariant
        size: "sm"
        onClicked: root.actionClicked()
    }
}
