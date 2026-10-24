// 清新极简 · 卡片壳（fresh 组件族 · 2026-10-24 班级工作台方案 A）
//
// 定位：班级工作台页族 + 外壳左栏的通用卡面。视觉真源在 Md3.qml 的 fr* token
// 组（本件不持色值）：白卡面 + frRCard(18) 大圆角 + 两层偏移微投影。
//
// 与 Md3Card 的分工：Md3Card 是 MD3 baseline 语言（containerLow 面色 + 16 圆角），
// 本件是清新极简语言（纯白 + 18 圆角 + 可感投影）。两族并存，消费方按页面选；
// 谁都不改谁 —— 避免一次性全端换肤（用户点单「先在管理端完成」的落地边界）。
//
// 结构纪律（同 Md3Card）：根 Rectangle 只做定位，卡面与阴影分层画 —— 只有这样
// 投影才压得到卡身下。消费方只许传 width/height/md3/radius，不许覆写 color。
// 内边距 16。投影两层偏移矩形近似，零依赖（不引 QtQuick.Effects，教室弱机可承受）。
import QtQuick

Rectangle {
    id: root

    property var md3: ({})
    /** frRCard 令牌控制圆角（18）；如需覆写可在此扩展为显式属性。 */
    default property alias content: inner.data

    implicitWidth: 200
    implicitHeight: inner.implicitHeight + 32

    radius: md3.frRCard || 18
    color: md3.frCard || "#FFFFFF"
    border.width: 0

    // ── 两层柔和投影（z:0 压在卡面下）──
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: (md3.frShadowY || 6)
        radius: root.radius
        color: md3.frShadow || "#101828"
        opacity: (md3.frShadowA === undefined ? 0.04 : md3.frShadowA)
        z: 0
    }
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 2
        radius: root.radius
        color: md3.frShadow || "#101828"
        opacity: (md3.frShadowA === undefined ? 0.04 : md3.frShadowA) * 0.8
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
