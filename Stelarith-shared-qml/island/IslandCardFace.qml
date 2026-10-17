import QtQuick 2.15
import "."

// 灵动岛 · 居中卡片面（Centered，2026-10-17）
//
// ── 为什么需要它（这不是"再加一种样式"，是在补一个**缺失**）──────
//   `IslandOverlay::paintEvent()` 在 QML 就绪时第一句就 `return` —— 也就是
//   **所有形态都由 QML 画**。但 IslandRoot 里此前只有 capsule / banner / mini
//   三套版式，`form === "centered"` 落进了胶囊的 else 分支：
//     窗口 520×200 却套 `radius = height/2 = 100` 的圆角 + 胶囊排版
//     ⇒ 观感是"一张被拉圆的黑卡，按钮从左边被裁掉"。
//   用户说的"很多通知样式不符合审美"，根因就是这个（不是配色主观问题）。
//
// ── 与 C++ 手绘版的关系 ──────────────────────────────────────
//   `paintBigForm()` 现在**只作兜底**（QML 加载失败时才有画面）。
//   本面按 QML/主题体系重排，不逐像素复刻手绘版：
//     · 字号走 th.fs* 令牌（体系 12/13/14/15/18/24/36…），手绘版用的是裸 QFont；
//     · 圆角由 IslandSurface 画 G2（曲率连续），手绘版是 addRoundedRect 的圆弧；
//     · 内容整体垂直居中（Column.centerIn），手绘版是贴顶累加 cursorY ——
//       按钮行一出现就更容易溢出（520×250 里尤其明显）。
Item {
    id: cardFace

    // ── 接口（由 IslandRoot 显式传入；隐式作用域不会跟着搬进独立文件）──
    property var    th: null
    property string icon: ""
    property string title: ""
    property string desc: ""
    property string severity: ""
    property var    actions: []
    property var    bridgeRef: null

    readonly property color tintColor: {
        if (cardFace.severity === "urgent") return cardFace.th ? cardFace.th.severityUrgent : "#B3261E"
        if (cardFace.severity === "inform") return cardFace.th ? cardFace.th.severityInform : "#C88C0A"
        if (cardFace.severity === "remind") return cardFace.th ? cardFace.th.severityRemind : "#148C3C"
        return cardFace.th ? cardFace.th.accent : "#D0BCFF"
    }

    Column {
        id: col
        anchors.centerIn: parent
        // 内容宽：卡片 520 时是 420，两侧各留 50；窄屏（<300）时退到 width-48
        width: Math.min(parent.width - 48, 420)
        spacing: 0

        SvgIcon {
            name: cardFace.icon
            tint: cardFace.tintColor
            px: 34
            visible: cardFace.icon !== ""
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Item { // 图标 → 标题 的间距
            width: 1
            height: cardFace.icon !== "" ? 16 : 0
        }

        Text {
            width: parent.width
            visible: cardFace.title !== ""
            text: cardFace.title
            color: cardFace.th ? cardFace.th.fg : "#FAFAFA"
            font.pixelSize: cardFace.th ? cardFace.th.fsTitleMd : 24
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            lineHeight: 0.95
        }

        Item {
            width: 1
            height: (cardFace.title !== "" && cardFace.desc !== "") ? 10 : 0
        }

        Text {
            width: parent.width
            visible: cardFace.desc !== ""
            text: cardFace.desc
            color: cardFace.th ? cardFace.th.sub : "#C8C8C8"
            font.pixelSize: cardFace.th ? cardFace.th.fsBadge : 14
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }

        Item {
            width: 1
            height: cardFace.actions.length > 0 ? 20 : 0
        }

        Row {
            id: btnRow
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            visible: cardFace.actions.length > 0

            Repeater {
                model: cardFace.actions
                delegate: ActionButton {
                    theme: cardFace.th
                    label: modelData && modelData.label ? modelData.label : String(modelData)
                    role: modelData && modelData.role ? modelData.role
                                                      : (index === 0 ? "primary" : "secondary")
                    onClicked: {
                        if (cardFace.bridgeRef && cardFace.bridgeRef.fireAction)
                            cardFace.bridgeRef.fireAction(label)
                    }
                }
            }
        }
    }
}
