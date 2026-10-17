import QtQuick 2.15
import "."

// 灵动岛 · 全屏公告面（Fullscreen，2026-10-17）
//
// 与 IslandCardFace 同因：QML 里此前没有 fullscreen 版式，铺满屏的窗口套的是
// 胶囊排版（`height/2` 圆角 + 320 宽的内容列），在大屏上就是"一片黑，字挤在顶部"。
//
// 版式取舍（公告板语义，不是普通弹窗）：
//   · 字号放大到 Display 档（标题 36 / 描述 18）—— 教室最后一排要看得清；
//   · 内容垂直居中、整块最宽 900 —— 不让一句短话横跨 1800px 的屏；
//   · 底色由 C++ 按 severity 给（urgent 红 / inform 琥珀 / remind 绿），本面只画字，
//     不自己算配色 —— 配色是"双绘 parity"的一部分，改一处必须改两处。
Item {
    id: fullFace

    property var    th: null
    property string icon: ""
    property string title: ""
    property string desc: ""
    property string severity: ""
    property var    actions: []
    property var    bridgeRef: null
    // 2026-10-17「通知写上时间」：发生时刻。全屏是公告板，时间放**右上角**、
    // 字号跟描述同级 —— 最后一排看不清的字等于没写，不做 10px 小字。
    property string time: ""

    // 全屏是强提示：文字用**纯白/纯黑**而不是主题 fg（红/绿底上主题色会发灰）
    readonly property color onColor: fullFace.th && fullFace.th.light ? "#0A0A0A" : "#FFFFFF"
    readonly property color onSub:   fullFace.th && fullFace.th.light ? "#3A3A3A" : Qt.rgba(1, 1, 1, 0.86)

    Column {
        id: col
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.72, 900)
        spacing: 0

        SvgIcon {
            name: fullFace.icon
            tint: fullFace.onColor
            px: 56
            visible: fullFace.icon !== ""
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Item {
            width: 1
            height: fullFace.icon !== "" ? 28 : 0
        }

        Text {
            width: parent.width
            visible: fullFace.title !== ""
            text: fullFace.title
            color: fullFace.onColor
            font.pixelSize: fullFace.th ? fullFace.th.fsTitleLg : 36
            font.weight: Font.Bold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            lineHeight: 0.95
        }

        Item {
            width: 1
            height: (fullFace.title !== "" && fullFace.desc !== "") ? 26 : 0
        }

        Text {
            width: parent.width
            visible: fullFace.desc !== ""
            text: fullFace.desc
            color: fullFace.onSub
            font.pixelSize: fullFace.th ? fullFace.th.fsTitleSm : 18
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 4
            elide: Text.ElideRight
        }

        Item {
            width: 1
            height: fullFace.actions.length > 0 ? 36 : 0
        }

        Row {
            id: btnRow
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            visible: fullFace.actions.length > 0
            // 全屏下按钮要够大（远距离可点）；scale 不改 implicitWidth，
            // 所以这里必须配 transformOrigin: Center，否则放大后会整体偏右下。
            scale: 1.35
            transformOrigin: Item.Center

            Repeater {
                model: fullFace.actions
                delegate: ActionButton {
                    theme: fullFace.th
                    label: modelData && modelData.label ? modelData.label : String(modelData)
                    role: modelData && modelData.role ? modelData.role
                                                      : (index === 0 ? "primary" : "secondary")
                    onClicked: {
                        if (fullFace.bridgeRef && fullFace.bridgeRef.fireAction)
                            fullFace.bridgeRef.fireAction(label)
                    }
                }
            }
        }
    }

    // 发生时刻（右上角）：公告板语义 ⇒ 与描述同字号（fsTitleSm），不缩成小字。
    Text {
        id: fTime
        visible: fullFace.time !== ""
        anchors.right: parent.right
        anchors.rightMargin: 40
        anchors.top: parent.top
        anchors.topMargin: 32
        text: fullFace.time
        color: fullFace.onSub
        font.pixelSize: fullFace.th ? fullFace.th.fsTitleSm : 18
    }
}
