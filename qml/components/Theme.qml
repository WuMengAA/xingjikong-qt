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
    // ── 彩色令牌（2026-10-10 · 用户裁决「可以彩色」+ 管理端 Fluent 铺开）──
    // 规范见 docs/彩色令牌设计规范-2026-10-09.md：只用于状态强调（点/标签/描边），
    // 不做背景铺色。深/浅各一套（WCAG 校准过对比度）。
    property string acc:  dark ? "#4A90D9" : "#2F6FBF"   // 强调/主操作
    property string ok:   dark ? "#2E9E5B" : "#1F7A43"   // 成功/在线
    property string warn: dark ? "#D4A017" : "#A87C0A"   // 警告/待处理
    property string err:  dark ? "#D64545" : "#B02323"   // 错误/离线
    property string info: dark ? "#3D7FB8" : "#2A6394"   // 信息/提示

    property int rWin:  16
    property int rCard: 12
    property int rCtrl: 8
}
