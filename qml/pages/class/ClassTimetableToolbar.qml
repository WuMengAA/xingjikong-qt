// 班级课表 · 工具栏（标题 / 群名 / 导入 + 刷新 / 选群条）· 2026-10-25 拆件
//
// 拆件原因：ClassTimetablePane 已 287 行（红线 200）。工具栏是纯展示 + 两个意图信号，
// 抽出来之后 Pane 只留状态与网络回执。
//
// 选群条只在档案含多个课表群时出现。**完整档案有 12 个群、而且 12 个群全叫
// 「新课表群」**（1~12 班全校课表）—— 靠名字分不出谁是谁，云端已按「ClassPlans 首次
// 出现顺序」回 label（1班…12班）并标 inferred。所以：
//   · 名字可信（1班课表群/8班课表群）→ 显示群名；
//   · 名字不可信（12 个同名）→ 显示推断班名 + 明确提示"请核对"，绝不假装知道。
// 群不猜：猜错就是把 3 班课表挂到 4 班。
import QtQuick
import "../../components/fresh"  // FreshBtn
import "../../components/theme"  // Theme 设计令牌单例（pragma Singleton）
import "../../island"            // SvgIcon

Column {
    id: bar

    property string subtitle: ""
    property bool canImport: false
    property var groups: []
    /** true = 班名是按档案顺序推的（12 群同名），界面必须提示人工核对。 */
    property bool nameInferred: false
    /** 下发目标班级 + ClassAdmin 引用（2026-11-02 课表下发）。
     *  为什么由父级传引用而不是自己 `backend`：MainWindow 作用域里裸 backend 恒为 null，
     *  本项目统一用 backendRef 显式传入（见 PreviewTopBar 的同款注释）。 */
    property string classCode: ""
    property var adminRef: null
    /** 送达视图：{total, delivered:[{uid,ok,days,error,at}], pending:[uid]}。
     *  一次下发后自动拉一次 —— 「已发送」和「已落盘」是两件事，老师要能看见差别。 */
    property var delivery: null

    signal importRequested()
    signal refreshRequested()
    signal groupPicked(string groupId)

    /** 下发 + 立刻回读送达情况（等设备回执要时间，先给当前快照，用户可再点刷新）。 */
    function pushAndPoll() {
        if (!bar.adminRef || bar.classCode === "") return
        bar.adminRef.pushTimetable(bar.classCode)
        bar.adminRef.timetableDelivery(bar.classCode)
    }

    Connections {
        target: bar.adminRef
        ignoreUnknownSignals: true
        function onTimetableDeliveryList(code, delivered, pending) {
            if (code !== bar.classCode) return
            bar.delivery = { total: delivered.length + pending.length,
                             delivered: delivered, pending: pending }
        }
    }

    width: parent ? parent.width : 0
    spacing: 6

    // ── 标题行 ──
    Item {
        width: parent.width
        height: 30

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            SvgIcon {
                name: "clock"; px: 16; tint: Theme.color.success || "#22C55E"
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: "课表"
                font.pixelSize: 16
                font.weight: Font.Medium
                color: Theme.color.textPrimary || "#111827"
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: bar.subtitle
                font.pixelSize: 12
                color: Theme.color.textSecondary || "#6B7280"
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            FreshBtn {
                text: "导入档案"; icon: "upload"; variant: "tonal"
                enabled: bar.canImport
                tip: "选择 ClassIsland 档案（Default.json），选群后导入本班"
                onClicked: bar.importRequested()
            }
            FreshBtn {
                text: "刷新"; variant: "ghost"
                enabled: bar.canImport
                onClicked: bar.refreshRequested()
            }
            // 下发到教室机（2026-11-02）：改造前课表只到云端库，教室机读的是它本机档案。
            FreshBtn {
                text: "下发到设备"; icon: "send"; variant: "filled"
                enabled: bar.canImport && bar.classCode !== ""
                tip: "把这份课表推给本班所有在线设备（离线设备联网后自动收到）"
                onClicked: bar.pushAndPoll()
            }
        }
    }

    // ── 送达条：只说「已下发」等于没说，送达 0 和送达 30 长得一样 ──
    Text {
        width: parent.width
        visible: bar.delivery !== null
        wrapMode: Text.WordWrap
        text: {
            const d = bar.delivery
            if (!d) return ""
            const ok = (d.delivered || []).filter(function (x) { return x.ok })
            const bad = (d.delivered || []).filter(function (x) { return !x.ok })
            let s = "送达：" + ok.length + " 台已落盘"
            if (bad.length > 0) s += " · " + bad.length + " 台失败（" + (bad[0].error || "未说明原因") + "）"
            if ((d.pending || []).length > 0) s += " · " + d.pending.length + " 台未回执（离线或未装新版）"
            return s
        }
        font.pixelSize: 12
        color: {
            const d = bar.delivery
            if (!d) return Theme.color.textSecondary || "#6B7280"
            const bad = (d.delivered || []).filter(function (x) { return !x.ok })
            return (bad.length > 0 || (d.delivered || []).length === 0)
                   ? (Theme.color.warning || "#D97706")
                   : (Theme.color.textSecondary || "#6B7280")
        }
    }

    // ── 选群条（档案含多个班才出现）──
    Column {
        width: parent.width
        visible: bar.groups.length > 1
        spacing: 6

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: bar.nameInferred
                  ? ("这份档案有 " + bar.groups.length + " 个课表群、而且群名都一样："
                     + "下面按档案顺序编号为 1班…" + bar.groups.length + "班，导入前请对照课表核对。")
                  : ("这份档案有 " + bar.groups.length + " 个课表群，选一个导入本班：")
            font.pixelSize: 12
            color: bar.nameInferred ? (Theme.color.destructive || "#DC2626") : (Theme.color.textSecondary || "#6B7280")
        }
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: bar.groups
                Rectangle {
                    width: gTxt.implicitWidth + 24
                    height: 28
                    radius: 14
                    color: gMa.containsMouse ? (Theme.color.surface || "#ECE6F0") : (Theme.color.surfaceVariant || "#EEF0F3")
                    Text {
                        id: gTxt
                        anchors.centerIn: parent
                        // 推断时群名是 12 个「新课表群」，显示出来只会误导 ⇒ 只显示「X班」
                        text: (modelData.inferred === true
                               ? (modelData.label || "")
                               : (modelData.name || modelData.label || ""))
                              + " · " + (modelData.days || 0) + "天"
                        font.pixelSize: 13
                        color: Theme.color.textPrimary || "#111827"
                    }
                    MouseArea {
                        id: gMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: bar.groupPicked(modelData.id)
                    }
                }
            }
        }
    }
}
