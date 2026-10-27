import QtQuick 2.15
import "."

// 灵动岛 · 统一列表行（统一布局规范 · 基元三）
//
// 历史面板 / 事件列表 / 通知流共用的 48px 行：**图标 + 未读点 + 标题 + 描述(2 行)
// + 时间 + 分类标签**。取代各面各自手搓的 48px 行渲染（漂移根因），所有度量走 th.*。
//
// ⚠️ 接口字段与 bridge.history 条目字段对齐（icon/title/desc/timeStr/category/
//    level/read/id），调用方直接透传；点击经 onClicked 上报，由上层决定 open 行为。
//
// 接口：
//   th        主题令牌（必传）
//   icon      前置语义图标（默认 bell）
//   title     标题（未读加粗）
//   desc      描述（2 行，WordWrap）
//   time      右上时间
//   category  描述行右侧小标签
//   level     "error"|"warning"|"" → 分类标签配色
//   unread    未读（左侧点 + 标题加粗）
//   onClicked 整行点击
Item {
    id: row
    property var th: null
    property string icon: "bell"
    property string title: ""
    property string desc: ""
    property string time: ""
    property string category: ""
    property string level: ""      // "error" | "warning" | ""
    property bool unread: false
    // ⚠️ Qt6：Item.height 是 FINAL 属性，不能 redeclare「property int height」，
    // 否则运行期报 "Cannot override FINAL property"。改用自定义名 + 绑定继承的 height。
    property int rowHeight: (th ? th.rowH : 48)
    signal clicked()
    implicitHeight: row.rowHeight
    height: row.rowHeight

    property bool _hov: false

    // 行底色（hover 加深，时长走 durInstant）
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: row.th ? row.th.rRow : 8
        color: row._hov
               ? (row.th && row.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
               : (row.th && row.th.light ? Qt.rgba(0, 0, 0, 0.02) : Qt.rgba(1, 1, 1, 0.04))
        Behavior on color { ColorAnimation { duration: row.th ? row.th.durInstant : 80 } }
    }

    // 未读小圆点（标题左侧）
    Rectangle {
        visible: row.unread
        anchors.left: parent.left
        anchors.leftMargin: 3
        anchors.verticalCenter: parent.verticalCenter
        width: 5; height: 5; radius: 2.5
        color: row.th ? row.th.accent : "#D0BCFF"
    }

    SvgIcon {
        id: ic
        name: row.icon
        tint: row.th ? row.th.sub : "#C8C8C8"
        px: 15
        anchors.left: parent.left
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        id: ttl
        anchors.left: ic.right
        anchors.leftMargin: 6
        anchors.right: tim.left
        anchors.rightMargin: 6
        anchors.top: parent.top
        anchors.topMargin: 5
        text: row.title
        color: row.th ? row.th.fg : "#FAFAFA"
        font.pixelSize: 13
        font.weight: row.unread ? Font.DemiBold : Font.Medium
        elide: Text.ElideRight
    }

    Text {
        anchors.left: ttl.left
        anchors.right: cat.left
        anchors.rightMargin: 6
        anchors.top: ttl.bottom
        text: row.desc
        color: row.th ? row.th.sub : "#C8C8C8"
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Text {
        id: cat
        anchors.right: tim.right
        anchors.top: ttl.bottom
        anchors.topMargin: 2
        text: row.category
        color: row.level === "error" ? "#e5484d"
               : (row.level === "warning" ? "#f5a623" : (row.th ? row.th.sub : "#C8C8C8"))
        font.pixelSize: 10
        opacity: 0.85
    }

    Text {
        id: tim
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.top: parent.top
        anchors.topMargin: 5
        text: row.time
        color: row.th ? row.th.sub : "#C8C8C8"
        font.pixelSize: 11
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: row._hov = true
        onExited: row._hov = false
        onClicked: row.clicked()
    }
}
