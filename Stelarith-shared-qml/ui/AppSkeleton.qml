// 星集控 · 加载骨架（AppSkeleton）—— 2026-10-31
//
// ── 为什么需要它 ────────────────────────────────────────────────────────
// 拉取详情/插件列表时，旧版是**一片空白**。空白和"已加载但没内容"在视觉上完全一样，
// 用户会以为界面坏了并反复点击。骨架先给出「即将出现的形状」，把这个歧义消掉。
//
// ── ⚠️ 骨架基线必须比所在卡亮一档（这是本项目踩过的实证坑）────────────────
// 设计稿阶段用 #33333D 铺在 #202025 卡上，无头截图里那块**看起来是空的** ——
// 因为基线色≈卡底色，动画跑到留白处整条就"消失"了。同色即消失，本项目已犯三次。
// 故这里不用固定灰，而是**主文字色叠低透明度**：它天然比任何 surface 都亮一档，
// 且令牌换浅色模式时自动跟随（浅色下主文字是深色 ⇒ 叠在浅底上是浅灰，方向仍正确）。
//
// ── 为什么不用 MultiEffect / 着色器 ────────────────────────────────────
// 被控端是讲台机，可能是软件渲染（AA_UseSoftwareOpenGL，见 AGENTS.md §10 与
// AppCard.qml 的同类注释）。着色器类效果在软件渲染下已知不生效 ——
// 骨架屏是"加载中"的唯一视觉线索，它挂了就等于整块空白。
// 这里只用「矩形 + 位移属性动画」，纯 QtQuick，任何后端都能跑。
// 动画关掉（animated: false）时退化成静态灰条，仍然表达"这里将有内容"。
//
// ── 用法 ────────────────────────────────────────────────────────────────
//   AppSkeleton { lines: 4 }                       // 通用 4 行
//   AppSkeleton { lines: 3; lineHeight: 15 }       // 标题型：更高更粗
//
// 放在 AppCard 里时注意：本件根是 ColumnLayout（与 AppEmptyState 同款约定），
// Layout 附加属性在普通 Column 里会被忽略，宽度跟随父级。

import QtQuick
import QtQuick.Layouts
import "theme"

ColumnLayout {
    id: root

    /** 占位行数（0 = 不占位）。 */
    property int lines: 3
    /** 每行高度。默认 13（正文一行的观感）。 */
    property int lineHeight: 13
    /** 行间距。走 8 的倍数刻度。 */
    property int lineSpacing: Theme.spacing.sm
    /**
     * 是否播放扫光动画。
     * 关闭后是静态灰条 —— 省 CPU，也给"尊重减少动效"留了开关。
     */
    property bool animated: true

    spacing: root.lineSpacing
    Layout.fillWidth: true

    /**
     * 每行宽度系数（0..1）。末尾一行固定短一截，模拟段落收尾；
     * 其余按 index 循环取确定值 —— 全一样长像表格，纯随机又会在重绘时跳。
     */
    readonly property var _seq: [1.0, 0.86, 0.94, 0.78, 0.90, 0.82]

    function factorFor(i) {
        if (root.lines > 1 && i === root.lines - 1) return 0.46
        return root._seq[i % root._seq.length]
    }

    Repeater {
        model: root.lines

        delegate: Item {
            id: row
            required property int index

            Layout.fillWidth: true
            Layout.preferredHeight: root.lineHeight

            // 基线色（主文字叠 10%）：保证比任何 surface 亮一档，见文件头实证
            readonly property color _base: Qt.rgba(Theme.color.textPrimary.r,
                                                   Theme.color.textPrimary.g,
                                                   Theme.color.textPrimary.b, 0.10)

            Rectangle {
                id: track
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width * root.factorFor(row.index)
                radius: Math.round(root.lineHeight / 2)
                color: row._base
                clip: true

                // ── 扫光：一条略亮的窄带从左划到右 ──────────────────────
                Rectangle {
                    id: band
                    height: parent.height
                    width: Math.max(24, track.width * 0.35)
                    radius: track.radius
                    color: Qt.rgba(Theme.color.textPrimary.r,
                                   Theme.color.textPrimary.g,
                                   Theme.color.textPrimary.b, 0.14)
                    visible: root.animated

                    // 起始位置由动画接管（from/to 已按 index 错开时长，避免整块同步闪）
                    // ⚠️ 不要同时写 `x:` 绑定 —— 那会和 NumberAnimation 抢同一个属性
                    NumberAnimation on x {
                        from: -band.width
                        to: track.width
                        duration: 1100 + (row.index % 3) * 90
                        loops: Animation.Infinite
                        running: root.animated && root.visible
                        easing.type: Easing.InOutSine
                    }
                }
            }
        }
    }
}
