// 新版外壳 · 顶栏（2026-11-01 · 转正后的唯一顶栏）
//
// 转正改动（2026-11-01）：从「纯展示」换成**真功能**顶栏 —— 接住经典外壳 TopBar 的三件事：
//   ① 班级切换下拉：走 ClassSort.byName 排序（1班<2班<10班），选中发 classPicked(code)，
//      宿主接到 shell.pickClass(code)（**双写**：旧筛选 setCurrentClass + 新班级域 subscribeClass，
//      2026-10-23 #472 裁决；2026-10-28「取代」后双写收进 pickClass 这一个入口）。
//   ② 未登录态「登录」按钮（H8）：发 loginRequested()，宿主调 backendRef.loginWithSite()。
//   ③ 头像：登录后显示，点了发 avatarClicked() → 个人中心（**不**是系统设置 ——
//      system:settings 有 RBAC 门禁，老师点自己头像会被挡，2026-10-25 裁决）。
//
// 为什么用原生 Menu：项目纪律是不裸用 Qt 的 Button/TextField，但项目里没有自带下拉框组件；
// QtQuick.Controls 的 Menu/MenuItem 是原生自带控件，直接用它，不手写弹出层。
// （与退役的 shell/TopBar.qml 同一决策，2026-10-21 已登记）
//
// ⚠️ backendRef 而不是裸 backend：MainWindow 作用域内的裸 `backend` 实测恒为 null
//    （QML 非限定名先查自身属性，而 Main 用 backendRef 这个不重名转发名）。
//    本件一律用宿主注入的 backendRef。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"          // AppButton（shared-qml/ui 构建期拷入）
import "../components/theme"    // Theme 令牌
import "../island"              // SvgIcon
import "ClassSort.js" as ClassSort

Item {
    id: root

    /** 宿主上下文（与 pages/ 下各页同一份 _pageCtx 契约）。 */
    property var ctx: null
    property var backendRef: null
    /** 班级列表：宿主注入；空则回落 backendRef.classes。 */
    property var classes: []
    /** 当前班：宿主注入；空则回落 backendRef.currentClass。 */
    property string currentClass: ""
    /** 顶栏标题（随栏目变，宿主给）。 */
    property string title: ""
    /** 副标题（本班台数等，宿主给；空则不显示）。 */
    property string subtitle: ""
    property string accountAvatarText: ""

    signal classPicked(string code)
    signal avatarClicked()
    signal loginRequested()

    /** 实际使用的班级表（注入优先）。 */
    readonly property var _classes: (root.classes && root.classes.length)
                                   ? root.classes
                                   : ((root.backendRef && root.backendRef.classes) ? root.backendRef.classes : [])
    /** 下拉里的显示顺序：1班 < 2班 < … < 10班 < 12班（规则见 ClassSort.js）。 */
    readonly property var _classesSorted: ClassSort.byName(root._classes)
    /** 实际使用的当前班（注入优先）。 */
    readonly property string _cur: (root.currentClass !== "")
                                   ? root.currentClass
                                   : ((root.backendRef && root.backendRef.currentClass) || "")

    /** 当前班级名（下拉按钮文案）—— 派生，不缓存。 */
    readonly property string currentClassName: {
        const cs = root._classes
        const cc = root._cur
        for (var i = 0; i < cs.length; ++i)
            if (cs[i].code === cc) return cs[i].name || cs[i].code
        return cc === "" ? "全部班级" : cc
    }

    /** 是否已登录（未登录时暴露「登录」入口，H8）。 */
    readonly property bool loggedIn: (root.backendRef && root.backendRef.loggedIn === true)

    height: 64

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.color.accent
    }

    // ── 左：标题（栏目名 + 副标题）────────────────────────────────
    ColumnLayout {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.lg
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            visible: root.title !== ""
            text: root.title
            font.pixelSize: 16
            font.weight: Font.Medium
            color: Theme.color.textPrimary
        }
        Text {
            visible: root.subtitle !== ""
            text: root.subtitle
            font.pixelSize: 13
            color: Theme.color.textSecondary
        }
    }

    // ── 右：班级下拉 + 登录 + 头像 ───────────────────────────────
    RowLayout {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing.lg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacing.md

        // 班级切换下拉（原生 Menu；项目无自带下拉组件，2026-10-21 决议）
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
                    height: 40
                }
            }
        }

        // H8：未登录态暴露「登录」入口
        AppButton {
            visible: !root.loggedIn
            text: "登录"
            variant: "primary"
            onClicked: root.loginRequested()
        }

        // 头像（圆 36，仅登录后显示）；点了进个人中心（无 RBAC 门禁）
        Rectangle {
            id: avatar
            visible: root.loggedIn
            width: 36; height: 36; radius: Theme.radius.full
            color: Theme.color.primary
            Text {
                anchors.centerIn: parent
                text: root.accountAvatarText || "◍"
                color: Theme.color.primaryForeground
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            TapHandler { onTapped: root.avatarClicked() }
            HoverHandler { id: avatarHover }
            Behavior on color {
                ColorAnimation { duration: Theme.motion.fast }
            }
        }
    }
}
