// 仪表盘 · 设备区（S1 统计卡 + S5 筛选 chip + S7 响应式网格 + 空态）
//
// 从 shell/MainWindow.qml 内联段抽出（#473 拆件）：外壳文件 700+ 行已经压到登记
// 上限，仪表盘这 270 行必须搬出来，否则任何接线都会让门禁变红。
//
// 本件只做「本班设备」的三种视图：数字（统计卡）、筛选（chip）、明细（网格）。
// 数据全部是输入（devices 已按班级过滤过，这里只按 chip 再筛一层），
// 事件原样抛给宿主——不自己改 openUid、不自己写 backend（选择路径只有一条）。
import QtQuick
import QtQuick.Layouts
import "../../components"         // AppChip / AppCard
import "../../shell"              // DeviceCard
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

ColumnLayout {
    id: sec

    property var devices: []           // 本班设备（未按 chip 过滤）
    property var stats: ({ total: 0, on: 0, off: 0, pending: "—" })
    property bool portrait: false
    property string filterMode: "all"
    property string openUid: ""

    signal filterChanged(string mode)
    signal deviceActivated(string uid)

    spacing: Theme.spacing.md

    /** 在 devices 之上按 chip 再筛一层（与集控页同名同义，但那份含其它班）。 */
    function shownDevices() {
        if (sec.filterMode === "all") return sec.devices
        var out = []
        for (var i = 0; i < sec.devices.length; ++i) {
            const on = sec.devices[i].online !== false
            if ((sec.filterMode === "online") === on) out.push(sec.devices[i])
        }
        return out
    }

    /** 列数随内容区宽度与朝向（窄窗不再挤成 4 列 100px 卡片）。 */
    function gridColumns(w) {
        if (sec.portrait) return w >= 560 ? 2 : 1
        if (w >= 900) return 4
        if (w >= 660) return 3
        return 2
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing.md

        Repeater {
            model: [
                { k: "在线设备",   v: String(sec.stats.on),        sub: "共 " + sec.stats.total + " 台" },
                { k: "待执行指令", v: String(sec.stats.pending),   sub: "云端队列" },
                { k: "离线设备",   v: String(sec.stats.off),       sub: "需排查" }
            ]

            delegate: AppCard {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                // 原 variant: "elevated" ⇒ AppCard 默认 shadow: true（极柔阴影）

                ColumnLayout {
                    // ⚠️ AppCard 内部已是 ColumnLayout：子项不能 anchors.fill，必须走 Layout
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacing.xs

                    Text {
                        text: modelData.k
                        font.pixelSize: 12
                        color: Theme.color.textSecondary
                    }
                    Text {
                        text: modelData.v
                        font.pixelSize: 20
                        font.weight: Font.Medium
                        color: Theme.color.textPrimary
                    }
                    Text {
                        text: modelData.sub
                        font.pixelSize: 12
                        color: Theme.color.textSecondary
                    }
                }
            }
        }
    }

    // ── S5 筛选 chip ──
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacing.sm

        Repeater {
            model: [
                { k: "all",     t: "全部" },
                { k: "online",  t: "在线" },
                { k: "offline", t: "离线" }
            ]

            delegate: AppChip {
                required property var modelData
                text: modelData.t
                selected: sec.filterMode === modelData.k
                onClicked: sec.filterChanged(modelData.k)
            }
        }

        Item { Layout.fillWidth: true }

        Text {
            text: sec.shownDevices().length + " 台"
            font.pixelSize: 12
            color: Theme.color.textSecondary
        }
    }

    // ── S7 设备网格 ──
    GridLayout {
        Layout.fillWidth: true
        columns: sec.gridColumns(sec.width)
        columnSpacing: Theme.spacing.md
        rowSpacing: Theme.spacing.md

        Repeater {
            model: sec.shownDevices()

            delegate: DeviceCard {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 168
                uid: modelData.uid || ""
                deviceName: modelData.name || ""
                online: modelData.online !== false
                version: modelData.version || ""
                lastSeenAgoSec: (modelData.lastSeenAgoSec === undefined) ? -1 : modelData.lastSeenAgoSec
                selected: sec.openUid === (modelData.uid || "")
                onClicked: sec.deviceActivated(modelData.uid || "")
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: sec.shownDevices().length === 0
        text: sec.filterMode === "all"
              ? "这个班还没有设备"
              : ("当前筛选（" + (sec.filterMode === "online" ? "在线" : "离线") + "）下没有设备")
        font.pixelSize: 12
        color: Theme.color.textSecondary
    }
}
