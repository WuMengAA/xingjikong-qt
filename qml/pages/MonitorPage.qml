// 监控页（整屏页 · U5「两端同 shell」A-2 第四页 · 2026-10-19 新增）
// ───────────────────────────────────────────────────────────────────────────
// 解决的问题（用户反馈 2026-10-19）：「监控没有独立界面和操作 UI」——
// 此前监控能力散落各处：摄像头在 CameraCenterDialog 弹窗、多班墙在
// MultiThumbDialog 弹窗、实时画面在控制页中栏，老师打开管理端没有一个
// "去监控"的整页入口。
//
// 本页是**监控中心**一页收拢：
//   ① 设备选择（Md3Chip 横条，与手机端 MonitorPage 同一套心智）
//   ② 主画面（实时帧流 image://frames/f + fps/链路徽标）
//   ③ 操作栏（截图 / 摄像头 / 多班墙 / 远控 —— 全部复用现有后端动作）
//
// 契约（同 ControlPage/SettingsPage，见 UiPageHost 的 _ctx()）：
//   ctx.window = 主界面 root；backend = C++ 上下文属性（本文件内可直接访问）
import QtQuick
import QtQuick.Layouts
import "../components/md3"

Item {
    id: page

    property var ctx
    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

    Rectangle { anchors.fill: parent; color: th ? th.body : "#141414" }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        // ── ① 设备选择横条（MD3 filter chip，同一套心智）──────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            radius: 12
            color: th ? th.cContLow : "#1E1E1E"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                Text {
                    text: "看哪台："
                    color: th ? th.cOnSurfaceVar : "#C8C8C8"
                    font.pixelSize: 13
                }

                ListView {
                    id: watchList
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    orientation: ListView.Horizontal
                    clip: true
                    spacing: 8
                    model: backend.devices
                    delegate: Md3Chip {
                        // Md3Chip 自带 implicitWidth（text + 32），横条里随内容撑
                        md3: th
                        text: (modelData.name && modelData.name !== "") ? modelData.name : modelData.uid
                        selected: backend.currentUid === modelData.uid
                        onClicked: {
                            backend.currentUid = modelData.uid
                            ctx.window.currentUid = backend.currentUid
                            // 选中的那台要真的开始拉帧（与集控页同语义）
                            backend.sendAction("screenshot", {})
                        }
                    }
                }
            }
        }

        // ── ② 主画面（实时帧流）────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: th ? th.panel : "#1E1E1E"
            border.color: th ? th.cOutlineVar : "#3C3C3C"
            border.width: 1
            clip: true

            Image {
                id: frameImg
                anchors.fill: parent
                anchors.margins: 8
                fillMode: Image.PreserveAspectFit
                cache: false
                property int tick: 0
                source: ctx.window.frameReady ? ("image://frames/f?" + tick) : ""
                Connections {
                    target: backend
                    function onFrameChanged() { frameImg.tick++ }
                }
            }

            // 未选设备 / 无帧时的引导（不弹空白）
            Text {
                anchors.centerIn: parent
                text: {
                    if (!backend.loggedIn) return "还没登录 —— 点右上角账户先登录"
                    if (backend.currentUid === "") return "先在顶部选一台设备看监控"
                    if (!ctx.window.frameReady) return "正在等该设备的画面…"
                    return ""
                }
                visible: text !== ""
                color: th ? th.cOnSurfaceVar : "#C8C8C8"
                font.pixelSize: 14
            }

            // 链路徽标（与控制页同一语义：实时/轮询/等待）
            Rectangle {
                visible: backend.currentUid !== ""
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 10
                height: 20
                radius: 4
                color: ctx.window.linkBusy ? (th ? th.todo : "#5B8CFF")
                     : (backend.frameSource === "jpeg" ? "transparent" : (th ? th.inv : "#FFFFFF"))
                border.color: th ? th.stroke : "#3C3C3C"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: ctx.window.linkBusy ? "正在接通…"
                        : backend.frameSource === "jpeg" ? "轮询"
                        : "实时"
                    color: ctx.window.linkBusy
                           ? (th ? th.win : "#FFFFFF")
                           : (backend.frameSource === "jpeg" ? (th ? th.fg3 : "#C8C8C8") : (th ? th.win : "#000000"))
                    font.pixelSize: 11
                }
            }
        }

        // ── ③ 操作栏（全部复用现有后端动作，给监控场景的快捷操作）──────
        Row {
            Layout.fillWidth: true
            spacing: 10

            Md3Btn { md3: th; text: "截图"; variant: "tonal"; onClicked: ctx.window.runActionById("screenshot") }
            Md3Btn {
                md3: th; text: "摄像头"; variant: "tonal"
                visible: backend.capActions.indexOf("camera_list") >= 0
                onClicked: ctx.window.openOnly("camera")
            }
            Md3Btn { md3: th; text: "多班墙"; variant: "tonal"; onClicked: ctx.window.openOnly("multiband") }
            Md3Btn { md3: th; text: "远程控制"; variant: "tonal"; onClicked: ctx.window.runActionById("remote-start") }

            // 停止看当前设备（outlined 弱化；Md3Btn 无 error 变体色的 text 写法，
            // 用 outlined + 红色 icon 近似 —— 语义仍清楚）
            Md3Btn {
                md3: th; text: "⏹ 停止"; variant: "outlined"
                onClicked: { backend.currentUid = ""; ctx.window.currentUid = "" }
            }
        }
    }
}