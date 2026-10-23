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

Item {
    id: nav

    property var md3: ({})
    property var ctx: null
    property var classes: []          // backend.classes [{code,name}]
    property var devices: []          // backend.devices（算每班在线数）
    property string currentClass: ""  // backend.currentClass
    property string section: ""       // 系统组选中：dash / classes / control / monitor / settings
    /** 系统组菜单源：外壳的 navItems（{key,icon,label}），与 BottomNav 同一份。 */
    property var navItems: []

    signal classPicked(string code)
    signal systemActivated(string key)
    signal classicRequested()

    readonly property int navWidth: 240
    readonly property int padX: md3 ? md3.spM : 16

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

    Rectangle { anchors.fill: parent; color: md3 ? md3.cSurfaceVar : "#E7E0EC" }

    // 右缘发丝线（与主内容区分界）
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: md3 ? md3.cOutlineVar : "#CAC4D0"
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
                font.pixelSize: md3 ? md3.titleM : 16
                font.weight: Font.Medium
                color: md3 ? md3.cOnSurfaceVar : "#49454F"
            }
        }

        // ── 班级分组 ──
        Text {
            Layout.leftMargin: nav.padX
            Layout.topMargin: 8
            text: "班级"
            font.pixelSize: md3 ? md3.labelS : 11
            color: md3 ? md3.cOutline : "#79747E"
        }

        // 班级空态放在 ListView **之前**：fillHeight 的列表会把后置文案推到页面
        // 中间，看起来像"空态飘在半空"，实际应紧贴「班级」标题。
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: nav.padX
            visible: classList.count === 0
            text: "还没有班级名单\n登录后由云端下发"
            font.pixelSize: md3 ? md3.bodyS : 12
            wrapMode: Text.WordWrap
            color: md3 ? md3.cOutline : "#79747E"
        }

        ListView {
            id: classList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: nav.classes
            boundsBehavior: Flickable.StopAtBounds

            delegate: ClassNavClassRow {
                required property var modelData
                width: classList.width
                md3: nav.md3
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
            color: md3 ? md3.cOutlineVar : "#CAC4D0"
            opacity: 0.6
        }

        Repeater {
            model: nav.sysItems
            delegate: ClassNavSysRow {
                required property var modelData
                Layout.fillWidth: true
                md3: nav.md3
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
                font.pixelSize: md3 ? md3.bodyM : 14
                color: md3 ? md3.cOnSurfaceVar : "#49454F"
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: nav.classicRequested()
            }
        }
    }
}
