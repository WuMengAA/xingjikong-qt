import QtQuick 2.15
import "."

// 灵动岛 · 顶部菜单栏面部（macOS 菜单栏风格 · 2026-11-01 升级）
//
// 常驻顶部**居中定宽横条**（C++ kBarW 520 × kBarH 38），三段：
//   左：应用图标（星）+「星集控」+「· 班级名」
//   中：动态课表（当前课/下一节摘要，30s 由 C++ 刷新）
//   右：通知铃（带未读角标）+ 时钟
// 点整条 → tapped()，由 IslandRoot 接 requestExpand（展开成下拉/面板）。
//
// ⚠️ 依赖显式声明成 property（隐式作用域不会跟着搬进独立文件 —— D 档拆分踩过）。
Item {
    id: face

    // ── 接口 ──────────────────────────────────────────────
    property var  th: null
    property bool idle: true
    property int  unread: 0
    property string className: ""   // 班级显示名（星集控右侧）
    property string lesson: ""      // 动态课表（中间）
    signal tapped()

    visible: face.idle
    opacity: face.idle ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: face.th ? face.th.durShort : 160 } }

    // 整条可点（空闲态 gestureArea 是 disabled，这里自带点击）
    MouseArea {
        objectName: "idleBarMA"
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: face.tapped()
    }

    // ── 左：应用标识 + 班级名 ──────────────────────────────
    Row {
        id: leftRow
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Rectangle {
            width: 16
            height: 16
            radius: 4
            color: face.th ? face.th.fg : "#FAFAFA"
            Text {
                anchors.centerIn: parent
                text: "星"
                color: face.th ? face.th.bg : "#1E1E1E"
                font.pixelSize: 10
                font.weight: Font.Medium
            }
        }
        Text {
            height: 16
            verticalAlignment: Text.AlignVCenter
            text: "星集控"
            color: face.th ? face.th.fg : "#FAFAFA"
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Text {
            height: 16
            verticalAlignment: Text.AlignVCenter
            visible: face.className !== ""
            text: face.className !== "" ? "· " + face.className : ""
            color: face.th ? face.th.sub : "#C8C8C8"
            font.pixelSize: 12
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    // ── 中：动态课表（居中，过长省略）──────────────────────
    Text {
        id: lessonLabel
        visible: face.lesson !== ""
        anchors.left: leftRow.right
        anchors.right: rightRow.left
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        maximumLineCount: 1
        text: face.lesson
        color: face.th ? face.th.sub : "#C8C8C8"
        font.pixelSize: 12
        font.weight: Font.Medium
    }

    // ── 右：时钟 + 铃铛（2026-11-01：通知放时间右边）────────────
    Row {
        id: rightRow
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        IslandClock {
            anchors.verticalCenter: parent.verticalCenter
            theme: face.th
            timePx: 13
        }

        Item {
            width: 16
            height: 16
            anchors.verticalCenter: parent.verticalCenter

            SvgIcon {
                anchors.centerIn: parent
                name: "bell"
                tint: face.th ? face.th.sub : "#C8C8C8"
                px: 16
            }

            // 未读角标
            Rectangle {
                visible: face.unread > 0
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: -6
                anchors.rightMargin: -6
                width: Math.max(14, badgeTxt.implicitWidth + 6)
                height: 14
                radius: 7
                color: "#E24B4A"
                Text {
                    id: badgeTxt
                    anchors.centerIn: parent
                    text: face.unread > 99 ? "99+" : String(face.unread)
                    color: "#FFFFFF"
                    font.pixelSize: 9
                    font.weight: Font.Medium
                }
            }
        }
    }
}
