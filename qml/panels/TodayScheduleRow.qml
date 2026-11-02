// 今日课表 · 单节课程行（2026-11-02 · QtShadcn 全量）
//
// ── 为什么单独拆一个文件 ────────────────────────────────────────────────
// AGENTS §10.5：.qml ≤ 200 行。整屏窗口 + 单行排版塞一个文件必超线，
// 而超线就意味着下一个人不敢改。拆成「宿主」+「行」两个件，各自都短。
//
// ── 大屏尺度的取舍（这是本文件最要紧的设计约束）────────────────────────
// 这块屏挂在教室前/后墙，最近的学生离它 2 米、最远的 9 米。按 8 米可读性反推：
//   科目名 ≥ 屏高 3.2%（1080p ≈ 35px）、时间 ≥ 1.6%（≈17px）；
//   再往下就是「前排能看、后排看不见」——那等于没做。
// 所以字号全部由 `scale`（= 屏高 / 720）派生，不做固定 px：
// 同一份 QML 在 720p 投影和 4K 大屏上都保持相同的**视觉视角**。
//
// ── 为什么不用 ShadcnCard 包裹 ──────────────────────────────────────────
// ShadcnCard 带 ring 描边 + MultiEffect 投影，几十个一起堆在 Flickable 里
// 每帧都要重算离屏特效，教室机（常见核显）会掉帧。这里只借它的**语义**
// （底色靠 theme.card / theme.accent 色差分块），不借它的实现。
// 与 AGENTS §9.4「无边框、靠色差分块」一致。
import QtQuick
import QtShadcn

Item {
    id: root

    /** 大屏缩放系数：宿主按 height/720 传下来。ⓘ 不能叫 `scale`：那是 Item 自带
     *  变换属性，撞名会让 qmllint 报 property-override、语义上也真会叠乘变换。 */
    property real dscale: 1.0
    property string slotName: ""
    property string subject: ""
    property string timeText: ""
    property bool isNow: false
    property bool isPast: false
    /** 0..1，仅 isNow 时有效；-1 表示不适用。 */
    property real progress: -1

    QtShadcnTheme { id: theme }

    readonly property int _pad: Math.round(16 * dscale)
    readonly property int _radius: Math.round(12 * dscale)

    implicitHeight: Math.round(88 * dscale)

    // 分块：当前节用 accent 反白块（大屏上必须一眼抓住），已过用极低对比，
    // 未开始用 card 底 —— 三档只靠底色区分，不加任何描边。
    Rectangle {
        id: bg
        anchors.fill: parent
        radius: root._radius
        color: root.isNow ? theme.primary
             : root.isPast ? "transparent"
             : theme.card

        Behavior on color { ColorAnimation { duration: 200 } }

        // ── 左：节次名（小字，上面一行）+ 时间（下面一行）──
        Column {
            id: metaCol
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: root._pad
            spacing: Math.round(2 * root.dscale)
            width: Math.round(150 * root.dscale)

            Text {
                text: root.slotName
                color: root.isNow ? theme.primaryForeground
                     : root.isPast ? theme.mutedForeground
                     : theme.foreground
                font.pixelSize: Math.round(15 * root.dscale)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                width: metaCol.width
            }
            Text {
                text: root.timeText
                color: root.isNow ? theme.primaryForeground
                     : theme.mutedForeground
                font.pixelSize: Math.round(12 * root.dscale)
                opacity: root.isNow ? 0.75 : 1.0
                width: metaCol.width
            }
        }

        // ── 中：科目名（本行真正的主角）──
        Text {
            id: subj
            anchors.left: metaCol.right
            anchors.right: right_block.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: root._pad
            anchors.rightMargin: root._pad
            text: root.subject
            color: root.isNow ? theme.primaryForeground
                 : root.isPast ? theme.mutedForeground
                 : theme.foreground
            font.pixelSize: Math.round(23 * root.dscale)
            font.weight: root.isNow ? Font.Bold : Font.Medium
            elide: Text.ElideRight
            // 已上完的课压低不透明度，而不是换一个灰色值 ——
            // 换色值会让浅色模式下"灰"和"更灰"糊成一片（§9.4 同款教训）。
            opacity: root.isPast ? 0.45 : 1.0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }

        // ── 右：状态标记 + 进行中进度条 ──
        Item {
            id: right_block
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: root._pad
            width: Math.round(160 * root.dscale)
            height: parent.height

            ShadcnBadge {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.isNow
                text: root.progress >= 0 ? Math.round(root.progress * 100) + "%" : ""
                variant: ShadcnBadge.Variant.Secondary
            }

            // 进度条只在当前节出现：其余时候它是噪音。
            // 宽度 140*scale 是「8 米外能看出走了多少」的下限，再窄就只是装饰。
            ShadcnProgress {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root._pad * 0.6
                visible: root.isNow && root.progress >= 0
                width: Math.round(140 * root.dscale)
                height: Math.round(5 * root.dscale)
                value: Math.max(0, Math.min(1, root.progress))
            }
        }
    }
}
