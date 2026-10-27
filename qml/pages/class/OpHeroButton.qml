// 操作面板 · 2:1 主按钮（通知 / 监控）—— 纯声明式，零逻辑
//
// 2026-10-28 B0 重构：本件**不含任何逻辑**。
//   · 配色、偏转角度、透明度、渐变焦点，全部由 C++（classCenter 控制器）算好后以属性传入；
//     本件不做 theme 判断 ⇒ 全文件**零三元**。
//   · 装饰件（麦克风 / 长条框）的位置差异（通知在左下+右上、监控镜像）用
//     **State + AnchorChanges** 声明式表达，不写 JS、不算坐标。
//   · 果冻曲线同样用 State 表达 press/hover 两态，不用三元。
//   · 点击只 `emit clicked()`，具体指令映射在 C++（B1）。
//
// ⚠️ 径向渐变：原生 Shape + ShapePath.fillGradient: RadialGradient，
//    圆角用声明式 PathArc 拼（PathRectangle 在 Qt 6.12 Shapes 不存在）。
//    禁止 Canvas 手绘（用户点名）；Qt5Compat.GraphicalEffects 本机未安装，不可 import。
import QtQuick 2.15
import QtQuick.Shapes 1.15
import "../../island"               // SvgIcon（着色用 tint）

Item {
    id: btn

    // ── 纯输入：全部由 C++ / 父级给出，本件不判断 ──────────────────
    property string label: "通知"
    property string iconName: "mic"
    property color  c0: "#6EE7B7"
    property color  c1: "#22C55E"
    property color  c2: "#16A34A"
    property color  textColor: "#14532D"
    property real   focalX: 0.2
    property real   iconRot: 45
    property real   barRot: -27
    property real   iconOpacity: 0.5
    property real   barOpacity: 0.3
    property real   corner: 24
    // 渐变半径：由 C++ 给出（QML 不得出现 Math.*，故不在 QML 里取 max）
    property real   gradRadius: 200
    // true = 监控（镜像方向与角度）；false = 通知
    property bool   mirror: false

    signal clicked()

    // ── 果冻曲线：press 0.9 / hover 1.05，400ms 回弹（声明式两态）──
    // 两态与镜像态合并写在**同一个** states 块里：同一属性写两遍 =
    // "Property value set multiple times"，是 QML **致命**错误（整页不加载）。
    scale: 1.0
    Behavior on scale { NumberAnimation { duration: 400; easing.type: Easing.OutBack } }

    // ── 底：原生径向渐变 ───────────────────────────────────────────
    Shape {
        anchors.fill: parent
        opacity: 0.96
        ShapePath {
            fillColor: "transparent"
            strokeWidth: 0
            fillGradient: RadialGradient {
                centerX: btn.width * btn.focalX
                centerY: btn.height * 0.2
                centerRadius: btn.gradRadius
                focalX: btn.width * btn.focalX
                focalY: btn.height * 0.2
                focalRadius: 0
                GradientStop { position: 0.0; color: btn.c0 }
                GradientStop { position: 0.6; color: btn.c1 }
                GradientStop { position: 1.0; color: btn.c2 }
            }
            // ⚠️ PathRectangle 在本机 Qt 6.12 的 Shapes 模块里**不存在**
            //（qmldir/qmltypes 0 命中，2026-10-28 探针实测 "not a type"）——
            // 圆角矩形用声明式 PathArc 四角拼（3 直线 + 4 弧），零 Canvas。
            startX: btn.corner
            startY: 0
            PathLine { x: btn.width - btn.corner; y: 0 }
            PathArc { x: btn.width; y: btn.corner
                      radiusX: btn.corner; radiusY: btn.corner }
            PathLine { x: btn.width; y: btn.height - btn.corner }
            PathArc { x: btn.width - btn.corner; y: btn.height
                      radiusX: btn.corner; radiusY: btn.corner }
            PathLine { x: btn.corner; y: btn.height }
            PathArc { x: 0; y: btn.height - btn.corner
                      radiusX: btn.corner; radiusY: btn.corner }
            PathLine { x: 0; y: btn.corner }
            PathArc { x: btn.corner; y: 0
                      radiusX: btn.corner; radiusY: btn.corner }
        }
    }

    // 内高光 / 内暗角（线性 Gradient，模拟原型 inset 立体感）
    Rectangle {
        anchors.fill: parent
        radius: btn.corner
        gradient: Gradient {
            GradientStop { position: 0.00; color: Qt.rgba(1, 1, 1, 0.40) }
            GradientStop { position: 0.35; color: Qt.rgba(1, 1, 1, 0.06) }
            GradientStop { position: 1.00; color: Qt.rgba(0, 0, 0, 0.10) }
        }
    }

    // 下缘暗边 ≈ 外投影（无 effects 模块时的原生近似）
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 3
        radius: btn.corner
        color: btn.c2
        opacity: 0.25
    }

    // ── 装饰主图标（通知：左下；监控：右上）────────────────────────
    SvgIcon {
        id: decoIcon
        name: btn.iconName
        px: 48
        tint: "#FFFFFF"
        opacity: btn.iconOpacity
        rotation: btn.iconRot
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
    }

    // ── 16:9 长条方框（通知：右上；监控：左下）─────────────────────
    Rectangle {
        id: decoBar
        width: 64
        height: 36
        radius: 6
        color: "#FFFFFF"
        opacity: btn.barOpacity
        rotation: btn.barRot
        anchors.right: parent.right
        anchors.rightMargin: 32
        anchors.top: parent.top
        anchors.topMargin: 24
    }

    // 镜像态（监控）：把上面两个装饰件换到对角，方向由 C++ 给的 iconRot/barRot 控制
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
        },
        State {
            name: "mirrored"
            when: btn.mirror
            AnchorChanges { target: decoIcon; anchors.left: undefined; anchors.bottom: undefined
                            anchors.right: btn.right; anchors.top: btn.top }
            PropertyChanges { target: decoIcon; anchors.rightMargin: 24; anchors.topMargin: 24 }
            AnchorChanges { target: decoBar; anchors.right: undefined; anchors.top: undefined
                            anchors.left: btn.left; anchors.bottom: btn.bottom }
            PropertyChanges { target: decoBar; anchors.leftMargin: 32; anchors.bottomMargin: 40 }
        }
    ]

    // ── 文案（底部居中）────────────────────────────────────────────
    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        horizontalAlignment: Text.AlignHCenter
        text: btn.label
        color: btn.textColor
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
