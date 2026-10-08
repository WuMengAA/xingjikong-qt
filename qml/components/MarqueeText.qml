// 灵动岛滚动文本条 · QML 版（2026-10-09 · 通知体系 §6.1）
//
// 与管理端 DynamicIsland 的展开态配套：标题/正文超长时横向滚动展示，
// 持续直到人为点击（点一下停住，再点继续；或父级在点击时收起岛）。
// 纯显示件：不自带手势，由父级（岛）把"点击"转成 pause/ka收。
//
// 用法（等 Main.qml/DynamicIsland.qml 工作区解禁后嵌）：
//   MarqueeText { width: parent.width; height: 20;
//                 text: longText; light: isl.theme.fg === "#FAFAFA" ? false : true }
import QtQuick

Item {
    id: root

    property string text: ""
    property bool light: false          // true=浅色（深字）
    property bool paused: false         // 人为点击后停住
    property int speed: 100             // px/s

    // 内部控制
    property real xOff: 0
    property bool running: text !== "" && !paused
    clip: true

    function togglePause() { root.paused = !root.paused }

    // 文本退场重新进场（与 C++ 版同语义）
    Text {
        id: label
        x: root.xOff
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: root.light ? "#1E1E1E" : "#F5F5F5"
        font.pixelSize: parent.height > 20 ? 14 : 12
        elide: Text.ElideNone          // 滚动件不截断；父级裁剪
        width: implicitWidth
        // 文本宽 ≥ 容器宽才需要滚
        visible: root.text !== ""
    }

    Timer {
        id: ticker
        interval: 16
        repeat: true
        running: root.running && label.implicitWidth > root.width
        onTriggered: {
            root.xOff -= root.speed * 0.016
            // 右缘滚出左缘 → 重进
            if (root.xOff + label.implicitWidth < 0) root.xOff = root.width
        }
    }

    onTextChanged: { root.xOff = root.width; if (root.text === "") root.xOff = 0 }
    onWidthChanged: { if (root.xOff === 0 && root.text !== "") root.xOff = root.width }
}