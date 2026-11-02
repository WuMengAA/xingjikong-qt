// 侧滑详情面板（SideSheet）· 宽 400px · QtShadcn 规范（2026-10-29 迁移）
//
// 规格（用户点单）：点击某台设备，从右侧滑出 400px 宽的面板，
//   主背景覆盖 50% 黑色遮罩。
//
// ── 竖屏自适应（2026-10-22 用户点单「竖屏横屏两种布局模式」）────────────
// 面板宽从写死 400 改为 `min(400, 宿主宽)`：手机竖屏（≈390）自动变**整宽**面板；
//   桌面竖条形窗口仍是 400 右板。窄屏同时压缩两块画面占位高度（200→150 / 96→72）。
//
// ── 一处对规格的有意偏离（必须写明）─────────────────────────────────
// 规格写「面板内分左右两块」。但面板只有 400px 宽，左右分块 ⇒ 每块不足 200px，
// 16:10 画面只有约 120px 高，教室里根本看不清 ⇒ **改为上下分块**。
//
// ── 画面源：用 frames 的 uid 通道，不是 VNC ───────────────────────────
// · image://frames/<uid> = 该设备的缩略图帧（FrameImageProvider）
// · image://vnc/live    = 当前选中设备的 VNC 实时流（集控页 / 监控页在用）
// 两者分工明确：面板看单台走 frames/<uid>，大画面远控走 vnc-data。
//
// ── 2026-10-29 迁移要点 ────────────────────────────────────────────────
// · **删除全部 border**（规范铁律）：原先描边勾勒占位区，改由底色差表达
//   监控中心/ActionBar 的局部点单（C 案）；本件当年只是"跟随"，故回落全局
//   `Theme.radius.md`。
// · 阴影改 MultiEffect，保留"同形同色源"降级技巧：特效失效只丢阴影、不丢面板
import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import "../components"            // AppButton（共享 UI，构建期拷到 components/）
import "../components/theme"     // Theme 令牌
import "../island"               // SvgIcon
import "../UiPlugins.js" as UiPlugins   // H6：面板入口清单（唯一真源）

Item {
    id: root

    property var ctx
    property bool opened: false
    /** 当前展开的设备对象（来自 backend.devices 的一条，原样透传）。 */
    property var device: null

    signal closed()

    readonly property real drawerW: Math.min(400, root.width)
    readonly property bool narrow: root.width < 480

    function panelEntries() {
        const want = ["notify", "file", "control", "term"]
        var all = []
        try { all = UiPlugins.manifest() } catch (e) { return [] }
        var out = []
        for (var i = 0; i < all.length; ++i)
            if (want.indexOf(all[i].id) >= 0) out.push(all[i])
        return out
    }

    visible: root.opened || scrimOut.running || drawerOut.running
    anchors.fill: parent

    // ── 遮罩：Theme.color.scrim（50% 黑）────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Theme.color.scrim
        opacity: root.opened ? 1 : 0
        Behavior on opacity { NumberAnimation { id: scrimOut; duration: Theme.motion.normal } }
        MouseArea {
            anchors.fill: parent
            enabled: root.opened
            onClicked: root.closed()
        }
    }

    // ── 抽屉容器：靠 x 位移滑入滑出 ──────────────────────────────────
    Item {
        id: drawer
        width: root.drawerW
        height: parent.height
        x: root.opened ? (root.width - root.drawerW) : root.width
        Behavior on x { NumberAnimation { id: drawerOut; duration: Theme.motion.normal } }

        // 阴影：同形同色源 + MultiEffect（特效失效时自身即面板，视觉无破损）
        Rectangle {
            id: shadowSrc
            anchors.fill: parent
            radius: Theme.radius.lg
            color: Theme.color.surface
            layer.enabled: true
        }
        MultiEffect {
            source: shadowSrc
            anchors.fill: shadowSrc
            shadowEnabled: true
            shadowColor: Theme.shadow.soft
            shadowBlur: 1.0
            shadowVerticalOffset: 2
            shadowHorizontalOffset: -6      // 向左投影（面板贴右缘）
        }

        // 面板本体（普通 Rectangle：不依赖特效，永远可见）
        Rectangle {
            anchors.fill: parent
            radius: Theme.radius.lg
            color: Theme.color.surface
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing.md
            spacing: Theme.spacing.sm + 4

            // 标题行
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.sm
                Text {
                    text: (root.device && (root.device.name || root.device.uid)) ? (root.device.name || root.device.uid) : "设备详情"
                    font.pixelSize: 16
                    font.weight: Font.Medium
                    color: Theme.color.textPrimary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                AppButton {
                    text: "关闭"
                    variant: "ghost"
                    size: "sm"
                    onClicked: root.closed()
                }
            }

            // ── 上：实时画面（frames/<uid> 通道）────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.narrow ? 150 : 200
                radius: Theme.radius.md
                color: Theme.color.surfaceVariant
                // 无边框（规范）：靠 surfaceVariant 与 surface 的色差分区
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
                    color: Theme.color.textSecondary
                }
                Text {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: Theme.spacing.sm
                    text: backend.frameSource === "vnc" ? "实时" : "轮询"
                    font.pixelSize: 12
                    color: Theme.color.textSecondary
                }
            }

            // ── 操作：三个动作，全部走宿主唯一执行入口 ─────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing.sm
                AppButton {
                    Layout.fillWidth: true
                    text: "屏幕截图"
                    variant: "secondary"
                    onClicked: if (root.ctx) root.ctx.window.runActionById("screenshot")
                }
                AppButton {
                    Layout.fillWidth: true
                    text: "锁定桌面"
                    variant: "destructive"
                    onClicked: if (root.ctx) root.ctx.window.runActionById("lock")
                }
                AppButton {
                    Layout.fillWidth: true
                    text: "远程重启"
                    variant: "destructive"
                    onClicked: if (root.ctx) root.ctx.window.runActionById("reboot")
                }
            }

            // ── S6 远控出口：VNC 键鼠在集控页，这里给明确入口 ──────────
            AppButton {
                Layout.fillWidth: true
                text: "在集控页打开（鼠标键盘远控）"
                variant: "secondary"
                onClicked: {
                    if (!root.ctx) return
                    root.ctx.window.goPage(1)
                    root.closed()
                }
            }

            // ── 面板功能（H6）：高频面板可见入口 ──────────────────────
            Text {
                Layout.fillWidth: true
                text: "面板功能"
                font.pixelSize: 12
                color: Theme.color.textSecondary
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spacing.sm
                rowSpacing: Theme.spacing.sm
                Repeater {
                    model: root.panelEntries()
                    delegate: AppButton {
                        required property var modelData
                        Layout.fillWidth: true
                        text: modelData.title
                        variant: "secondary"
                        onClicked: if (root.ctx) root.ctx.open(modelData.id)
                    }
                }
            }

            // ── 下：摄像头画面与回放（占位 · 附真因说明）────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.narrow ? 72 : 96
                radius: Theme.radius.md
                // 原为描边占位；规范禁边框 ⇒ 用极浅弱底表达"这里是占位区"
                color: Qt.rgba(Theme.color.surfaceVariant.r,
                               Theme.color.surfaceVariant.g,
                               Theme.color.surfaceVariant.b, 0.5)
                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spacing.xs
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "摄像头画面"
                        font.pixelSize: 13
                        color: Theme.color.textSecondary
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "待接入：实时流轨已移除，云端无转发端点"
                        font.pixelSize: 12
                        color: Theme.color.textSecondary
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
