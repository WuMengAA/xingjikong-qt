// 星集控 · 数字卡（AppStatTile）—— 2026-10-31
//
// ── 为什么需要它 ────────────────────────────────────────────────────────
// 记录页与时段页都要「一排四个数字」，设计稿里已定形：大号数字 + 单位 + 底下一行说明。
// 不抽出来的话，两页会各写一遍同样的 12 行 —— 然后必然漂移成两种字号、两种间距。
// 这是本项目反复出现的形态：同一形状在多页各写一遍，改一次要改好几处、且必然漏一处。
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · 底：Theme.color.surfaceVariant（次级容器，数字卡是"卡里的卡"，后退一层）
// · 圆角：Theme.radius.md（10，比外层卡片 lg=14 小一档，视觉上明确是内层）
// · 数字 21 Bold + 单位 12 + 说明 12 —— 三档字阶，**不许所有字一样大**（AGENTS.md §9.4）
// · 数字对齐靠「同行卡片由 Layout 均分」而不是字体特性（取舍见下）
// · 颜色只用 Theme.color.*，本文件无硬编码 Hex
//
// ── 关于等宽数字 ────────────────────────────────────────────────────────
// CSS 的 `font-variant-numeric: tabular-nums` 在 QML 里**没有可直接书写的等价属性**。
// 要真正启用得走 C++ 字体特性 `QFont::setFeature(QFont::Tag("tnum"), 1)`
// （Qt 6.12 头文件 QtGui/qfont.h:272 有此接口），但那要求样式从 C++ 侧下发，
// 与"组件只认 Theme 令牌"的分层相冲突。本件不改字体特性，改为：
//   · 同一行多个数字卡由 Layout 均分宽度 ⇒ **卡本身对齐**（这是眼睛真正在意的）
//   · 卡内数字保持默认比例数字 —— 单卡内部不上下对齐，故无碍
// 若将来某页需要"竖排数字逐位对齐"，应在该页用 C++ 下发字体，而不是把字体特性塞进本件。
//
// ── 用法 ────────────────────────────────────────────────────────────────
//   RowLayout {
//       spacing: Theme.spacing.sm
//       AppStatTile { Layout.fillWidth: true; value: "5,830"; unit: "条"; label: "采样总数" }
//       AppStatTile { Layout.fillWidth: true; value: "3";     unit: "天"; label: "有噪音数据" }
//   }
// valueUnit 折行：数字与单位之间**不加空格**（中文排版里数字与其量词不空格），
// 单位字号小一档并贴住基线 —— 硬挤成 "5,830条" 同号同重会读成一个大数。

import QtQuick
import QtQuick.Layouts
import "theme"

Rectangle {
    id: root

    /** 主数字（已格式化好的字符串，例如 "5,830" / "91.2"）。格式化在 C++ 做，QML 不拼。 */
    property string value: ""
    /** 单位（条/天/次/dB/分）。空串 = 不带单位。 */
    property string unit: ""
    /** 说明（这一格是什么）。必填，否则一串数字没有意义。 */
    property string label: ""
    /** 是否用警示色（如"最高峰值"用弱化的警告色）。默认主文字色。 */
    property bool emphasized: false

    radius: Theme.radius.md
    color: Theme.color.surfaceVariant
    implicitWidth: Math.max(body.implicitWidth + Theme.spacing.md * 2, 96)
    implicitHeight: body.implicitHeight + Theme.spacing.md * 1.5

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: Theme.spacing.sm
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: root.value
                font.pixelSize: 21
                font.weight: Font.Bold
                color: root.emphasized ? Theme.color.warning : Theme.color.textPrimary
            }

            Text {
                id: unitText
                text: root.unit
                visible: root.unit !== ""
                font.pixelSize: 12
                color: Theme.color.textSecondary
                // 单位贴住数字的基线（不是垂直居中）：12px 贴在 21px 数字的底部，读起来才是一个整体
                Layout.alignment: Qt.AlignBaseline | Qt.AlignLeft
            }

            // 吃掉剩余宽度，把「数字 + 单位」顶到左边
            // （不给数字设 fillWidth：那会让它被压缩甚至省略掉，数字卡里最不能丢的就是数字）
            Item { Layout.fillWidth: true }
        }

        Text {
            Layout.fillWidth: true
            text: root.label
            font.pixelSize: 12
            color: Theme.color.textSecondary
            elide: Text.ElideRight
        }
    }
}
