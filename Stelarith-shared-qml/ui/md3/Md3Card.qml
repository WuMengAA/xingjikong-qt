// MD3 卡片：elevated（containerLow + 极微弥散阴影）/ filled（containerHigh 平面）/
// outlined（surface + 描边，仅既有兼容场景用）。
//
// 2026-10-22 按《全局 UI 构建红线》（AGENTS.md §9）升级：
//   ① 圆角 12 → **16（shape.lg）**：规范「所有卡片圆角必须是 16」，全端统一；
//   ② elevated 变体加**极微弥散阴影**（两层低透明度偏移矩形近似，零依赖、不引
//      QtQuick.Effects —— 与 shell/DeviceCard 同一手法，教室弱机/软渲染可承受）；
//   ③ 无边框原则：默认与 elevated 均无描边，区块隔离靠色阶 + 圆角 + 微投影。
// ⚠️ 默认变体保持 "filled"（既有约 15 处消费方零视觉差，仅圆角变 16）；
//    新代码按规范用 "elevated"（16 圆角 + 微阴影 + containerLow）。
// ⚠️ 结构说明：根 Rectangle 透明、卡片面由 cardFace 画 —— 只有这样阴影（兄弟节点
//    z:0）才压得到卡片（z:1）身下。消费方只许传 width/height/variant/md3，
//    不许覆写 color/radius（既有消费方已核实无一覆写）。
// 内边距 16。用法：Md3Card { md3: …; variant: "elevated" } { 子项 }
import QtQuick

Rectangle {
    id: root
    property var md3: ({})
    property string variant: "filled"   // elevated | filled | outlined
    default property alias content: inner.data

    implicitWidth: 200
    implicitHeight: inner.implicitHeight + 32
    radius: md3.rL
    color: "transparent"                // 根透明：卡片面在 cardFace，阴影才压得下去
    border.width: 0

    // ── 极微弥散阴影（仅 elevated）：两层偏移矩形，z 压在 cardFace 身下 ──
    Rectangle {
        visible: root.variant === "elevated"
        anchors.fill: parent
        anchors.topMargin: 6
        radius: root.radius
        color: Qt.rgba(0, 0, 0, 0.05)
        z: 0
    }
    Rectangle {
        visible: root.variant === "elevated"
        anchors.fill: parent
        anchors.topMargin: 2
        radius: root.radius
        color: Qt.rgba(0, 0, 0, 0.04)
        z: 0
    }

    // ── 卡片面 ──
    Rectangle {
        id: cardFace
        anchors.fill: parent
        radius: root.radius
        z: 1
        color: root.variant === "filled" ? md3.cContHigh
                                         : (root.variant === "outlined" ? md3.cSurface : md3.cContLow)
        border.width: root.variant === "outlined" ? 1 : 0
        border.color: md3.cOutlineVar

        Item {
            id: inner
            anchors.fill: parent
            anchors.margins: 16
            implicitHeight: childrenRect.height
        }
    }
}
