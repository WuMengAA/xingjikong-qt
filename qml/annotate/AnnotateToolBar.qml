// 批注层底部工具条（2026-10-27 用户实测点单：「在底部要显示鼠标、批注、橡皮、白板」）
//
// 为什么它必须有：批注层是**全屏 + 置顶 + 无焦点**窗口。没有工具条时，老师的退出路径
// 只有两条 —— 低层键盘钩子的 ESC，或屏幕顶部灵动岛上的「结束批注」。
// 任一条失效，老师就被自己的批注层困住（用户实测原话：「开了之后就关不了了」）。
// 所以「鼠标」这一项是**必达出口**，不是装饰。
//
// 顺序按教学动线排：鼠标（退出）→ 批注（笔）→ 橡皮 → 白板；
// 右半（撤销/清空）用细线隔开，避免和主操作抢眼。
//
// ⚠️ 状态一律读 canvas 的属性（tool / board），不在这里存副本 ——
//    管理端指令、岛上按钮、键盘钩子都会改状态，本地存一份必然对不上。
import QtQuick

Rectangle {
    id: bar

    // 由 AnnotateRoot 注入（AnnotateCanvas 实例）
    property var canvas: null

    readonly property string tool:  bar.canvas ? String(bar.canvas.tool)  : ""
    readonly property string board: bar.canvas ? String(bar.canvas.board) : ""

    // 透明 → 白 → 黑 → 绿 → 透明。老师的心理模型是"一直按，四种底色轮一遍"，
    // 而不是进二级菜单挑颜色（讲台上没人有时间挑）。
    function nextBoard(cur) {
        if (!cur) return "white"
        if (cur === "white") return "black"
        if (cur === "black") return "green"
        return ""                       // 绿板或其他自定义色 → 回透明批注
    }

    implicitWidth: row.implicitWidth + 22
    height: 52
    radius: 26
    color: "#E6151418"
    border.width: 1
    border.color: "#33FFFFFF"

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4

        AnnotateToolButton {
            label: "鼠标"
            onClicked: { if (bar.canvas) bar.canvas.requestStop() }
        }
        AnnotateToolButton {
            label: "批注"
            // 荧光笔也算"批注中"：否则用了荧光笔，工具条上四个按钮全灭，看着像没在工作
            active: bar.tool === "pen" || bar.tool === "highlighter"
            onClicked: { if (bar.canvas) bar.canvas.setTool("pen") }
        }
        AnnotateToolButton {
            label: "橡皮"
            active: bar.tool === "eraser"
            onClicked: { if (bar.canvas) bar.canvas.setTool("eraser") }
        }
        AnnotateToolButton {
            label: "白板"
            active: bar.board !== ""
            onClicked: { if (bar.canvas) bar.canvas.requestBoard(bar.nextBoard(bar.board)) }
        }

        Rectangle {
            width: 1
            height: 20
            anchors.verticalCenter: parent.verticalCenter
            color: "#33FFFFFF"
        }

        AnnotateToolButton {
            label: "撤销"
            onClicked: { if (bar.canvas) bar.canvas.undo() }
        }
        AnnotateToolButton {
            label: "清空"
            onClicked: { if (bar.canvas) bar.canvas.clearAll() }
        }
    }
}
