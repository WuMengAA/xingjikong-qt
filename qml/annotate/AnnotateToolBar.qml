// 批注层底部工具条（2026-10-27 用户两次点单后成型）
//
// 第一次：「开了之后就关不了了…在底部要显示鼠标、批注、橡皮、白板」
// 第二次：「显示颜色之类的应该在底部，底部导航栏可以在下方隐藏」
//
// 为什么它必须有：批注层是**全屏 + 置顶 + 无焦点**窗口。没有工具条时，老师的退出路径
// 只有低层键盘钩子的 ESC、或屏幕顶部灵动岛上的「结束批注」；任一条失效就被自己的
// 批注层困住（实测原话：「开了之后就关不了了」）。所以「鼠标」是**必达出口**。
//
// 为什么颜色也要在这儿：岛在批注时被压在批注层之下（层级见 annotate_domain.cpp），
// 老师换颜色得先收批注层再去岛上点——动线直接断掉。工具条必须自足。
//
// 可收起：讲课时工具条会挡住课件底部，右侧「▾」收成一条 78×14 的小凸条；
// 鼠标悬停或点一下即展开（触摸屏没有 hover，所以点击也展开）。
import QtQuick

Rectangle {
    id: bar

    // 由 AnnotateRoot 注入（AnnotateCanvas 实例）
    property var canvas: null
    property bool collapsed: false

    readonly property string tool: bar.canvas ? String(bar.canvas.tool)   : ""
    readonly property string board: bar.canvas ? String(bar.canvas.board) : ""
    readonly property real   penW: bar.canvas ? Number(bar.canvas.penWidth) : 4

    // 透明 → 白 → 黑 → 绿 → 透明。老师的心理模型是"一直按，四种底色轮一遍"，
    // 而不是进二级菜单挑颜色（讲台上没人有时间挑）。
    function nextBoard(cur) {
        if (!cur) return "white"
        if (cur === "white") return "black"
        if (cur === "black") return "green"
        return ""                       // 绿板或其他自定义色 → 回透明批注
    }
    // 笔粗三档：细 2 / 中 4.5 / 粗 8。单键循环，按钮上直接写当前档位。
    function nextWidth(w) {
        if (w <= 3) return 4.5
        if (w <= 6) return 8
        return 2
    }
    function widthLabel(w) {
        if (w <= 3) return "细"
        if (w <= 6) return "中"
        return "粗"
    }

    implicitWidth: bar.collapsed ? 78 : (row.implicitWidth + 34)
    implicitHeight: bar.collapsed ? 14 : 52
    radius: bar.collapsed ? 7 : 26
    // ⚠️ 恒深色半透明浮层：批注条**叠在第三方画面/黑板上**，必须与"被批注的内容"保持对比，
    //    不能跟随 app 明暗主题（浅色模式下变白会让白笔批注直接看不见）。
    //    故这里保留专用色（带 alpha），不受「页面禁 Hex」约束；2026-10-30 只删了描边。
    color: bar.collapsed ? "#B3151418" : "#E6151418"

    Behavior on implicitWidth  { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
    Behavior on implicitHeight { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
    Behavior on radius         { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4
        visible: !bar.collapsed

        AnnotateToolButton {
            label: "鼠标"
            onClicked: { if (bar.canvas) bar.canvas.requestStop() }
        }
        AnnotateToolButton {
            label: "批注"
            // 荧光笔也算"批注中"：否则用了荧光笔四个按钮全灭，看着像没在工作
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

        // ⚠️ Row 的子项不能写 anchors（会让 Row 整个失效），
        //    所以矮元素一律包一层 height=36 的 Item，内部再 centerIn。
        Item {
            width: 1
            height: 36
            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: 20
                color: "#33FFFFFF"
            }
        }

        Item {
            width: dotRow.implicitWidth
            height: 36
            AnnotateColorDots {
                id: dotRow
                anchors.centerIn: parent
                canvas: bar.canvas
            }
        }

        Item {
            width: 1
            height: 36
            Rectangle {
                anchors.centerIn: parent
                width: 1
                height: 20
                color: "#33FFFFFF"
            }
        }

        AnnotateToolButton {
            label: bar.widthLabel(bar.penW)
            minWidth: 50
            onClicked: { if (bar.canvas) bar.canvas.setWidth(bar.nextWidth(bar.penW)) }
        }
        AnnotateToolButton {
            label: "撤销"
            onClicked: { if (bar.canvas) bar.canvas.undo() }
        }
        AnnotateToolButton {
            label: "清空"
            onClicked: { if (bar.canvas) bar.canvas.clearAll() }
        }
        // 收起：把整条缩成底部一条小凸条，不挡课件
        AnnotateToolButton {
            label: "▾"
            minWidth: 34
            onClicked: bar.collapsed = true
        }
    }

    // 收起态：整条就是把手本身。悬停展开（鼠标）、点击展开（触摸屏）
    MouseArea {
        anchors.fill: parent
        visible: bar.collapsed
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        onEntered: bar.collapsed = false
        onClicked: bar.collapsed = false
    }
}
