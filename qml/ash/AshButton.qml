import QtQuick

// 按钮（shadcn Button）
//
// 兼容原 Md3Btn 的调用写法（`variant: "filled" / "tonal" / "text"`），
// 这样 34 处调用点改 import 与类型名即可，不必逐处改参数。
// 同时接受 shadcn 的语义名（default / secondary / ghost / outline / destructive）。
//
// ⚠️ QML 零逻辑：
//   · variant 是**长期属性**，用 State + when（互斥）切；
//   · hover/press 是**瞬时交互**，用独立图层叠透明度（bool→real 直接绑定），
//     不塞进 State —— 否则两者会互相顶掉（同一时刻只能有一个 state 生效）。
//   · 全程无三元、无 Math。
Item {
    id: root

    property var th
    property string text: ""
    property string variant: "filled"
    /** 圆角：shadcn rounded-md(6)；内层底色/覆盖层复用，避免引用不存在的 parent.radius。 */
    property int radius: th ? th.radiusMd : 6

    signal clicked()

    implicitWidth: label.implicitWidth + (th ? th.rowPadH : 12) * 2
    implicitHeight: th ? th.controlH : 36
    opacity: enabled ? 1.0 : 0.5

    // ── 底色层（颜色由下方 State 改）──
    Rectangle {
        id: bg
        anchors.fill: parent
        radius: root.th ? root.th.radiusMd : 6
        color: root.th ? root.th.primary : "#18181b"
        border.width: 0
        border.color: root.th ? root.th.border : "#e4e4e7"
    }

    // ── hover / press 覆盖层（独立于 variant）──
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.th ? root.th.foreground : "#09090b"
        opacity: ma.containsMouse * 0.08
        Behavior on opacity { NumberAnimation { duration: root.th ? root.th.durFast : 120 } }
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.th ? root.th.foreground : "#09090b"
        opacity: ma.pressed * 0.06
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: root.th ? root.th.fsSm : 14
        font.weight: Font.Medium
        color: root.th ? root.th.primaryFg : "#fafafa"
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: root.enabled
        onClicked: root.clicked()
    }

    // ══ variant → State（when 互斥；都不成立时回 base = primary）══
    state: ""
    states: [
        State {
            name: "secondary"
            when: root.variant === "tonal" || root.variant === "secondary"
            PropertyChanges {
                target: bg
                color: root.th ? root.th.secondary : "#f4f4f5"
            }
            PropertyChanges {
                target: label
                color: root.th ? root.th.secondaryFg : "#18181b"
            }
        },
        State {
            name: "ghost"
            when: root.variant === "text" || root.variant === "ghost"
            PropertyChanges {
                target: bg
                color: "transparent"
            }
            PropertyChanges {
                target: label
                color: root.th ? root.th.foreground : "#09090b"
            }
        },
        State {
            name: "outline"
            // "cancel" 也走这档：Dialog 的 actions 里 role=cancel 的按钮在 shadcn 里是 outline
            when: root.variant === "outline" || root.variant === "cancel"
            PropertyChanges {
                target: bg
                color: "transparent"
                border.width: 1
            }
            PropertyChanges {
                target: label
                color: root.th ? root.th.foreground : "#09090b"
            }
        },
        State {
            name: "destructive"
            when: root.variant === "destructive" || root.variant === "danger"
            PropertyChanges {
                target: bg
                color: root.th ? root.th.destructive : "#ef4444"
            }
            PropertyChanges {
                target: label
                color: root.th ? root.th.destructiveFg : "#fafafa"
            }
        }
    ]
}
