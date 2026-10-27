// 操作面板 · 条状按钮（锁屏 / 重启 / 关机 / 定时 / 软件）—— 纯声明式，零逻辑
//
// 2026-10-28 B0 重构：本件**不含任何逻辑**。
//   底色/前景/暗边三色由 C++（classCenter 控制器）按 kind 给好，父级显式传入，
//   本件不做 warn/danger/neutral 判断 ⇒ 全文件**零三元**。
//   果冻曲线用 State 表达，点击只 emit clicked()，指令映射在 C++（B1）。
//
// 蓝图对应：锁屏/重启 明黄底白字；关机 亮红底白字（危险键，视觉最重）；
//           定时/软件 中性灰底。
import QtQuick 2.15
import "../../island"               // SvgIcon（tint 着色）

Item {
    id: btn

    // ── 纯输入：配色由 C++ 给，本件不判断 ──────────────────────────
    property string label: "锁屏"
    property string iconName: "lock"
    property color  baseColor: "#FBBF24"
    property color  fgColor: "#FFFFFF"
    property color  shadeColor: "#D97706"
    property real   corner: 16

    signal clicked()

    scale: 1.0
    Behavior on scale { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }

    states: [
        State {
            name: "pressed"
            when: ma.pressed
            PropertyChanges { target: btn; scale: 0.9 }
        },
        State {
            name: "hovered"
            when: ma.containsMouse && !ma.pressed
            PropertyChanges { target: btn; scale: 1.05 }
        }
    ]

    Rectangle {
        anchors.fill: parent
        radius: btn.corner
        color: btn.baseColor
    }
    // 下缘暗边 ≈ 软投影（无 effects 模块时的原生近似：Qt5Compat 未安装）
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 3
        radius: btn.corner
        color: btn.shadeColor
        opacity: 0.85
    }

    SvgIcon {
        id: ico
        name: btn.iconName
        px: 20
        tint: btn.fgColor
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        anchors.left: ico.right
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        text: btn.label
        color: btn.fgColor
        font.pixelSize: 18
        font.bold: true
        font.letterSpacing: 4
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: btn.clicked()
    }
}
