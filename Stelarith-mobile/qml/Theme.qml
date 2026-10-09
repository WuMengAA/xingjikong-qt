// 手机端主题（2026-10-09 · 精简令牌，对齐管理端黑白语言 + 彩色规范）
// 普通 QtObject 组件（相对 import 可用；单例留给模块化后）
import QtQuick

QtObject {
    // 深色（默认，手机夜间护眼）
    readonly property var dark: ({
        win: "#0A0A0A", body: "#080808", panel: "#101010", cream: "#141414",
        card: "#242424", line: "#202020", stroke: "#333333",
        fg: "#FAFAFA", fg3: "#8A8A8A", fg4: "#707070", inv: "#F0F0F0",
        rCard: 12, rCtrl: 8,
        acc: "#4A90D9", ok: "#2E9E5B", warn: "#D4A017", err: "#D64545", info: "#3D7FB8"
    })
    // 浅色（白底黑字，教室投影用）
    readonly property var light: ({
        win: "#F6F6F6", body: "#FFFFFF", panel: "#FFFFFF", cream: "#EDEDED",
        card: "#CCCCCC", line: "#D4D4D4", stroke: "#B4B4B4",
        fg: "#1E1E1E", fg3: "#5A5A5A", fg4: "#707070", inv: "#0A0A0A",
        rCard: 12, rCtrl: 8,
        acc: "#2F6FBF", ok: "#1F7A43", warn: "#A87C0A", err: "#B02323", info: "#2A6394"
    })

    property bool isLight: false
    readonly property var th: isLight ? light : dark
}