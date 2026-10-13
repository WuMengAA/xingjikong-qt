// 星集控 · 通用按钮（2026-10-14 · MD3 唯一模板迁移批）
//
// 全界面只有这一份按钮长相。本次把内部规格升到 Material Design 3：
//   高 40、全圆 20、字 labelL(14) Medium、状态层 hover 8% / press 10%；
//   strong（主操作）= MD3 filled（primary 底 + onPrimary 字）；
//   默认 = MD3 outlined（outline 描边 + primary 字）；
//   disabled = 整体 38% 不透明度（MD3 规范）。
// **调用方 API 一字未改**（theme/text/strong/disabled/tip/pointToUse/clicked），
// 桌面端 Main.qml 与全部弹窗零改动换 MD3 形态——这就是"逐页迁组件"的落地方式：
// 共享组件换规格，全端同时到位，页面级差异再逐页收拾。
//
// ⚠️ 别给这个组件写死 width：控制页右栏要它等到容器宽度（wide=true 独占整行），
//    而 Loader/组件里 width 写死会让"跟着容器变"失效。调用方用 Layout/anchors 定尺寸。
import QtQuick
import QtQuick.Controls

Rectangle {
    id: btn
    property var theme: ({ })
    property alias text: label.text
    property bool strong: false      // 主操作 → MD3 filled
    property bool disabled: false    // 外部强塞的禁用（用不了/没权限），比 role 门控更早生效
    property string tip: ""          // 悬停说明：置灰时尤其要给，否则灰按钮没人知道为什么灰
    property bool pointToUse: true   // 能点就给手型；不可用时给禁止符（不然鼠标还是箭头）

    signal clicked()

    // MD3 规格：高 40、全圆 20（令牌 rFull；老 theme 没有该键时退回 20）
    height: 40
    readonly property real md3Radius: btn.theme && btn.theme.rFull !== undefined ? btn.theme.rFull : 20
    radius: btn.md3Radius

    // MD3 filled：primary 底；outlined：透明底 + outline 描边
    color: btn.strong ? (btn.theme.acc || "#6750A4") : "transparent"
    border.width: btn.strong ? 0 : 1
    border.color: btn.disabled ? (btn.theme.line || "#202020") : (btn.theme.stroke2 || "#79747E")
    opacity: btn.disabled ? 0.38 : 1
    Behavior on opacity { NumberAnimation { duration: 120 } }

    // MD3 状态层：当前景色的 8% / 10%
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: btn.strong ? (btn.theme.win || "#FFFFFF") : (btn.theme.acc || "#6750A4")
        opacity: ma.pressed ? 0.10 : (ma.containsMouse ? 0.08 : 0)
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: btn.text
        // filled 态字用 onPrimary（inv 的 legacy 别名就是 inverseSurface 上的 on 色）；
        // outlined 态字用 primary（acc 别名）。
        color: btn.strong ? (btn.theme.win || "#FFFFFF") : (btn.theme.acc || "#6750A4")
        font.pixelSize: btn.theme && btn.theme.labelL !== undefined ? btn.theme.labelL : 14
        font.weight: Font.Medium
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: !btn.disabled
        hoverEnabled: true
        cursorShape: !btn.enabled ? (btn.pointToUse ? Qt.ForbiddenCursor : Qt.ArrowCursor)
                                  : Qt.PointingHandCursor
        onClicked: if (!btn.disabled) btn.clicked()
    }

    ToolTip {
        visible: ma.containsMouse && btn.tip !== ""
        delay: 450
        text: btn.tip
    }
}
