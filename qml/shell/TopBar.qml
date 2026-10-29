// 顶部导航栏（TopBar）· 高 64px · QtShadcn 规范（2026-10-29 迁移）
//
// 规格（用户点单）：包含标题「设备监控」、班级切换下拉框、用户头像。
//
// ── 班级下拉为什么用原生 Menu ────────────────────────────────────────
// 项目纪律是不裸用 Qt 的 Button / TextField（自有 AppButton / AppInput 顶上），
// 但项目里没有自带下拉框组件；QtQuick.Controls 的 Menu / MenuItem 是原生自带控件，
// 直接用它，不手写弹出层 —— 少一份要维护的浮层逻辑。
//
// ── 属性注入 vs 直接读 backend ─────────────────────────────────────
// classes / currentClass 两者都支持：宿主注入优先，没注入就回落 backend。
// ⚠️ 这里踩过一次：宿主传了 TopBar 上不存在的属性名 ⇒ 整个 Main.qml 加载失败、
//    程序起不来，而 qmllint 报 0 Error —— 静态检查查不出，只能真跑。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · `Md3.*` → `Theme.*`；字号走规范 12/13/14/16
// · `Md3Btn outlined/filled` → `AppButton secondary/primary`
// · **不给 radius 覆写**（C 案：12px 例外只属 ActionBar / MonitorPage），回落全局 md
// · **删掉底部 1px 发丝分隔线**：规范要求「区域划分完全靠背景色差」，
//   发丝线虽是 Rectangle 不是 border.*，仍属靠线条分区，故按规范移除
// · `Column` → `ColumnLayout`（规范只许 Layout 系列）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"           // AppButton
import "../island"              // SvgIcon
import "../components/theme"    // Theme 令牌
import "ClassSort.js" as ClassSort

Item {
    id: root

    /** 宿主上下文（与本端 pages/ 下各页同一份 _pageCtx 契约）。 */
    property var ctx: null
    /** 顶部标题（宿主可覆写，默认「设备监控」）。 */
    property string title: "设备监控"
    /** 班级列表：宿主注入；空则回落到 backend.classes。 */
    property var classes: []
    /** 当前班：宿主注入；空则回落到 backend.currentClass。 */
    property string currentClass: ""

    /** 实际使用的班级表（注入优先）。 */
    readonly property var _classes: (root.classes && root.classes.length)
                                   ? root.classes
                                   : ((backend && backend.classes) ? backend.classes : [])
    /** 下拉里的显示顺序：1班 < 2班 < … < 10班 < 12班（规则见 ClassSort.js）。 */
    readonly property var _classesSorted: ClassSort.byName(root._classes)
    /** 实际使用的当前班（注入优先）。 */
    readonly property string _cur: (root.currentClass !== "")
                                   ? root.currentClass
                                   : (backend.currentClass || "")

    /** 当前班级名（下拉框按钮文案）—— 派生，不缓存。 */
    readonly property string currentClassName: {
        const cs = root._classes
        const cc = root._cur
        for (var i = 0; i < cs.length; ++i)
            if (cs[i].code === cc) return cs[i].name || cs[i].code
        return cc === "" ? "全部班级" : cc
    }
    /** 本班在线 / 总数（现算）。 */
    readonly property string headCount: {
        const ds = (backend && backend.devices) ? backend.devices : []
        const cc = root._cur
        var total = 0, on = 0
        for (var i = 0; i < ds.length; ++i) {
            if (!ds[i]) continue
            if (cc !== "" && (ds[i].classCode || "") !== cc) continue
            total++
            if (ds[i].online !== false) on++
        }
        return total + " 台 · " + on + " 台在线"
    }

    signal classPicked(string code)
    signal avatarClicked()

    /** 是否已登录（未登录时暴露「登录」入口，H8）。 */
    readonly property bool loggedIn: (backend && backend.loggedIn === true)
    /** 点了「登录」按钮（H8）。 */
    signal loginRequested()

    Layout.fillWidth: true
    Layout.preferredHeight: 64

    RowLayout {
        anchors.fill: parent
        spacing: 12

        ColumnLayout {
            spacing: 2
            Text {
                text: root.title
                font.pixelSize: 16
                font.weight: Font.Medium
                color: Theme.color.textPrimary
            }
            Text {
                text: root.headCount
                font.pixelSize: 13
                color: Theme.color.textSecondary
            }
        }

        Item { Layout.fillWidth: true }

        // ── 班级切换下拉框（原生 Menu）──────────────────────────────
        AppButton {
            id: classBtn
            text: root.currentClassName + " 切换"
            variant: "secondary"
            onClicked: classMenu.open()
        }
        Menu {
            id: classMenu
            y: classBtn.height
            Repeater {
                model: root._classesSorted
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name || modelData.code
                    onTriggered: root.classPicked(modelData.code)
                }
            }
        }

        // ── H8：未登录态暴露「登录」入口 ──────────────────────────────
        AppButton {
            visible: !root.loggedIn
            text: "登录"
            variant: "primary"
            onClicked: root.loginRequested()
        }

        // ── 用户头像（圆 32，仅登录后显示）────────────────────────────
        // 实心 surfaceVariant 圆已足够辨识（无边框原则，2026-10-22 去描边）
        Rectangle {
            visible: root.loggedIn
            width: 32
            height: 32
            radius: 16            // 正圆（非卡片圆角令牌）
            color: Theme.color.surfaceVariant
            Text {
                anchors.centerIn: parent
                text: root.avatarInitial
                font.pixelSize: 13
                font.weight: Font.Medium
                color: Theme.color.textPrimary
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.avatarClicked()
            }
        }
    }

    /** 头像首字：优先 backend 的账号显示名，没有就用占位符兜底。 */
    readonly property string avatarInitial: {
        const n = (backend && (backend.accountName || backend.userName))
                  ? String(backend.accountName || backend.userName).trim() : ""
        return n !== "" ? n.charAt(0) : "◍"
    }
}
