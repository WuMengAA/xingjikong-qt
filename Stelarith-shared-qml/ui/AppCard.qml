// 星集控 · 卡片（AppCard）—— 2026-10-29 QtShadcn 规范
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · 圆角：Theme.radius.lg（14，落在规范 12–16 区间）
// · 背景：Theme.color.surface；**无边框**（不写 border.*）
// · 阴影：极柔和（Theme.shadow.soft = 4% 黑，blur 20，yOffset 4）
// · 结构：内部默认 ColumnLayout，四周 padding Theme.spacing.md（16）
//
// ── 为什么阴影要"双层同形"而不是直接给卡片加特效 ────────────────────────
// MultiEffect 是把**源 + 特效**一起画出来的。若直接把卡片当源，一旦特效不可用
// （软件渲染 / AA_UseSoftwareOpenGL 下 MultiEffect 阴影已知不生效，QtShadcn 源码
//  注释原话："MultiEffect 阴影等软件渲染不支持，否则内容空白"），**卡片本体也会消失**。
// 所以这里：
//   · 卡片本体 = 普通 Rectangle（永远能渲染，绝不依赖特效）
//   · 阴影源  = 另一个**同形同色**的 Rectangle，放在卡片下方、被卡片完全盖住
//     ⇒ 特效正常时只见投影；特效失效时它自己画出来也"长得跟卡片一样"，视觉无破损
// 这就是 graceful degradation：最坏情况只丢阴影，不丢内容。
//
// ── 用法 ────────────────────────────────────────────────────────────────
//   AppCard {
//       Layout.fillWidth: true
//       Label { text: "标题" }
//       AppButton { text: "确定" }
//   }
// 直接写在 AppCard 里的子项会进入内部 ColumnLayout（带 padding md）。
// 需要自定义布局时用 `content`：
//   AppCard { content: RowLayout { ... } }

import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import "theme"

Item {
    id: root

    /** 是否启用微弱阴影（软件渲染环境可关掉，避免无谓开销）。 */
    property bool shadow: true
    /** 内部内容容器（ColumnLayout + padding md）。 */
    property alias content: contentBox
    /** 默认子项全部落到内容容器里。 */
    default property alias contentData: contentBox.data

    implicitWidth: contentBox.implicitWidth + Theme.spacing.md * 2
    implicitHeight: contentBox.implicitHeight + Theme.spacing.md * 2

    // ── 阴影源：同形同色，被上方卡片完全盖住，只露出投影 ────────────────
    Rectangle {
        id: shadowSrc
        anchors.fill: parent
        radius: Theme.radius.lg
        color: Theme.color.surface
        visible: root.shadow
        layer.enabled: root.shadow
    }

    MultiEffect {
        source: shadowSrc
        anchors.fill: shadowSrc
        visible: root.shadow
        shadowEnabled: true
        shadowColor: Theme.shadow.soft
        shadowBlur: 1.0
        shadowVerticalOffset: Theme.shadow.yOffset
        shadowHorizontalOffset: 0
    }

    // ── 卡片本体（普通 Rectangle：不依赖任何特效，永远可见）──────────────
    Rectangle {
        id: cardBg
        anchors.fill: parent
        radius: Theme.radius.lg
        color: Theme.color.surface
    }

    // ── 默认内容容器 ────────────────────────────────────────────────────
    ColumnLayout {
        id: contentBox
        anchors.fill: parent
        anchors.margins: Theme.spacing.md
        spacing: Theme.spacing.md
    }
}
