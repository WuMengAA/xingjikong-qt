// 批注层 QML 容器（H 批 · 2026-10-27）
//
// 承载方式：C++ 用 **QQuickView**（QWindow，不是 QWidget）加载本文件，
// 全屏透明置顶 + WS_EX_TOPMOST 兜底（见 annotate_domain.cpp）。
//
// 分工：
//   · 墨迹与工具状态全在 C++ 的 AnnotateCanvas（QQuickPaintedItem）里，
//     本文件只做**输入采集 + 摆一个底部工具条**，不存任何绘制状态。
//   · 所以这里没有 Canvas/Shape 之类的 QML 绘制 —— 墨迹一多 QML Canvas 会掉帧。
//
// ⚠️ **本层不接受焦点**（无焦点触控，2026-10-27 用户点单）：
//    窗口是 WS_EX_NOACTIVATE，落笔不激活、不抢 PPT 的键盘焦点（否则翻页笔当场失效）。
//    因此这里**不能**写 focus:true / forceActiveFocus() / Keys.onPressed ——
//    窗口永不成为活动窗口，QML 一辈子收不到按键事件，写了只会误导后来人。
//    键盘逃生口（ESC 收层 / Ctrl+Z 撤销 / Ctrl+Y 重做 / Ctrl+E 清空）
//    在 C++ 侧用低层键盘钩子实现，见 annotate_domain.cpp 的 annotateKeyboardProc。
//
// ⚠️ **底部工具条是必达出口**（2026-10-27 用户实测后加）：
//    用户原话「开了之后就关不了了」。当时唯一的出口是 ESC 钩子或顶部灵动岛，
//    任一条失效就把老师困在全屏批注层里。所以工具条第一项就是「鼠标」= 退出。
import QtQuick
import Stelarith.Annotate 1.0

Item {
    id: root

    // 墨迹层（C++ 渲染，铺满）
    AnnotateCanvas {
        id: inkCanvas
        objectName: "annotateCanvas"     // C++ 侧 findChild 靠它
        anchors.fill: parent
    }

    // 输入层
    // ⚠️ Qt 6 的 MouseArea 信号是 function(mouse) 形式；老写法 onPressed: {} 里读 mouse
    //    会刷 "Parameter is not declared"（P0 那轮踩过）。
    // ⚠️ 一体机触摸屏的触点会被 Qt 合成成鼠标左键事件，所以 H1 用 MouseArea 就够；
    //    真正的多点手势（双指缩放/旋转画布、两笔同写）留给 H3，那时换 MultiPointTouchArea。
    // ⚠️ 无焦点窗口照样收鼠标：鼠标消息按"光标下窗口"投递，与激活状态无关；
    //    拖动期间 Qt 会 SetCapture，光标出界也不丢点。
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: false

        onPressed: function (mouse) {
            // 右键 = 撤销一笔（手写板上的快速纠错，比伸到工具栏快）
            if (mouse.button === Qt.RightButton) {
                inkCanvas.undo()
                return
            }
            inkCanvas.beginStroke(mouse.x, mouse.y)
        }
        onPositionChanged: function (mouse) {
            if (pressed)
                inkCanvas.addPoint(mouse.x, mouse.y)
        }
        onReleased: inkCanvas.endStroke()
    }

    // 底部工具条：鼠标 / 批注 / 橡皮 / 白板 + 撤销 / 清空
    // 声明在输入层**之后** ⇒ 叠在它上面 ⇒ 点在按钮上不会落笔
    // （QML 按声明顺序堆叠，命中测试从最上层往下找，工具条先吃到事件）。
    AnnotateToolBar {
        id: toolbar
        objectName: "annotateToolBar"    // 探针断言"工具条真的被实例化"靠它
        canvas: inkCanvas
        anchors.horizontalCenter: parent.horizontalCenter
        // 56 = 给任务栏留的余量（任务栏 40~48，多留一点免得太贴边）
        y: root.height - height - 56
    }
}
