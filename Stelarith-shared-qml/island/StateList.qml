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
//     ⇒ 本组件不含 MouseArea，事件自然穿透到下层。
//
// 数据：states = [{ k: "voice", t: "老师正在讲话", d: "…", p: 0.4 }, ...]
//   k 类别（决定图标）t 标题 d 描述 p 进度（-1=无进度条, 0~1=进度）
Item {
    id: list

    property var theme
    // ⚠️ 不能叫 states —— QQuickItem 已有同名属性（会 shadow 基类）
    property var items: []
    property int maxRows: 3               // 展开态最多逐条显示，超出走「还有 N 项」

    readonly property int n: items ? items.length : 0
    readonly property var first: n > 0 ? items[0] : null
    readonly property int rest: Math.max(0, n - 1)
    // ⚠️ 传给上层判空用的：Repeater.count 在同帧还是 0，用它判空会慢一帧
    readonly property int rows: Math.min(n, maxRows)

    implicitHeight: col.height

    // k → lucide 图标名（提到组件根：QML 非限定名只查「自身→组件根」两级，
    //   delegate 里直接写 iconNameFor() 会 ReferenceError —— 项目已踩过的坑）
    //
    // ⚠️ 2026-10-18 T 档：桥现在把 **Task.icon 原样**塞进 k（"download"/"music"/"mic"/…），
    //    不再是旧的那套语义 key（"voice"/"file"/"exam"…）。原白名单只认旧 key ⇒
    //    music/mic/bell 全落到 default 画成空心圆 —— 图标看着"都一样"。
    //    这里新旧通吃：先查新名（直透），再回落旧语义 key，最后才 default。
    function iconNameFor(k) {
        switch (k) {
        // ── T 档：桥直透的 Task.icon（与 C++ 侧 upsertTask 写的图标名对齐）──
        case "download":  return "download"       // 文件传输
        case "music":     return "music"          // 媒体播放
        case "mic":       return "mic"            // 对讲中
        case "bell":      return "bell"           // 通知
        // ⚠️ 资源名是 circle-check（lucide 原名），不是 check-circle ——
        //    2026-10-18 实测 resources/icons/ 里没有 check-circle.svg，
        //    旧白名单里写 check-circle 的分支一直画空白（既有 bug，顺手修）。
        case "check-circle": return "circle-check"
        case "circle-check": return "circle-check"
        case "triangle-alert": return "triangle-alert"
        // ── 旧语义 key（管理端聚合那套，保留勿删）──
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

    // 状态 → 文案（state: 0=running 1=paused 3=failed；2=done 已被桥过滤掉）
    function stateLabelFor(s) {
        switch (s) {
        case 1:  return "已暂停"
        case 3:  return "失败"
        default: return ""
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
                // p 可能是 undefined（旧契约无该字段）⇒ 归一化成 -1（无进度条）
                readonly property real prog: (row.it && typeof row.it.p === "number") ? row.it.p : -1
                readonly property bool hasBar: row.prog >= 0
                width: col.width
                // ⚠️ 行高必须把进度条算进去（原实现只算图标/标题/描述 ⇒ 进度条被裁在行外，
                //    看着像"没画出来" —— 2026-10-18 T 档修）
                height: Math.max(iconBox.height,
                                 titleText.implicitHeight + descText.implicitHeight + 4)
                        + (row.hasBar ? barBox.height + 3 : 0)

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
                    // 2026-10-18 MD3 适配：13（非标）→ bodyMedium 14
                    // 状态行的主信息按 MD3 bodyMedium 档；同时把行高预算一起抬（见上方 height）
                    font.pixelSize: list.theme ? list.theme.fsBadge : 14
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
                    // 2026-10-18 MD3 适配：12 与 bodySmall 同值，改走令牌便于统一调
                    font.pixelSize: list.theme ? list.theme.fsNote : 12
                    elide: Text.ElideRight
                    visible: text !== ""
                }

                // ── T 档：进度条（p ∈ [0,1] 才画；-1 = 无进度条）──────────
                // 与 ftRow 的进度槽同款视觉（8px 高 / 4 圆角 / accent 色），
                // 保证"岛内所有进度条长得一样"（UI 布局统一）。
                Item {
                    id: barBox
                    anchors.left: titleText.left
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.top: descText.visible ? descText.bottom : titleText.bottom
                    anchors.topMargin: 3
                    height: 8
                    visible: row.hasBar

                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: list.theme
                               ? (list.theme.light ? Qt.rgba(0, 0, 0, 0.10)
                                                   : Qt.rgba(1, 1, 1, 0.14))
                               : Qt.rgba(1, 1, 1, 0.14)
                        clip: true

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: Math.max(0, parent.width * Math.min(1, row.prog))
                            radius: 4
                            color: list.theme ? list.theme.accent : "#5B8CFF"
                            // 2026-10-18：进度条宽度过渡用 durXShort（原硬编码 120）
                            Behavior on width { NumberAnimation { duration: list.theme ? list.theme.durXShort : 120 } }
                        }
                    }
                }

                // ── T 档：状态标（暂停/失败；running 不画，免得每行都挂个字）──
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.top: titleText.top
                    text: list.stateLabelFor(row.it ? row.it.state : 0)
                    visible: text !== ""
                    // 失败态复用主题已有的严重度色（severityUrgent = MD3 error 色系），
                    // 不新造 theme.err —— 那个属性不存在（2026-10-18 查证），
                    // 取 undefined 会静默画成黑色，且在浅色底上看不清。
                    color: (row.it && row.it.state === 3)
                           ? (list.theme ? list.theme.severityUrgent
                                         : Qt.rgba(179/255, 38/255, 30/255, 1))
                           : (list.theme ? list.theme.sub : "#C8C8C8")
                    font.pixelSize: 11
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
