import QtQuick 2.15
import "."

// 「收起最小化」按钮（2026-11-04 · m18334 用户点单）
//
// 放在完整收起条右端（铃铛左边）。点了 → islandCapsule.minimize()，把整条
// 缩成 112×26 的小胶囊，撑到本时段结束再弹回来。
//
// ── 为什么单独一个文件 ─────────────────────────────────────────────
//   IslandIdleFace.qml 已 194 行、红线 200；按钮自带状态层 + hover + 命中
//   三件套，塞进去必然破线（且那三件套别处还用得上）。
//
// ── 为什么用 TapHandler 而不是 MouseArea 点 ───────────────────────
//   外面的整条 MouseArea（idleBarMA）也要接点击；两个 MouseArea 会互相吞。
//   TapHandler 是「子件先抢」语义，抢到了外层就不会再收到，正是这里要的。
//
// ── 图标为什么是 chevron-right ────────────────────────────────────
//   resources/icons 里**没有** minimize / minus（44 个 svg 全量核对过），
//   拿一个不存在的名字去 image://svgicon/ 只会得到空图（静默失败）。
//   chevron-right 的"指向内侧"语义在一条贴顶横条上读起来就是"收回"。
Item {
    id: btn

    property var th: null
    property bool hovered: false
    property string iconName: "chevron-right"

    implicitWidth: 18
    implicitHeight: 18
    signal clicked()

    Rectangle {
        anchors.centerIn: parent
        width: 18
        height: 18
        radius: 9
        color: btn.th ? btn.th.fg : "#FAFAFA"
        opacity: btn.hovered ? 0.20 : 0.10
        Behavior on opacity { NumberAnimation { duration: btn.th ? btn.th.durShort : 160 } }
    }

    SvgIcon {
        anchors.centerIn: parent
        name: btn.iconName
        tint: btn.th ? btn.th.sub : "#C8C8C8"
        px: 12
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: btn.clicked()
    }

    // hover 只做视觉反馈，不吃点击（acceptedButtons: NoButton）
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        onEntered: btn.hovered = true
        onExited: btn.hovered = false
    }
}
