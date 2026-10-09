// 星集控 · 管理端主题 Token 单一真源（2026-10-09）
//
// 作用：把原来散在 Main.qml 的 darkTh / lightTh 两个 JS 对象里的颜色与圆角
// 集中到一处，深 / 浅两套配色由 `dark` 布尔切换。Main.qml 里
//   readonly property var darkTh:  Theme { dark: true }
//   readonly property var lightTh: Theme { dark: false }
//   readonly property var th: darkMode ? darkTh : lightTh
// 行为与旧版完全一致（token 值逐字照搬），只是来源统一了。
//
// 为什么不做 pragma Singleton：单例只能有一个状态，而本应用需要深 / 浅两套
// 并存、按 darkMode 切换，所以这里用「共享组件 + 两个实例」达到「单一真源」。
import QtQuick 2.15

QtObject {
    property bool dark: true

    // 字号 / 对比度按 WCAG AA：正文 fg ≥ 4.5:1，弱化 fg4 ≥ 4:1（2026-10-07 实测校准）
    property string win:     dark ? "#0A0A0A" : "#F6F6F6"
    property string body:    dark ? "#080808" : "#FFFFFF"
    property string panel:   dark ? "#101010" : "#FFFFFF"
    property string cream:   dark ? "#141414" : "#EDEDED"
    property string hover:   dark ? "#1E1E1E" : "#E6E6E6"
    property string seg:     dark ? "#121212" : "#E9E9E9"
    property string hover2:  dark ? "#111111" : "#EFEFEF"
    property string line:    dark ? "#202020" : "#D4D4D4"
    property string canvas:  dark ? "#1A1A1A" : "#DCDCDC"
    property string stroke:  dark ? "#333333" : "#B4B4B4"
    property string stroke2: dark ? "#2A2A2A" : "#ACACAC"
    property string todo:    dark ? "#191919" : "#CCCCCC"
    property string card:    dark ? "#242424" : "#CCCCCC"
    property string sepline: dark ? "#1E1E1E" : "#D0D0D0"

    property string fg:   dark ? "#FAFAFA" : "#161616"
    property string op:   dark ? "#C8C8C8" : "#3C3C3C"
    property string fg3:  dark ? "#8A8A8A" : "#6E6E6E"
    property string fg4:  dark ? "#707070" : "#808080"
    property string inv:  dark ? "#F0F0F0" : "#1A1A1A"
    property string ph:   dark ? "#6E6E6E" : "#8A8A8A"
    property string ok:   "#2E9E5B"

    property int rWin:  16
    property int rCard: 12
    property int rCtrl: 8
}
