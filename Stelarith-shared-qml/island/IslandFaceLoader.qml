// IslandFaceLoader.qml —— 按面 id 取对应面组件（2026-11-03 · 行数门禁抽取）
// 轮播分页器与平铺网格共用同一份 id→Component 映射，避免 switch 写两遍漂移。
// components 由宿主注入：{ 7: compToday, 1: compMedia, ... }；未命中的 id 取不到 ⇒ Loader 空。
import QtQuick

Loader {
    id: faceLoader

    property int faceId: -1
    property var components: ({})
    property int scalePercent: 100
    // 设置面(5)内容更密，留白小一些（沿用原分页 delegate 的口径）
    property int sideMargin: faceId === 5 ? 14 : 10

    anchors.margins: sideMargin
    scale: scalePercent ? scalePercent / 100.0 : 1.0
    transformOrigin: Item.Center
    sourceComponent: components[faceId] ?? null
}
