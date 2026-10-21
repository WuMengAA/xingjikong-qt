// MD3 侧滑详情面板（SideSheet）· 宽 400px · V1.0 P1 骨架层
//
// 规格（用户点单）：点击某台设备，从右侧滑出 400px 宽的面板，
//   面板带 Level 3 阴影，主背景覆盖 50% 黑色遮罩。
//
// ── 一处对规格的有意偏离（必须写明）─────────────────────────────────
// 规格写「面板内分左右两块：左侧 VNC 实时画面、右侧摄像头画面与回放」。
// 但面板只有 400px 宽，左右分块 ⇒ 每块不足 200px，16:10 画面只有约 120px 高，
// 教室里根本看不清 ⇒ **改为上下分块**：上方实时画面（约 200px 高），
// 下方摄像头 / 回放占位。等面板需要加宽（如 720px）时再切回左右。
//
// ── 画面源：用 frames 的 uid 通道，不是 VNC ───────────────────────────
// · image://frames/<uid> = 该设备的缩略图帧（FrameImageProvider，main.cpp:253-261）
//   ⇒ 单机面板要的是"这台设备的画面"，走这条最合适，且不切主画面订阅。
// · image://vnc/live    = 当前选中设备的 VNC 实时流（集控页 / 监控页在用）。
// 两者分工明确：面板看单台走 frames/<uid>，大画面远控走 vnc-data ——
// 与蓝图「控制指令走 instruction，别走 VNC 底层通道」是同一条纪律。
import QtQuick
import QtQuick.Layouts
import "../components/md3"
import "../island"          // SvgIcon

Item {
    id: root

    property var ctx
    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null
    property bool opened: false
    /** 当前展开的设备对象（来自 backend.devices 的一条，原样透传）。 */
    property var device: null

    signal closed()

    visible: root.opened || scrimOut.running || drawerOut.running
    anchors.fill: parent

    // ── 遮罩：50% 黑（规格）──────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.5)
        opacity: root.opened ? 1 : 0
        Behavior on opacity { NumberAnimation { id: scrimOut; duration: root.th ? root.th.durMedium : 350 } }
        MouseArea {
            anchors.fill: parent
            enabled: root.opened
            onClicked: root.closed()
        }
    }

    // ── 抽屉容器：靠 x 位移滑入滑出 ──────────────────────────────────
    Item {
        id: drawer
        width: 400
        height: parent.height
        x: root.opened ? (root.width - 400) : root.width
        Behavior on x { NumberAnimation { id: drawerOut; duration: root.th ? root.th.durMedium : 350 } }

        // Level 3 阴影：两层向左扩散的半透明黑（无外部依赖的近似实现）
        // ⚠️ 为什么不用 MultiEffect：它要 import QtQuick.Effects，本端没引过；
        //    为骨架起见不擅自加模块依赖，等点单再换规范实现。
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -16
            anchors.topMargin: 10
            radius: 20
            color: Qt.rgba(0, 0, 0, 0.10)
        }
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -7
            anchors.topMargin: 4
            radius: 17
            color: Qt.rgba(0, 0, 0, 0.12)
        }

        // 面板本体
        Rectangle {
            anchors.fill: parent
            radius: root.th ? root.th.rL : 16
            color: root.th ? root.th.cSurface : "#FFFBFE"
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            // 标题行
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    text: (root.device && (root.device.name || root.device.uid)) ? (root.device.name || root.device.uid) : "设备详情"
                    font.pixelSize: 16
                    font.weight: Font.Medium
                    color: root.th ? root.th.cOnSurface : "#1C1B1B"
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Md3Btn {
                    md3: root.th
                    text: "关闭"
                    variant: "text"
                    onClicked: root.closed()
                }
            }

            // ── 上：实时画面（frames/<uid> 通道）────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                radius: root.th ? root.th.rM : 12
                color: root.th ? root.th.cContLow : "#F7F2FA"
                border.width: 1
                border.color: root.th ? root.th.cOutlineVar : "#CAC4D0"
                clip: true

                Image {
                    id: liveImg
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    property int tick: 0
                    source: (root.device && root.device.uid) && backend.frameSource !== "vnc"
                            ? ("image://frames/" + root.device.uid + "?t" + tick) : ""
                }
                Connections {
                    target: backend
                    function onFrameChanged() { liveImg.tick++ }
                }
                Text {
                    anchors.centerIn: parent
                    visible: backend.frameSource === "vnc"
                    text: "实时画面在「集控」页（VNC 链路）"
                    font.pixelSize: 12
                    color: root.th ? root.th.cOutline : "#79747E"
                }
                Text {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 8
                    text: backend.frameSource === "vnc" ? "实时" : "轮询"
                    font.pixelSize: 12
                    color: root.th ? root.th.cOnSurfaceVar : "#49454F"
                }
            }

            // ── 操作：三个动作，全部走宿主唯一执行入口 ─────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Md3Btn {
                    md3: root.th
                    Layout.fillWidth: true
                    text: "屏幕截图"
                    variant: "tonal"
                    radius: 12
                    onClicked: if (root.ctx) root.ctx.window.runActionById("screenshot")
                }
                Md3Btn {
                    md3: root.th
                    Layout.fillWidth: true
                    text: "锁定桌面"
                    variant: "error"
                    radius: 12
                    onClicked: if (root.ctx) root.ctx.window.runActionById("lock")
                }
                Md3Btn {
                    md3: root.th
                    Layout.fillWidth: true
                    text: "远程重启"
                    variant: "error"
                    radius: 12
                    onClicked: if (root.ctx) root.ctx.window.runActionById("reboot")
                }
            }

            // ── 下：摄像头画面与回放（占位 · 附真因说明）────────────────
            // ⚠️ 为什么是占位：被控端**有**采集能力（ffmpeg DirectShow，
//    src/main.cpp 相机四件套，2026-10-09 起），但**上行实时流轨**
//    （WebRTC / libdatachannel）已于 2026-10-18 彻底移除，云端侧
//    也没有 camera 转发端点 ⇒ 实时画面当前无数据源。
//    快照 / 录制类指令（camera_snapshot / camera_record_*）走
//    instruction 通道，是能用的，接线等单独点单。
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                radius: root.th ? root.th.rM : 12
                color: "transparent"
                border.width: 1
                border.color: root.th ? root.th.cOutlineVar : "#CAC4D0"
                Column {
                    anchors.centerIn: parent
                    spacing: 4
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "摄像头画面"
                        font.pixelSize: 13
                        color: root.th ? root.th.cOutline : "#79747E"
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "待接入：实时流轨已移除，云端无转发端点"
                        font.pixelSize: 12
                        color: root.th ? root.th.cOutline : "#79747E"
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
