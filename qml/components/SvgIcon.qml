import QtQuick 2.15

// 2026-10-08 lucide(morphicons 同款 24×24 stroke 风格) SVG 图标封装。
// 用法：SvgIcon { name: "x"; tint: th.fg1; width: 16; height: 16 }
// 颜色在请求时按主题染（image://svgicon/<name>?color=<tint>），深浅主题自动适配。
Image {
    id: _ic
    property string name: ""
    property color tint: "#ffffff"

    source: "image://svgicon/" + name + "?color=" + encodeURIComponent(tint)
    fillMode: Image.PreserveAspectFit
    sourceSize.width: width
    sourceSize.height: height
    cache: false
    onStatusChanged: if (status === Image.Error) console.log("[SvgIcon] 加载失败:", name, source)
}
