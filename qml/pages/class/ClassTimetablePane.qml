// 班级工作台 · 中栏「课表」段（2026-10-25）
//
// 为什么要有：课表后端链路（云端 class_timetable + ClassAdmin.timetable/saveTimetable）
// 早就通了，但**界面一直没有** —— 老师存不进、也看不到。CIMS 退役后课表回落到我们云端，
// 这一件把"看得见、导得进"补齐。
//
// 数据形状（云端归一化信封，见 Stelarith-cloud/src/class-timetable.js）：
//   { schema:"classisland/v1", group:{id,name}, days:[{dow,name,timeLayoutId,
//     lessons:[{index,subject,initial,teacher,start,end}]}], layouts:[{id,name,items}] }
//
// 导入语义（刻意的）：先预览群、再由人点选（ClassTimetableToolbar 的选群条）。
// **完整档案含 12 个班**（72 个 ClassPlan = 12 群 × 6 天），且 12 个群全叫「新课表群」；
// 云端按「ClassPlans 首次出现顺序」回 label（1班…12班）+ inferred，界面据此提示核对。
// 猜错=挂错班，所以：群不猜、名字不可信时明说不可信。
import QtQuick
import "../../components/fresh"  // FreshCard
import "../../components/theme"  // Md3 令牌（pragma Singleton）
import "../../island"            // SvgIcon

Item {
    id: pane

    property string classCode: ""
    property string className: ""
    property bool subscribed: false
    property var toastFn: null

    property var profile: null      // 归一化课表（null = 还没导入过）
    property var groups: []         // 待选课表群
    property string rawText: ""     // 最近一次选中的档案原文（选群时复用）
    property int dayIdx: 0

    /** 待选群里班名是"按顺序推的"（12 群同名）⇒ 选群条必须提示核对。 */
    readonly property bool groupsInferred: pane.groups.length > 1
                                          && pane.groups[0] !== undefined
                                          && pane.groups[0].inferred === true

    readonly property var days: (pane.profile && pane.profile.days) ? pane.profile.days : []
    readonly property var curDay: (pane.dayIdx >= 0 && pane.dayIdx < pane.days.length)
                                  ? pane.days[pane.dayIdx] : null
    readonly property var curLayout: {
        if (!pane.curDay || !pane.profile) return null
        const ls = pane.profile.layouts || []
        for (var i = 0; i < ls.length; ++i)
            if (ls[i].id === pane.curDay.timeLayoutId) return ls[i]
        return ls.length > 0 ? ls[0] : null
    }

    function toast(msg, isErr) { if (pane.toastFn) pane.toastFn(msg, isErr === true) }
    function admin() {
        return (typeof backend !== "undefined" && backend && backend.classAdmin) ? backend.classAdmin : null
    }

    function reload() {
        const a = pane.admin()
        if (!a || pane.classCode === "") return
        a.timetable(pane.classCode)
    }

    /** 默认停在"今天"（周日=0，周一=1 …）；今天没课就停第一天。 */
    function pickToday(p) {
        if (!p || !p.days || p.days.length === 0) return 0
        const dow = new Date().getDay()
        for (var i = 0; i < p.days.length; ++i)
            if (p.days[i].dow === dow) return i
        return 0
    }

    /** 选档案 → 预览群（不落库）。真正写入在 doImport。 */
    function pickProfile() {
        const a = pane.admin()
        if (!a || typeof backend === "undefined" || !backend) return
        const path = backend.pickFile()
        if (!path) return                       // 取消选择：不报错
        const text = a.readTextFile(path, 4 * 1024 * 1024)
        if (!text) { pane.toast(a.lastFileError() || "读不到文件内容", true); return }
        // 2026-10-29：档案校验收归 C++（ClassAdmin::isClassIslandArchive），
        // QML 不再 JSON.parse —— §10.9 QML 纯净封杀令。
        if (!a.isClassIslandArchive(text)) {
            pane.toast("这不是 ClassIsland 档案（缺 ClassPlans / TimeLayouts）", true)
            return
        }
        pane.rawText = text
        pane.groups = []
        a.timetableGroups(text)                 // 云端回 timetableGroupsList
    }

    function doImport(groupId) {
        const a = pane.admin()
        if (!a || pane.rawText === "" || pane.classCode === "") return
        a.importTimetable(pane.classCode, pane.rawText, groupId || "")
    }

    // 换班：清掉上一班的课表，别让老师看到别班内容
    onClassCodeChanged: {
        pane.profile = null; pane.groups = []; pane.rawText = ""; pane.dayIdx = 0
        pane.reload()
    }

    Connections {
        target: pane.admin()
        ignoreUnknownSignals: true
        function onTimetableLoaded(code, profile, updatedAt) {
            if (code !== pane.classCode) return
            // 2026-10-29：profile 已是 C++ 下发的 QVariantMap/QVariantList，
            // QML 直接消费，不再 JSON.parse —— §10.9 QML 纯净封杀令。
            const p = (profile === undefined || profile === null) ? null : profile
            pane.profile = p
            if (!p) return                      // 还没排课：留空态，dayIdx 不动
            pane.dayIdx = pane.pickToday(p)
        }
        function onTimetableGroupsList(list) {
            pane.groups = list || []
            if (pane.groups.length === 1) pane.doImport(pane.groups[0].id)   // 单群：不打断
        }
        function onOpFinished(op, ok, message) {
            if (op === "timetable-import") {
                pane.toast(message, !ok)
                if (ok) { pane.groups = []; pane.reload() }
            } else if (op === "timetable-groups" && !ok) {
                pane.toast(message, true)
            }
        }
    }

    Column {
        anchors.fill: parent
        spacing: 10

        ClassTimetableToolbar {
            id: toolBar
            subtitle: pane.profile
                      ? (((pane.profile.group || {}).label
                          || (pane.profile.group || {}).name || "") + " · " + pane.days.length + " 天")
                      : "还没导入"
            canImport: pane.subscribed && pane.classCode !== ""
            groups: pane.groups
            nameInferred: pane.groupsInferred
            onImportRequested: pane.pickProfile()
            onRefreshRequested: pane.reload()
            onGroupPicked: function (id) { pane.doImport(id) }
        }

        FreshCard {
            width: parent.width
            height: Math.max(120, parent.height - toolBar.height - 10)

            // 空态：说清"这里会显示什么、怎么让它有内容"（ClassTimetableEmpty）
            ClassTimetableEmpty {
                visible: !pane.profile
                subscribed: pane.subscribed
            }

            Column {
                anchors.fill: parent
                spacing: 8
                visible: !!pane.profile

                Row {
                    id: dayRow
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: pane.days
                        Rectangle {
                            width: Math.max(56, (dayRow.width - (pane.days.length - 1) * 6)
                                                / Math.max(1, pane.days.length))
                            height: 28
                            radius: Md3.rS
                            color: index === pane.dayIdx ? (Md3.frGreenSoft || "#E9FBF0") : "transparent"
                            border.width: index === pane.dayIdx ? 1 : 0
                            border.color: Md3.frGreen || "#22C55E"
                            Text {
                                anchors.centerIn: parent
                                text: (modelData.name || "") + " " + ((modelData.lessons || []).length)
                                font.pixelSize: Md3.body13
                                color: index === pane.dayIdx ? (Md3.frGreenD || "#16A34A")
                                                             : (Md3.frInk2 || "#6B7280")
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: pane.dayIdx = index
                            }
                        }
                    }
                }

                ClassTimetableDayView {
                    width: parent.width
                    height: Math.max(60, parent.height - 36)
                    day: pane.curDay
                    layout: pane.curLayout
                }
            }
        }
    }

    Component.onCompleted: pane.reload()
}
