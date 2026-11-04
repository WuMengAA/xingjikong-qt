import QtQuick 2.15
import "."

// 灵动岛 · 顶部菜单栏面部（macOS 菜单栏风格 · 2026-11-04 组件化重写）
//
// 结构照 ClassIsland 实测主界面（一条常驻横条，每个组​件是一个并排单元）：
//   左：应用标识 + 班级名（身份）｜中：组件槽横排（时钟/日期/课表/倒计时…，序由 C++ 布局模型定）
//   右：铃铛 + 未读角标 + 「收起最小化」按钮
//
// 交互（m16757）：点槽 → 展开那一面；长按槽 → 进编辑；点空白 → 笼统展开。
// 2026-11-04（m18334）：右侧「收起最小化」→ 整条缩成 112×26 小胶囊，撑到本时段结束自动弹回；
//   点胶囊只回到本条（**不**展开整页）。
//
// 依赖一律显式声明成 property（隐式作用域不跟着搬进独立文件 —— D 档拆分踩过）
Item {
    id: face

    // ── 接口 ──────────────────────────────────────────────
    property var  th: null
    property bool idle: true
    property int  unread: 0
    property string className: ""   // 班级显示名（星集控右侧）
    /** 组件槽数据：默认本件自己向 panelSource 取（见 _slotsNow）。外部可显式喂，非空即优先用。
     *  [{ id:int, icon:string, text:string, sub:string, accent:bool, wide:bool }, ...]
     *  ⚠️ 只传"要显示的"——隐藏哪个由 C++ 布局模型决定，QML 不再过滤（§10.9）。 */
    property var slots: []
    /** 自取结果。**必须自己持有一个 property**：Q_INVOKABLE 返回值不是可观察属性，
     *  不落地就读不到变化 —— 时钟/课表/倒计时都要逐秒走字（2026-11-03 踩过：只截到静态图）。 */
    property var _auto: []
    readonly property var _slotsNow: face.slots.length > 0 ? face.slots : face._auto
    // 2026-11-04（m18334）：最小化胶囊态。几何由 C++ 收到 112×26，本件按**自己的宽度**
    // 决定要不要退成紧凑排版 —— 不新增 C++ 属性，也就不动已顶格的 island_bridge.h。
    readonly property bool _mini: face.width < 200

    // 1s 重取：秒针够准，又不至于让整条 Row 每秒重排两次。
    Timer {
        interval: 1000; repeat: true; running: face.idle; triggeredOnStart: true
        onTriggered: if (typeof panelSource !== "undefined" && panelSource && panelSource.barSlots)
                         face._auto = panelSource.barSlots()
    }
    // ⚠️ 取数放在本件而不是 IslandRoot：那边登记 3220 行已顶到门禁线，
    //    而"横条上摆什么"本身就是本件职责（§10.2 新代码进职能文件）。

    signal tapped()
    signal slotPicked(int id)
    signal slotLongPressed(int id)

    visible: face.idle
    opacity: face.idle ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: face.th ? face.th.durShort : 160 } }

    // 整条可点（空白区）—— 组件槽自己会先抢到点击，见下方槽的 TapHandler
    MouseArea {
        objectName: "idleBarMA"
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: face.tapped()
    }

    // 最小化胶囊：112×26 只装得下「星」标 + 时间，其余全隐身（点它走 C++ setMinimized(false)）
    IslandClock {
        visible: face._mini
        anchors.centerIn: parent
        theme: face.th
        timePx: 11
    }

    // ── 左：应用标识 + 班级名 ──────────────────────────────
    Row {
        id: leftRow
        visible: !face._mini
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Rectangle {
            width: 16; height: 16; radius: 4
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
            height: 16; verticalAlignment: Text.AlignVCenter
            text: "星集控"
            color: face.th ? face.th.fg : "#FAFAFA"
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Text {
            height: 16; verticalAlignment: Text.AlignVCenter
            visible: face.className !== ""
            text: face.className !== "" ? "· " + face.className : ""
            color: face.th ? face.th.sub : "#C8C8C8"
            font.pixelSize: 12
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    // ── 右：铃铛 + 未读角标 + 收起最小化 ────────────────────
    Row {
        id: rightRow
        visible: !face._mini
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        Item {
            width: 16; height: 16
            anchors.verticalCenter: parent.verticalCenter

            SvgIcon {
                anchors.centerIn: parent
                name: "bell"
                tint: face.th ? face.th.sub : "#C8C8C8"
                px: 16
            }

            Rectangle {
                visible: face.unread > 0
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: -6
                anchors.rightMargin: -6
                width: Math.max(14, badgeTxt.implicitWidth + 6)
                height: 14; radius: 7
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

        // 2026-11-04（m18334）：「收起最小化」。QML 是唯一入口，状态全在 C++（§10.9）。
        IslandMinimizeButton {
            th: face.th
            anchors.verticalCenter: parent.verticalCenter
            onClicked: if (typeof islandCapsule !== "undefined" && islandCapsule) islandCapsule.minimize()
        }
    }

    // ── 中：组件槽横排（本版核心）────────────────────────────
    // 宽度不够时允许横滑 —— 宁可少显示一个槽，也不把字压成豆腐块。
    Flickable {
        id: slotFlick
        visible: !face._mini
        anchors.left: leftRow.right
        anchors.right: rightRow.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.leftMargin: 14
        anchors.rightMargin: 12
        contentWidth: slotRow.implicitWidth
        contentHeight: height
        clip: true
        // 只有在真的很窄时才允许横滑；宽屏上让槽全部平铺（滑动的横条很反直觉）
        interactive: slotRow.implicitWidth > width
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        Row {
            id: slotRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Repeater {
                model: face._slotsNow
                delegate: IslandIdleSlot {
                    required property var modelData
                    th: face.th
                    faceId: modelData.id !== undefined ? modelData.id : -1
                    icon: modelData.icon || ""
                    text: modelData.text || ""
                    sub: modelData.sub || ""
                    accent: modelData.accent === true
                    wide: modelData.wide === true
                    isClock: modelData.isClock === true
                    scalePercent: modelData.scale !== undefined ? modelData.scale : 100
                    anchors.verticalCenter: parent.verticalCenter
                    onPicked: (id) => face.slotPicked(id)
                    onLongPressed: (id) => face.slotLongPressed(id)
                }
            }
        }
    }
}
