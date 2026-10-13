// 星集控 · Material 3 设计令牌单一真源（2026-10-13 · 用户批准为全项目唯一模板）
//
// 来源：qml/MaterialLab.qml 测试页。用户裁决 2026-10-13：「都可以，被控端和
// 管理端都用这个」——本文件 + qml/components/md3/ 组件库即两端的唯一视觉模板。
// 规范值：Material Design 3 baseline 色板 / 字阶 / 形状 / 状态层 / 动效。
//
// 用法：
//   Md3 { id: md3; dark: root.darkMode }     // 深浅两实例并存、运行期切换
//   color: md3.cSurface   font.pixelSize: md3.titleM   radius: md3.rM
//
// 为什么不是 pragma Singleton：同 Theme.qml —— 单例只能持一份值，而本应用
// 需要深浅两套并存按 darkMode 切，故用「共享组件 + 两个实例」达到单一真源。
//
// ── 迁移桥（legacy 别名，2026-10-13）─────────────────────────────
// 存量界面（桌面 Main.qml 3802 行 + Btn/InputField/Card/ToggleRow/RtcVideoSurface
// 等既有组件）读的是旧 Theme 令牌名（win/panel/fg/acc/rCtrl…）。本文件提供
// **同名别名**映射到 MD3 值：把 `Theme {}` 换成 `Md3 {}` 即整域换肤，无需逐页
// 重写。⚠️ 这是迁移桥不是长期 API——全端组件迁移完成后删除别名块。
import QtQuick 2.15

QtObject {
    id: root
    property bool dark: false

    // ═══ 色彩 · MD3 baseline ═══
    property color cPrimary:      dark ? "#D0BCFF" : "#6750A4"
    property color cOnPrimary:    dark ? "#381E72" : "#FFFFFF"
    property color cPrimaryCont:  dark ? "#4F378B" : "#EADDFF"
    property color cOnPrimaryCont:dark ? "#EADDFF" : "#21005D"
    property color cSecondary:      dark ? "#CCC2DC" : "#625B71"
    property color cSecondaryCont:  dark ? "#4A4458" : "#E8DEF8"
    property color cOnSecondaryCont:dark ? "#E8DEF8" : "#1D192B"
    property color cTertiary:      dark ? "#EFB8C8" : "#7D5260"
    property color cTertiaryCont:  dark ? "#633B48" : "#FFD8E4"
    property color cOnTertiaryCont:dark ? "#FFD8E4" : "#31111D"
    property color cError:         dark ? "#F2B8B5" : "#B3261E"
    property color cOnError:       dark ? "#601410" : "#FFFFFF"
    property color cErrorCont:     dark ? "#8C1D18" : "#F9DEDC"
    property color cOnErrorCont:   dark ? "#F9DEDC" : "#410E0B"
    property color cSurface:       dark ? "#1C1B1F" : "#FFFBFE"
    property color cOnSurface:     dark ? "#E6E1E5" : "#1C1B1F"
    property color cSurfaceVar:    dark ? "#49454F" : "#E7E0EC"
    property color cOnSurfaceVar:  dark ? "#CAC4D0" : "#49454F"
    property color cOutline:       dark ? "#938F99" : "#79747E"
    property color cOutlineVar:    dark ? "#49454F" : "#CAC4D0"
    // surface 容器五级（MD3 elevation 以色阶表达，不用投影）
    property color cContLowest:    dark ? "#0F0D13" : "#FFFFFF"
    property color cContLow:       dark ? "#1D1B20" : "#F7F2FA"
    property color cCont:          dark ? "#211F26" : "#F3EDF7"
    property color cContHigh:      dark ? "#2B2930" : "#ECE6F0"
    property color cContHighest:   dark ? "#36343B" : "#E6E0E9"
    property color cInverseSurf:   dark ? "#E6E1E5" : "#313033"
    property color cInverseOn:     dark ? "#313033" : "#F4EFF4"
    // 状态色（MD3 baseline 未定义语义状态，沿用项目 WCAG 校准值，err 归 MD3 error）
    property color cOk:            dark ? "#4CC38A" : "#1B7A43"
    property color cWarn:          dark ? "#D4A017" : "#A87C0A"
    property color cInfo:          dark ? "#7FB3E0" : "#2A6394"

    // ═══ 形状（MD3 shape scale） ═══
    property int rXS: 4      // 文本框 / Snackbar
    property int rS: 8       // Chip / 小卡片
    property int rM: 12      // 卡片 / FAB / 菜单
    property int rL: 16      // FAB / 大面板
    property int rXL: 28     // 对话框 / 抽屉
    property int rFull: 999  // 按钮 / 开关 / pill（取值大于半高即全圆）

    // ═══ 动效（MD3 motion） ═══
    property int durShort: 200    // 颜色 / 开关 / 指示器
    property int durMedium: 350   // 进入 / 退出
    property int durLong: 500     // 几何形变（大元素）
    // emphasized decelerate（进入）/ standard（通用）
    property var easeEmph: [0.05, 0.7, 0.1, 1.0]
    property var easeStd:  [0.2, 0.0, 0.0, 1.0]
    // 状态层不透明度（MD3 state layers）
    property real stHover: 0.08
    property real stPress: 0.10

    // ═══ 字阶（MD3 type scale，px） ═══
    property int displayL: 57
    property int displayM: 45
    property int displayS: 36
    property int headlineL: 32
    property int headlineM: 28
    property int headlineS: 24
    property int titleL: 22
    property int titleM: 16
    property int titleS: 14
    property int bodyL: 16
    property int bodyM: 14
    property int bodyS: 12
    property int labelL: 14
    property int labelM: 12
    property int labelS: 11

    // ═══ legacy 别名（迁移桥 · 全端迁移完成后删） ═══
    // 旧 Theme 令牌名 → MD3 值。存量组件/页面零改动换肤。
    property color win:    cSurface
    property color body:   cSurface
    property color panel:  cCont
    property color cream:  cContHigh
    property color hover:  cContHighest
    property color seg:    cContHigh
    property color hover2: cContLow
    property color line:   cOutlineVar
    property color canvas: cContHighest
    property color stroke: cOutlineVar
    property color stroke2:cOutline
    property color todo:   cContHigh
    property color card:   cContHighest
    property color sepline:cOutlineVar
    property color fg:     cOnSurface
    property color op:     cOnSurfaceVar
    property color fg3:    cOnSurfaceVar
    property color fg4:    cOutline
    property color inv:    cInverseSurf
    property color ph:     cOutline
    property color acc:    cPrimary
    property color ok:     cOk
    property color warn:   cWarn
    property color err:    cError
    property color info:   cInfo
    property int rWin:  rXL   // 16 → 28（MD3 对话框圆角）
    property int rCard: rM    // 12 → 12（MD3 卡片圆角）
    property int rCtrl: rFull // 8  → 20（MD3 按钮全圆）
}
