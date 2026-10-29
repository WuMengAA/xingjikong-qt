// 星集控被控端 · 首次启动引导（OOBE Wizard）界面壳（2026-10-20）
//
// 结构：StackView 单向五步（欢迎 → **选用途** → 选方式 → 扫码/输码 → 确认）。
//   2026-10-25：用途成为第一步 —— 用户裁决「独立界面要在 OOBE 阶段完成」。
//   它决定后面绑的是班级还是广播站，也决定这台机器开哪套外壳。
//   · 单向：只能往前走 + 从「选方式」退回欢迎；不能跳到后面的步骤。
//   · 状态真源在 C++（`oobe` = OobeWizard）：bindingState 变了这里才动页面，
//     QML 自己不猜绑定结果（扫码页/输码页共用同一处跳转判断）。
//   · 失败一律弹 OobeErrorDialog（薄包装共享 AppDialog，友好文案来自 C++），**绝不白屏/不动死**。
//
// ⚠️ 命名纪律：令牌实例 id 用 `tok`（`md3` 与子组件属性同名会自引用，项目已踩过）。
// ⚠️ 依赖：QtQuick.Controls 的 StackView。被控端自 0.6.46 起由 windeployqt 部署
//    QtQuick/Controls + Qt6QuickControls2.dll（见 CMakeLists 的模块完整性断言）。
import QtQuick
import QtQuick.Controls
import "../components"
import "../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一）
import "steps"

Rectangle {
    id: wizardRoot
    color: Theme.color.surface

    // OOBE 向导固定浅色（首启视觉基线）：2026-10-30 起只对 `Theme` 生效。
    // 用 Binding + RestoreBindingOrValue 临时覆盖全局 dark，向导销毁时自动恢复原值，
    // 不破坏主外壳 AgentShell 的 dark 绑定。
    Binding {
        target: Theme; property: "dark"; value: false
        restoreMode: Binding.RestoreBindingOrValue
    }

    // 同样要「不要品牌紫」（2026-10-30 用户点单）。这条**不能只写在 AgentShell 里**：
    // OOBE 是独立窗口/独立 QML 文档，AgentShell 那时还没加载 —— 漏了这里，
    // 向导的步骤点、选中卡、主按钮全是品牌紫（截图实证：底部进度点就是紫的）。
    Binding {
        target: Theme; property: "neutralAccent"; value: true
        restoreMode: Binding.RestoreBindingOrValue
    }

    // 状态常量（与 OobeWizard::BindingState 对齐；写死数值是为了 QML 侧不依赖类型注册）
    readonly property int stIdle: 0
    readonly property int stWaiting: 1
    readonly property int stSuccess: 2
    readonly property int stFailed: 3

    property int step: 0
    // 错误提示是否正在显示（对外可观察：探针 / e2e 用它判断有没有弹出来）
    readonly property bool errorDialogOpen: errDlg.opened

    // ── 导航（供各步按钮与探针调用）─────────────────────────────
    // 步序：0 欢迎 | 1 用途 | 2 选方式 | 3 扫码 | 4 输码 | 5 完成
    function gotoStep(n) {
        var comp = n === 0 ? welcomeComp
                 : n === 1 ? roleComp
                 : n === 2 ? chooseComp
                 : n === 3 ? scanComp
                 : n === 4 ? codeComp
                 : doneComp;
        step = n;
        stack.replace(null, comp, { wizard: wizardRoot });
    }
    function chooseQr()   { gotoStep(3); oobe.startScan(); }
    function chooseCode() { gotoStep(4); }
    /** 用途是否为广播机（各步文案据此切换：班级 / 广播站）。 */
    readonly property bool isBroadcast: oobe.deviceRole === "broadcast"
    function skipNow()    { oobe.skipWizard(); }
    function completeNow(){ oobe.finishWizard(); }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: Item { }

        pushEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "x"; from: 24; to: 0; duration: Theme.motion.normal; easing.type: Easing.OutCubic }
        }
        pushExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.fast }
        }
        popEnter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.normal }
        }
        popExit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.fast }
        }
    }

    // ── 步骤指示点（4 步；纯装饰，不提供跳转入口）────────────────
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32
        spacing: 8
        visible: wizardRoot.step < 5
        Repeater {
            model: 5
            delegate: Rectangle {
                required property int index
                width: index === wizardRoot.step ? 20 : 8
                height: 8
                radius: 4
                color: index <= wizardRoot.step ? Theme.accentStrong : Theme.color.surfaceVariant
                Behavior on width { NumberAnimation { duration: Theme.motion.fast } }
            }
        }
    }

    // ── C++ 状态 → 页面跳转 / 错误提示 ──────────────────────────
    Connections {
        target: oobe
        function onBindingStateChanged() {
            if (oobe.bindingState === wizardRoot.stSuccess && wizardRoot.step !== 5) {
                wizardRoot.gotoStep(5);
            }
        }
        // 错误提示的开关**只以 errorText 为准**（C++ 是唯一真源）：
        //   · 有文案 ⇒ 弹；文案被清掉（用户点「我知道了」，或别处把状态复位）⇒ 收。
        // 为什么不在 bindingStateChanged 里开：那样「别处清错」时对话框会挂在屏幕上关不掉
        //（探针实测：直接调 dismissError 后对话框仍显示）。
        function onErrorTextChanged() {
            if (oobe.errorText.length > 0) errDlg.opened = true;
            else errDlg.opened = false;
        }
    }

    // 名字必须与共享库的 AppDialog 区分开：本件只是「一句话 + 一个按钮」的薄包装
    OobeErrorDialog {
        id: errDlg
        objectName: "oobeErrorDialog"   // 探针按名字找它（验证失败时对话框真的弹出来）
        anchors.fill: parent
        text: oobe.errorText
        onDismissed: oobe.dismissError()
    }

    Component { id: welcomeComp; StepWelcome  { } }
    Component { id: roleComp;    StepRole     { } }
    Component { id: chooseComp;  StepChooseMethod { } }
    Component { id: scanComp;    StepScan     { } }
    Component { id: codeComp;    StepCode     { } }
    Component { id: doneComp;    StepDone     { } }

    Component.onCompleted: gotoStep(0)
}
