import QtQuick 2.15
import "."

// 灵动岛 · 顶部菜单栏面部（macOS 菜单栏风格 · 2026-11-04 组件化重写）
//
// ── 这一版为什么推翻旧版（m16706/m16757 用户裁决）───────────────────
//   用户原话：「时钟什么的组件应该显示在**收起态**上，而不是放到页面里面」。
//   旧版收起条只有「星集控 · 班级名 | 一句课表摘要 | 时钟 | 铃铛」——
//   课表/日期/倒计时全在**展开后翻页**里找，这是把 ClassIsland 的
//   「组件」做成了「页面」，方向就错了。
//
//   实测本机 ClassIsland（`D:/Classlsland`）主界面是一条常驻横条：
//     `🔴距离运动会还有0天 | 🌫霾10°C | 周三11/04第10周双周 | 00:58 | 早化数物英数语地地自化地 | 下午第一·07:25`
//   ——**每个组件就是一个并排单元**。本文件照这个结构重写：
//     左：应用标识 + 班级名（身份，不是组件）
//     中：**组件槽横排**（时钟 / 日期周次 / 课表简称 / 倒计时 …，由 C++ 布局模型决定哪些、什么序）
//     右：铃铛 + 未读角标（通知入口，不是组件）
//
// ── 交互（m16757）─────────────────────────────────────────────────
//   · 点组件槽 → slotPicked(id)：展开并打开**那一面**（不是笼统展开概览）
//   · 长按组件槽 → slotLongPressed(id)：进编辑模式
//   · 点空白区 → tapped()：笼统展开（保留旧入口，老师习惯点条就展开）
//
// ── 依赖一律显式声明成 property（隐式作用域不跟着搬进独立文件 —— D 档拆分踩过）
Item {
    id: face

    // ── 接口 ──────────────────────────────────────────────
    property var  th: null
    property bool idle: true
    property int  unread: 0
    property string className: ""   // 班级显示名（星集控右侧）
    /** 组件槽数据：默认由本件自己向 panelSource 取（见下方 _slotsNow）。
     *  外部也可显式喂（探针/预览），非空即优先用外部给的。
     *  [{ id:int, icon:string, text:string, sub:string, accent:bool, wide:bool }, ...]
     *  ⚠️ 只传"要显示的"——隐藏哪个由 C++ 布局模型的 enabled/inBar 决定，QML 不再过滤（§10.9）。 */
    property var slots: []
    /** 自取结果。**必须自己持有一个 property**：Q_INVOKABLE 的返回值不是可观察属性，
     *  不落地就读不到变化 —— 时钟/课表/倒计时全要逐秒走字（2026-11-03 踩过：只截到静态图）。 */
    property var _auto: []
    readonly property var _slotsNow: face.slots.length > 0 ? face.slots : face._auto

    // 1s 重取：秒针够准，又不至于让整条 Row 每秒重排两次。
    Timer {
        interval: 1000; repeat: true; running: face.idle; triggeredOnStart: true
        onTriggered: if (typeof panelSource !== "undefined" && panelSource && panelSource.barSlots)
                         face._auto = panelSource.barSlots()
    }
    // ⚠️ 取数放在本件而不是 IslandRoot：IslandRoot 登记 3220 行、已顶到门禁线，
    //    而"横条上要摆什么"本身就是本件的职责（§10.2 新代码进职能文件）。

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

    // ── 右：铃铛 + 未读角标（通知入口）──────────────────────
    Row {
        id: rightRow
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

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

    // ── 中：组件槽横排（本版核心）────────────────────────────
    // 宽度不够时右端渐隐消失 —— 宁可少显示一个槽，也不把字压成豆腐块。
    Flickable {
        id: slotFlick
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
