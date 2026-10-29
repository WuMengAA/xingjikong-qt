// 班级导航（ClassNav）· 骨架左栏 · #473 点单①「左班级导航 + 右班级工作台」
//
// 与 Sidebar 的区别：Sidebar 是"功能菜单"（仪表盘/班级看板/监控/设置），
// 本件是"以班为单位"的导航——上面是可变的班级列表（backend.classes），
// 下面是固定的系统组。老师的日常入口是班级，不是功能名。
//
// 三处设计要求：
//   ① 班级项带人数（在线/总数）：一眼看出哪个班在跑，不用点进去。
//      计数从 backend.devices 按 classCode 现算，不另建缓存（云端名单是唯一口径）。
//   ② 选中态 = 工业青左规 + secondaryContainer 底（沿用 Sidebar 语言，不另造）。
//   ③ 空态说人话：没有班级时提示"登录后由云端下发"，不摆空列表装正常。
//
// 竖屏不走本件（底栏放不下可变数量的班级）——竖屏用 BottomNav 的系统组，
// 班级切换走 TopBar 的「全部班级」下拉。
import QtQuick
import QtQuick.Layouts
import "../components/theme"   // Theme 设计令牌单例（pragma Singleton）
import "ClassSort.js" as ClassSort

Item {
    id: nav

    property var ctx: null
    property var classes: []          // backend.classes [{code,name}]
    property var devices: []          // backend.devices（算每班在线数）
    property string currentClass: ""  // backend.currentClass
    property string section: ""       // 系统组选中：dash / classes / control / monitor / settings
    /** 列表是否只含「我带的班」（backend.classesFiltered）。true 时标题右侧标一行小字——
     *  登录后云端按 class_members 过滤，若不说清，老师会以为班级数据丢了。 */
    property bool classesFiltered: false
    /** 系统组菜单源：外壳的 navItems（{key,icon,label}），与 BottomNav 同一份。 */
    property var navItems: []

    signal classPicked(string code)
    signal systemActivated(string key)
    signal classicRequested()

    /** 班级显示序：1班<2班<…<12班（规则在 ClassSort.js，TopBar 共用；只排序不动身份码）。 */
    readonly property var sortedClasses: ClassSort.byName(nav.classes)

    readonly property int navWidth: 240
    readonly property int padX: Theme.spacing.md

    implicitWidth: navWidth
    Layout.fillHeight: true
    Layout.preferredWidth: navWidth

    /** 系统组（监控仍是跳整页，走宿主 navActivated 的统一分支）。 */
    /** 系统组：由外壳的 navItems 派生（**不另写一份**）。
     *  以前这里硬编码 3 项，加了「设备集控」栏目就漏了——左栏少了入口，
     *  页面却切得过去，属于"能到但找不到路"。
     *  classes 项保留：班级名单为空（云端没下发）时，它是进班级工作台的唯一入口。 */
    readonly property var sysItems: nav.navItems

    /** 某个班的在线/总数（现算；devices 是云端下发的唯一名单）。 */
    function countOf(code) {
        const ds = nav.devices
        var on = 0, all = 0
        for (var i = 0; i < ds.length; ++i) {
            const d = ds[i]
            if (!d || (d.classCode || "") !== code) continue
            all++
            if (d.online !== false) on++
        }
        return { on: on, all: all }
    }

    Rectangle { anchors.fill: parent; color: Theme.color.surfaceVariant}

    // 右缘发丝线（与主内容区分界）
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.color.textSecondary
        opacity: 0.5
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4

        // ── 品牌行 ──
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Text {
                anchors.left: parent.left
                anchors.leftMargin: nav.padX
                anchors.verticalCenter: parent.verticalCenter
                text: "星集控"
                font.pixelSize: 16
                font.weight: Font.Medium
                color: Theme.color.textSecondary
            }
        }

        // ── 班级分组 ──
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: nav.padX
            Layout.rightMargin: nav.padX
            Layout.topMargin: 8
            spacing: 6

            Text {
                text: "班级"
                font.pixelSize: 11
                color: Theme.color.textSecondary
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: nav.classesFiltered
                text: "仅我带的班"
                font.pixelSize: 11
                color: Theme.color.primary
            }
        }

        // 班级空态放在 ListView **之前**：fillHeight 的列表会把后置文案推到页面
        // 中间，看起来像"空态飘在半空"，实际应紧贴「班级」标题。
        // 2026-10-25（用户点单「班级创建繁琐，没有引导」）：原文案"登录后由云端
        // 下发"会让新手以为不用自己建班 ⇒ 改成指明入口（工作台有「上手三步」）。
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: nav.padX
            visible: classList.count === 0
            text: "还没有班级\n去「班级工作台」新建"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            color: Theme.color.textSecondary
        }

        ListView {
            id: classList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: nav.sortedClasses
            boundsBehavior: Flickable.StopAtBounds

            delegate: ClassNavClassRow {
                required property var modelData
                width: classList.width
                code: modelData.code || ""
                name: modelData.name || ""
                onCount: nav.countOf(modelData.code || "").on
                totalCount: nav.countOf(modelData.code || "").all
                active: nav.section === "classes" && nav.currentClass === (modelData.code || "")
                onClicked: nav.classPicked(modelData.code || "")
            }
        }

        // ── 系统组 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.bottomMargin: 4
            height: 1
            color: Theme.color.textSecondary
            opacity: 0.6
        }

        Repeater {
            model: nav.sysItems
            delegate: ClassNavSysRow {
                required property var modelData
                Layout.fillWidth: true
                icon: modelData.icon
                label: modelData.label
                active: nav.section === modelData.key
                onClicked: nav.systemActivated(modelData.key)
            }
        }

        // ── 返回经典界面（骨架与旧壳并存期必须有退路）──
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Text {
                anchors.left: parent.left
                anchors.leftMargin: nav.padX
                anchors.verticalCenter: parent.verticalCenter
                // lucide 无 arrow-left（原 Sidebar 那里一直画成空白），用「‹」汉字位形更稳
                text: "‹  返回经典界面"
                font.pixelSize: 14
                color: Theme.color.textSecondary
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: nav.classicRequested()
            }
        }
    }
}
