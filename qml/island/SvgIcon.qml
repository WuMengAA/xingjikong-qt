import QtQuick 2.15

// SVG 着色图标
//
// ⚠️ 依赖 C++ 侧注册名为 "svgicon" 的 QQuickImageProvider（批次 3 由 island.cpp 宿主提供）。
//    批次 1/2 独立渲染时该 provider 不存在，图标会空白 —— 这是预期，不是 bug。
//
// 与 viewer/qml/components/SvgIcon.qml 语义一致（那边已有 C++ provider），
// 被控端这边 provider 尚未接线，故本文件先落地、后接线。
Image {
    id: icon

    property string name: ""
    property color tint: "#FAFAFA"
    property int px: 22

    source: name === "" ? "" : "image://svgicon/" + name + "?color=" + encodeURIComponent(tint)
    sourceSize.width: px
    sourceSize.height: px
    width: px
    height: px
    fillMode: Image.PreserveAspectFit
    cache: false
    smooth: true
    visible: name !== ""
}
