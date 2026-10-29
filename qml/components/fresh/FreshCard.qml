// 清新极简 · 卡片壳（fresh 组件族 · 2026-10-24 班级工作台方案 A）
//
// 定位：班级工作台页族 + 外壳左栏的通用卡面。视觉真源已迁至 Theme 令牌
// （本件不持色值）：白卡面 + radius.lg 大圆角 + 两层偏移微投影。
//
// 与 AppCard 的分工：AppCard 是 QtShadcn 语言（surface 面色 + radius.lg），
// 本件是清新极简语言（纯白 + 可感投影）。两族并存，消费方按页面选；
// 谁都不改谁 —— 避免一次性全端换肤（用户点单「先在管理端完成」的落地边界）。
//
// 结构纪律（同 AppCard）：根 Rectangle 只做定位，卡面与阴影分层画 —— 只有这样
// 投影才压得到卡身下。消费方只许传 width/height/radius，不许覆写 color。
// 内边距 16。投影两层偏移矩形近似，零依赖（不引 QtQuick.Effects，教室弱机可承受）。
import QtQuick
import "../theme"   // Theme 设计令牌单例（pragma Singleton）

Rectangle {
    id: root

    /** Theme.radius.lg 控制圆角；如需覆写可在此扩展为显式属性。 */
    default property alias content: inner.data

    implicitWidth: 200
    implicitHeight: inner.implicitHeight + 32

    radius: Theme.radius.lg
    color: Theme.color.surface
    border.width: 0

    // ── 两层柔和投影（z:0 压在卡面下）──
    // Theme.shadow.soft 已含 4% alpha ⇒ 这里 opacity 直接用 1.0 / 0.8（不再二次乘 alpha）。
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: Theme.shadow.yOffset
        radius: root.radius
        color: Theme.shadow.soft
        opacity: 1.0
        z: 0
    }
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 2
        radius: root.radius
        color: Theme.shadow.soft
        opacity: 0.8
        z: 0
    }

    // ── 卡面内容（16 内边距）──
    // 注意：inner 不声明 implicitHeight（让其回退为自身 height），否则
    // inner.implicitHeight → childrenRect.height → inner.height → root.height
    // → root.implicitHeight → inner.implicitHeight 成环（QML 报 binding loop）。
    // 消费方一律 anchors.fill:parent 给 FreshCard 显式高，root.implicitHeight 不参与布局。
    Item {
        id: inner
        x: 16
        y: 16
        width: root.width - 32
        height: root.height - 32
        z: 1
    }
}
