// MD3 顶部导航栏（TopBar）· 高 64px · V1.0 P1 骨架层
//
// 规格（用户点单）：包含标题「设备监控」、班级切换下拉框、用户头像。
//
// ── 班级下拉为什么用原生 Menu ────────────────────────────────────────
// 项目纪律是**不用 Qt 的 Button / TextField**（自有 Md3Btn / Md3TextField 顶上），
// 但项目里**没有自带下拉框组件**；QtQuick.Controls 的 Menu / MenuItem 是
// 原生自带控件，直接用它，不手写弹出层 —— 少一份要维护的浮层逻辑。
//
// ── 属性注入 vs 直接读 backend ─────────────────────────────────────
// classes / currentClass **两者都支持**：宿主注入优先，没注入就回落
// backend.classes / backend.currentClass —— 真源始终只有一份（云端班级目录）。
// ⚠️ 这里踩过一次：宿主传了 TopBar 上不存在的属性名 ⇒
//    `Type Md3Shell unavailable`（时名，现名 MainWindow）⇒ 整个 Main.qml 加载失败、程序起不来，
//    而 qmllint 报 0 Error —— 静态检查查不出这类问题，只能真跑。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/md3"
import "../island"          // SvgIcon
import "../components/theme"   // Md3 令牌（pragma Singleton）

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
            // cc 为空 = 全部班级 ⇒ 不过滤
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

        Column {
            spacing: 2
            Text {
                text: root.title
                font.pixelSize: Md3.titleM
                font.weight: Font.Medium
                color: Md3.cOnSurface
            }
            Text {
                text: root.headCount
                font.pixelSize: Md3.bodyS
                color: Md3.cOnSurfaceVar
            }
        }

        Item { Layout.fillWidth: true }

        // ── 班级切换下拉框（原生 Menu）──────────────────────────────
        Md3Btn {
            id: classBtn
            text: root.currentClassName + " 切换"
            variant: "outlined"
            radius: 12
            onClicked: classMenu.open()
        }
        Menu {
            id: classMenu
            y: classBtn.height
            Repeater {
                model: root._classes
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name || modelData.code
                    onTriggered: root.classPicked(modelData.code)
                }
            }
        }

        // ── H8：未登录态暴露「登录」入口（骨架页没有旧壳那颗登录态头像下拉）──
        Md3Btn {
            visible: !root.loggedIn
            text: "登录"
            variant: "filled"
            radius: 12
            onClicked: root.loginRequested()
        }

        // ── 用户头像（圆 32，仅登录后显示）────────────────────────────
        // 无边框原则（AGENTS.md §9.4）：实心 secondaryContainer 圆已足够辨识，
        // 2026-10-22 去掉原先 1.5px outline 描边。
        Rectangle {
            visible: root.loggedIn
            width: 32
            height: 32
            radius: 16
            color: Md3.cSecondaryCont
            Text {
                anchors.centerIn: parent
                text: root.avatarInitial
                font.pixelSize: Md3.body13
                font.weight: Font.Medium
                color: Md3.cOnSecondaryCont
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.avatarClicked()
            }
        }
    }

        // 底部发丝分隔线（顶栏与内容区之间，避免顶栏"贴"在内容上）
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Md3.cOutlineVar
            opacity: 0.5
        }

    /** 头像首字：优先 backend 的账号显示名，没有就用占位符兜底。 */
    readonly property string avatarInitial: {
        const n = (backend && (backend.accountName || backend.userName))
                  ? String(backend.accountName || backend.userName).trim() : ""
        return n !== "" ? n.charAt(0) : "◍"
    }
}
