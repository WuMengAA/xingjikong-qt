import QtQuick 2.15

// 多状态聚合列表（清单 #2：管理端独有，被控端补上）
//
// 管理端形态（viewer/qml/components/DynamicIsland.qml）：
//   · 收起态：显示**首项** + 「N 项：…」提示 + 「展开」
//   · 展开态：逐条 `1. 2. 3.` 编号列表，首项反白，超 3 条显示「还有 N 项」
//   · 空列表：不显示
//
// 被控端差异：
//   · 被控端岛默认**点击穿透**（设计文档 §9「学生端灵动岛默认纯展示、点击穿透」）
//     ⇒ 列表默认不接收鼠标，interactive=false 时整体 ignore
//
// 数据：states = [{ k: "voice", t: "老师正在讲话", d: "…", p: 0.4 }, ...]
//   k 类别（决定图标）t 标题 d 描述 p 进度（-1=无进度条, 0~1=进度）
Item {
    id: list

    property var theme
    // ⚠️ 不能叫 states —— QQuickItem 已有同名属性（会 shadow 基类）
    property var items: []
    property bool interactive: false
    property int maxRows: 3               // 展开态最多逐条显示，超出走「还有 N 项」

    readonly property int n: items ? items.length : 0
    readonly property var first: n > 0 ? items[0] : null
    readonly property int rest: Math.max(0, n - 1)
    // ⚠️ 传给上层判空用的：Repeater.count 在同帧还是 0，用它判空会慢一帧
    readonly property int rows: Math.min(n, maxRows)

    implicitHeight: col.height

    // k → lucide 图标名（提到组件根：QML 非限定名只查「自身→组件根」两级，
    //   delegate 里直接写 iconNameFor() 会 ReferenceError —— 项目已踩过的坑）
    function iconNameFor(k) {
        switch (k) {
        case "voice":     return "bell"           // 老师讲话
        case "offline":   return "circle-dashed"  // 断连
        case "command":   return "layout-grid"    // 批量指令
        case "file":      return "download"       // 文件分发
        case "alert":     return "triangle-alert" // 告警 / 离线设备
        case "monitor":   return "monitor"        // 屏幕监控
        case "broadcast": return "monitor"        // 屏幕广播
        case "exam":      return "ban"            // 考试模式
        case "clock":     return "clock"
        default:          return "circle"
        }
    }

    Column {
        id: col
        width: parent.width
        height: childrenRect.height
        spacing: 2

        Repeater {
            model: list.rows

            delegate: Item {
                id: row
                // ⚠️ model 是 int（行数）时 modelData 不存在 ⇒ 一律用 index 从 items 取
                readonly property var it: (list.items && index < list.items.length)
                                          ? list.items[index] : null
                width: col.width
                height: Math.max(iconBox.height,
                                 titleText.implicitHeight + descText.implicitHeight + 4)

                Rectangle {
                    anchors.fill: parent
                    visible: index === 0
                    radius: 6
                    color: list.theme
                           ? (list.theme.light ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(1, 1, 1, 0.07))
                           : "transparent"
                }

                SvgIcon {
                    id: iconBox
                    name: list.iconNameFor(row.it ? row.it.k : "")
                    tint: list.theme ? list.theme.fg : "#FAFAFA"
                    px: 16
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: titleText
                    anchors.left: iconBox.right
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.top: parent.top
                    anchors.topMargin: 3
                    text: (index + 1) + ". " + (row.it && row.it.t ? row.it.t : "")
                    color: list.theme ? list.theme.fg : "#FAFAFA"
                    font.pixelSize: 13
                    font.weight: index === 0 ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }

                Text {
                    id: descText
                    anchors.left: titleText.left
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.top: titleText.bottom
                    anchors.topMargin: 1
                    text: row.it && row.it.d ? row.it.d : ""
                    color: list.theme ? list.theme.sub : "#C8C8C8"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    visible: text !== ""
                }
            }
        }

        // 「还有 N 项」
        Text {
            width: col.width
            visible: list.n > list.maxRows
            leftPadding: 8
            text: "还有 " + (list.n - list.maxRows) + " 项"
            color: list.theme ? list.theme.sub : "#C8C8C8"
            font.pixelSize: 12
        }
    }
}
