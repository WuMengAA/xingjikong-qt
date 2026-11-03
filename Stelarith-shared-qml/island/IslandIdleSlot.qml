import QtQuick 2.15
import "."

// 灵动岛 · 收起条上的单个组件槽（2026-11-04 · m16757/m16706 用户裁决）
//
// ── 为什么要有这个文件 ────────────────────────────────────────────
//   用户原话：「时钟什么的组件应该显示在**收起态**上，而不是放到页面里面」
//   「灵动岛触发展开除了通知，其他的改为按**收起态上面的各种按钮组件**进行展开，
//     长按进入编辑模式」。
//   实测本机 ClassIsland（`D:/Classlsland`）主界面：一条常驻横条上并排
//   「倒计时 | 天气 | 日期周次 | 时钟 | 课表简称条」——**组件就是横条上的单元**，
//   不是"展开后翻页看"的页面。本文件就是那个单元。
//
// ── 交互语义（与 ClassIsland 对齐）────────────────────────────────
//   · 单击 → picked(id)：宿主要「展开对应组件」并翻到那一面（不是随便展开概览）
//   · 长按 → longPressed(id)：宿主进编辑模式（可拖拽排序/增删）
//   · hover/press 用状态层（不改底色）——改底色是"塑料感"主因，同 §9.5
//
// ── 为什么不在这里读数据 ──────────────────────────────────────────
//   数据一律由宿主（IslandRoot）用 panelSource / bridge 取好传进来：
//   本文件只画 + 收点击。QML 不解析业务数据（§10.9）。
Item {
    id: slot

    // ── 接口 ──────────────────────────────────────────────
    property var th: null
    property int faceId: -1          // 与 IslandLayoutModel 的 id 对齐
    property string icon: ""         // SvgIcon 名（空=不画图标）
    property string text: ""         // 主文案
    property string sub: ""          // 次要文案（如剩余分钟），可空
    property bool accent: false      // 正在发生（正在上课/倒计时到点）→ 强调色
    property bool wide: false        // 需要更宽（课表横条）
    property bool isClock: false     // 时钟槽：文案由 IslandClock 自己滴答（C++ 不逐秒 emit）
    signal picked(int id)
    signal longPressed(int id)

    implicitWidth: wide ? Math.min(360, contentRow.implicitWidth + 20)
                        : contentRow.implicitWidth + 20
    implicitHeight: 26

    // 药丸底：收起条本身就是深色横条，槽底再深一档形成"按钮"层次。
    // 不用 border（§9.4 无边框原则），靠 surfaceVariant 色差 + 圆角。
    Rectangle {
        anchors.fill: parent
        radius: 8
        color: {
            if (th && th.surfaceVariant) return th.surfaceVariant
            return "#2A2A2A"
        }
        opacity: slot.accent ? 0.30 : 0.55
        // 按下时再压一点（状态层，不改色相）
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: th ? th.fg : "#FAFAFA"
            opacity: ma.pressed ? 0.10 : 0
            Behavior on opacity { NumberAnimation { duration: th ? th.durShort : 160 } }
        }
    }

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: 6

        SvgIcon {
            visible: slot.icon !== ""
            anchors.verticalCenter: parent.verticalCenter
            name: slot.icon
            tint: slot.accent ? (th ? th.accent : "#D0BCFF") : (th ? th.sub : "#C8C8C8")
            px: 13
        }

        Text {
            id: mainText
            anchors.verticalCenter: parent.verticalCenter
            // 课表槽要横滚而不是省略 —— 见 wide 分支
            visible: !slot.isClock
            text: slot.text
            color: slot.accent ? (th ? th.accent : "#D0BCFF") : (th ? th.fg : "#FAFAFA")
            font.pixelSize: th ? th.fsBody : 13
            font.weight: slot.accent ? Font.Medium : Font.Normal
            elide: slot.wide ? Text.ElideNone : Text.ElideRight
            maximumLineCount: 1
        }

        // 时钟槽：走共用的 IslandClock（500ms 滴答），与旧收起条同一份实现，
        // 不新造第二套秒针（两套秒针必然在一秒里对不上）。
        IslandClock {
            visible: slot.isClock
            anchors.verticalCenter: parent.verticalCenter
            theme: slot.th
            timePx: slot.th ? slot.th.fsBody : 13
        }

        Text {
            visible: slot.sub !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: slot.sub
            color: th ? th.sub : "#C8C8C8"
            font.pixelSize: th ? th.fsNote : 12
        }
    }

    // 点击 + 长按：两个独立 handler，TapHandler 的 longPressed 与 tapped 互斥
    // （长按超过 longPressThreshold 后松手不再触发 tapped）。
    // ⚠️ 不能用 MouseArea + Timer 自造：Qt 的 TapHandler 已处理"移动即取消"，
    //    自造版在横滑（DragHandler 抢事件）时会把滑动误判成长按。
    TapHandler {
        acceptedButtons: Qt.LeftButton
        longPressThreshold: 0.8
        onTapped: slot.picked(slot.faceId)
        onLongPressed: slot.longPressed(slot.faceId)
    }

    // ma 只为状态层的 pressed 读数（TapHandler 没有 pressed 属性）。
    // 不参与手势判定 —— passive 不抢 grab。
    MouseArea {
        id: ma
        anchors.fill: parent
        acceptedButtons: Qt.NoButton   // 只报 hover/pressed，不吃点击
        hoverEnabled: true
    }
}
