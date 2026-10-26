// 批注层 QML 容器（H 批 · 2026-10-27）
//
// 承载方式：C++ 用 **QQuickView**（QWindow，不是 QWidget）加载本文件，
// 全屏透明置顶 + WS_EX_TOPMOST 兜底（见 annotate_domain.cpp）。
//
// 分工：
//   · 墨迹与工具状态全在 C++ 的 AnnotateCanvas（QQuickPaintedItem）里，
//     本文件只做**输入采集**与**键盘逃生口**，不存任何绘制状态。
//   · 所以这里没有 Canvas/Shape 之类的 QML 绘制 —— 墨迹一多 QML Canvas 会掉帧。
import QtQuick
import Stelarith.Annotate 1.0

Item {
    id: root
    focus: true

    // 墨迹层（C++ 渲染，铺满）
    AnnotateCanvas {
        id: canvas
        objectName: "annotateCanvas"     // C++ 侧 findChild 靠它
        anchors.fill: parent
    }

    // 输入层
    // ⚠️ Qt 6 的 MouseArea 信号是 function(mouse) 形式；老写法 onPressed: {} 里读 mouse
    //    会刷 "Parameter is not declared"（P0 那轮踩过）。
    // ⚠️ 一体机触摸屏的触点会被合成成鼠标左键事件，所以 H1 用 MouseArea 就够；
    //    真正的多点手势（双指缩放/旋转画布）留给 H3，那时换 MultiPointTouchArea。
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: false

        onPressed: function (mouse) {
            // 右键 = 撤销一笔（手写板上的快速纠错，比伸到工具栏快）
            if (mouse.button === Qt.RightButton) {
                canvas.undo()
                return
            }
            canvas.beginStroke(mouse.x, mouse.y)
        }
        onPositionChanged: function (mouse) {
            if (pressed)
                canvas.addPoint(mouse.x, mouse.y)
        }
        onReleased: canvas.endStroke()
    }

    // 键盘逃生口 —— 批注层全屏吃输入，没有本地出口的话老师会被困住
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape) {
            canvas.requestStop()
            event.accepted = true
        } else if (event.key === Qt.Key_Z && (event.modifiers & Qt.ControlModifier)) {
            if (event.modifiers & Qt.ShiftModifier)
                canvas.redo()
            else
                canvas.undo()
            event.accepted = true
        } else if (event.key === Qt.Key_E && (event.modifiers & Qt.ControlModifier)) {
            canvas.clearAll()
            event.accepted = true
        }
    }

    Component.onCompleted: root.forceActiveFocus()
}
