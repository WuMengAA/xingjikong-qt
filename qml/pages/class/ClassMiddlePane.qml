// 班级工作台 · 中栏（设备网格 ↔ 班级展板 ↔ 学生 ↔ 成员）· #472 拆件
//
// 从 ClassDashboard 抽出来的原因：横屏三栏和竖屏竖排都要这一块，
// 原来两处各写一遍（分段按钮 model + StackLayout + 两个子件），
// 一处改字段另一处忘记改就是"竖屏和横屏不一样"的经典 bug。
//
// 本件只管中栏的**视图切换**，不持有任何数据：
//   · 设备/事件数据从 deviceModel / evModel（ClassDeviceModel / ClassEventModel）进来；
//   · 选中态、分段态都由宿主传，事件原样抛回。
//
// 2026-10-25 两处修：
//   ① 四段文字自然宽 ≈300，窄中栏（240~290）会把「展板/成员」段裁到可视区外 ⇒
//      用户报「展板按钮按了没反应」（本质是按钮在可视区外，不是信号没接）。
//      现在宽度 < 320 自动切**纯图标段**（Md3SegmentedButton 已支持：label="" 定宽 44，
//      四段 4×44+3×4=188 < 240），tooltip 仍带完整说明 ⇒ 每一段都在可视区、都能点。
//   ② 展板从「说明卡」换成真视图 ⇒ 需要把 evModel / 班名 / 展开态透传下去。
import QtQuick
import QtQuick.Layouts
import "../../components"         // AppSegmentedButton
import "../../island"             // （SvgIcon 由子件自带，这里不直接用）

Item {
    id: pane

    property var deviceModel: null
    property var evModel: null
    property bool subscribed: false
    property string selectedUid: ""
    /** 当前段：devices / students / board / members（宿主持有，切段不改数据）。 */
    property string tab: "devices"
    /** members 段用：换班目标名单（backend.classes）。 */
    property var classes: []
    /** members 段用：当前班码。 */
    property string classCode: ""
    /** 展板用：当前班显示名（顶栏"班级展板 · 高二(3)班"）。 */
    property string className: ""
    /** 展板用：宿主是否已进入页内全屏展示。 */
    property bool boardExpanded: false
    /** students 段用：外壳页级 toast（写操作成功/失败都要说话）。 */
    property var toastFn: null

    signal deviceClicked(string uid)
    signal tabActivated(string tab)
    /** 展板请求宿主切换页内全屏（由 ClassDashboard 收口，横竖屏同一个入口）。 */
    signal boardExpandToggled()
    /** 展板「发布到展板」→ 宿主打开页级发布弹窗（真发请求，不做装饰按钮）。 */
    signal publishRequested()
    /** 学生行「编辑」→ 宿主打开页级编辑弹窗（弹窗要 420px 宽，塞不进中栏）。 */
    signal editStudentRequested(var student)

    /** 窄栏放不下四段文字 → 纯图标段（见文件头 ①）。 */
    readonly property bool compactSeg: pane.width < 320

    // 分段定义只写一份（横竖屏共用）。窄栏用纯图标（label=""），宽栏带文字。
    readonly property var segModel: pane.compactSeg ? [
        { value: "devices",    label: "", icon: "layout-grid",   tip: "设备 · 本班设备实时画面" },
        { value: "students",   label: "", icon: "users",         tip: "学生 · 本班学生名单（存云端）" },
        { value: "attendance", label: "", icon: "check",         tip: "考勤 · 本班学生出勤点名（按天）" },
        { value: "board",      label: "", icon: "tv",            tip: "展板 · 教室大屏常亮视图" },
        { value: "timetable",  label: "", icon: "clock",         tip: "课表 · 本班周课表与作息" },
        { value: "members",    label: "", icon: "circle-check",  tip: "成员 · 老师/助教账号与设备去留" }
    ] : [
        { value: "devices",    label: "设备", icon: "layout-grid",   tip: "本班设备实时画面" },
        { value: "students",   label: "学生", icon: "users",         tip: "本班学生名单（存云端）" },
        { value: "attendance", label: "考勤", icon: "check",         tip: "本班学生出勤点名（按天）" },
        { value: "board",      label: "展板", icon: "tv",            tip: "教室大屏常亮视图" },
        { value: "timetable",  label: "课表", icon: "clock",         tip: "本班周课表与作息（可导入 ClassIsland 档案）" },
        { value: "members",    label: "成员", icon: "circle-check",  tip: "老师/助教账号与设备去留" }
    ]

    Column {
        anchors.fill: parent
        spacing: 12

        // 分段条：窄栏会自动切纯图标（见 compactSeg），仍放进 Flickable 兜底 ——
        // 万一未来再加段，至少还能横向滑，不会溢出压住邻栏。
        Flickable {
            width: parent.width
            height: 32
            contentWidth: seg.implicitWidth
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            AppSegmentedButton {
                id: seg
                // Flickable 不按 implicitWidth 布局子项：显式给宽，否则所有段叠在 0 宽处
                width: implicitWidth
                x: Math.max(0, (parent.width - width) / 2)
                model: pane.segModel
                current: pane.tab
                onActivated: function (v) { pane.tabActivated(v) }
            }
        }

        StackLayout {
            width: parent.width
            height: parent.height - 32 - 12
            currentIndex: pane.tab === "students" ? 1
                          : (pane.tab === "board" ? 2
                          : (pane.tab === "members" ? 3
                          : (pane.tab === "timetable" ? 4
                          : (pane.tab === "attendance" ? 5 : 0))))

            ClassDeviceGrid {
                model: pane.deviceModel
                subscribed: pane.subscribed
                selectedUid: pane.selectedUid
                onDeviceClicked: function (uid) { pane.deviceClicked(uid) }
            }
            ClassStudentPane {
                classCode: pane.classCode
                subscribed: pane.subscribed
                toastFn: pane.toastFn
                onEditStudentRequested: function (st) { pane.editStudentRequested(st) }
            }
            ClassBoardPane {
                deviceModel: pane.deviceModel
                evModel: pane.evModel
                subscribed: pane.subscribed
                className: pane.className
                classCode: pane.classCode
                expanded: pane.boardExpanded
                onToggleExpanded: pane.boardExpandToggled()
                onPublishRequested: pane.publishRequested()
            }
            ClassMemberPane {
                deviceModel: pane.deviceModel
                classes: pane.classes
                classCode: pane.classCode
                subscribed: pane.subscribed
            }
            // 索引 4 = 课表（见上面 currentIndex 映射）；声明顺序 = StackLayout 序号，
            // 所以课表放在成员之后（顺序错了会"点课表弹出成员页"）。
            ClassTimetablePane {
                classCode: pane.classCode
                className: pane.className
                subscribed: pane.subscribed
                toastFn: pane.toastFn
            }
            // 索引 5 = 考勤（声明顺序 = StackLayout 序号，排在课表后）。
            ClassAttendancePane {
                classCode: pane.classCode
                subscribed: pane.subscribed
                toastFn: pane.toastFn
            }
        }
    }
}
