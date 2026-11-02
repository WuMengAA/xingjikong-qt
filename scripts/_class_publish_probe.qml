// 发布中心 + 课堂工具运行时走查探针（2026-10-25 · 发布中心 P1 / 教室小工具第一刀）
//
// 为什么必须跑：这批 QML 的静态检查全绿但 qmllint 对「不存在的属性/方法」0 Error
// （甚至整页崩溃也 0 Error，2026-10-22/25 两次实证）—— 只有真 QML 引擎能验。
// 本轮 0fad104 已被这条抓到一个 fatal：Main.qml 两个 actionLabel 重名 → 整页不加载。
//
// 宿主与跑法同 _class_admin_probe.qml（scripts/_build_class_admin_probe.py 会依次
// 跑两个探针）：C++ 注入假 backend（FakeAdmin 已补 publish），probe.finish(bad)。
// 验什么：
//   ① ClassPublishDialog/ClassPublishForm 编译 + 实例化（fresh/md3/island 三段 import）；
//   ② 类型四选一：段位模型 4 项、activated → form.type 联动、渲染落点提示随类型变；
//   ③ 校验：标题正文全空 → 发布禁用；键入 → 启用；清空 → 再禁用；
//   ④ 发布链：调用参数 = (班码, 类型, trim 标题, trim 正文)；发送中按钮文案/禁用 + 防连点；
//   ⑤ 回执：失败保留弹窗与内容 + 错误 toast；create 等非 publish 回执不得误动；成功才关+复位；
//   ⑥ 未订阅：canSend=false 且按钮禁用；
//   ⑦ ClassToolPane：三个音量档 / 专注开关 / 噪音开关 → 批量 id + picked 只含本班在线；
//   ⑧ 无在线设备：toast 报错且不下发；未订阅全禁用。
import QtQuick
import QtQuick.Window
import "../../Stelarith-shared-qml/ui/theme"     // Md3（浅色令牌）

Window {
    id: win
    width: 1200
    height: 900
    visible: true
    color: "#eceef1"

    Rectangle {
        anchors.fill: parent
        z: -1
        color: "#eceef1"
    }


    property var toasts: []

    function allOf(root) {
        var out = []
        function walk(o) {
            if (!o || !o.children) return
            for (var i = 0; i < o.children.length; ++i) {
                var c = o.children[i]
                out.push(c)
                walk(c)
            }
        }
        walk(root)
        return out
    }
    function btnByText(root, t) {
        var all = allOf(root)
        for (var i = 0; i < all.length; ++i)
            if (all[i].text === t && all[i].variant !== undefined) return all[i]
        return null
    }
    /** Md3TextField 内部真 TextInput（有 echoMode 且是输入型）。 */
    function textInputOf(tf) {
        var all = allOf(tf)
        for (var i = 0; i < all.length; ++i)
            if (all[i].echoMode !== undefined && all[i].selectAll !== undefined) return all[i]
        return null
    }
    /** 自绘正文框的 TextEdit。
     *  ⚠️ 不能用 wrapMode+selectAll 判：TextInput 也有这两个（2026-10-25 实测，
     *  第一版按此判把标题输入框当成了正文框，5 条断言连环误判）。
     *  TextEdit 没有 echoMode —— 用「有 selectAll 且无 echoMode」唯一确定。 */
    function textEditOf(root) {
        var all = allOf(root)
        for (var i = 0; i < all.length; ++i)
            if (all[i].wrapMode !== undefined && all[i].selectAll !== undefined
                && all[i].echoMode === undefined) return all[i]
        return null
    }
    /** ClassPublishForm 实例（typeModel 是它独有的 readonly 属性）。 */
    function formOf(root) {
        var all = allOf(root)
        for (var i = 0; i < all.length; ++i)
            if (all[i].typeModel !== undefined && all[i].reset !== undefined) return all[i]
        return null
    }

    Component.onCompleted: {
        var bad = 0
        function ck(name, cond) {
            if (cond) console.log("  ok   " + name)
            else { bad++; console.log("  FAIL " + name) }
        }

        // ── 一、发布弹窗加载 ──
        var compD = Qt.createComponent(
            "../build/qml/Stelarith/pages/class/ClassPublishDialog.qml", Component.PreferSynchronous)
        ck("ClassPublishDialog 编译 Ready（fresh/island/md3 三段 import + ClassPublishForm 解析）",
           compD.status === Component.Ready)
        if (compD.status !== Component.Ready) {
            console.log("  errorString: " + compD.errorString())
            probe.finish(99)
            return
        }

        var cs = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {'
            + ' property bool subscribed: true;'
            + ' property string classCode: "cls-fbe3c89dc4";'
            + ' property string className: "测试班" }', win)

        var dlg = compD.createObject(win, {
            "cs": cs,
            "toastFn": function (m, e) { win.toasts.push([m, e === true]) }
        })
        ck("弹窗实例化成功", dlg !== null)
        if (!dlg) { probe.finish(99); return }
        ck("context backend.classAdmin 注入生效（admin() 非空）", dlg.admin() !== null)
        ck("初始未打开", dlg.opened === false)

        dlg.open()
        ck("open() 后 opened === true", dlg.opened === true)

        var form = formOf(dlg)
        ck("找到内嵌 ClassPublishForm", form !== null)
        if (!form) { probe.finish(99); return }

        // ── 二、类型四选一 ──
        ck("类型段 4 项", form.typeModel.length === 4)
        ck("类型值白名单与云端一致",
           form.typeModel[0].value === "student-notice"
           && form.typeModel[1].value === "class-notice"
           && form.typeModel[2].value === "fullscreen-notice"
           && form.typeModel[3].value === "homework")
        ck("默认类型 class-notice", form.type === "class-notice")
        form.type = "student-notice"
        ck("student-notice 落点 = 跑马灯", form.typeHint().indexOf("跑马灯") >= 0)
        form.type = "fullscreen-notice"
        ck("fullscreen-notice 落点 = 全屏文字窗", form.typeHint().indexOf("全屏文字窗") >= 0)
        form.type = "homework"
        ck("homework 落点 = 全屏文字窗", form.typeHint().indexOf("全屏文字窗") >= 0)
        form.type = "class-notice"
        ck("切回 class-notice 落点 = 跑马灯", form.typeHint().indexOf("跑马灯") >= 0)

        // ── 三、校验（标题正文全空 → 禁用）──
        ck("全空时 canSend === false", dlg.canSend === false)
        var pubBtn = btnByText(dlg, "发布到全班")
        ck("找到「发布到全班」按钮", pubBtn !== null)
        ck("全空时发布按钮禁用", pubBtn !== null && pubBtn.enabled === false)

        var ti = textInputOf(form)
        ck("找到标题输入框", ti !== null)
        if (ti) {
            ti.text = "  晚自习安排  "
            ck("键入后 form.titleText 跟上（原样含空白）", form.titleText.indexOf("晚自习安排") >= 0)
            ck("有标题后 canSend === true", dlg.canSend === true)
            ck("发布按钮启用", pubBtn.enabled === true)
        }

        var te = textEditOf(form)
        ck("找到正文多行框（TextEdit）", te !== null)
        if (te) {
            te.text = "第 3 节改答疑"
            ck("正文键入后 form.bodyText 跟上", form.bodyText.indexOf("第 3 节改答疑") >= 0)
        }

        // ── 四、发布链（参数 + trim + 防连点）──
        var n0 = backend.classAdmin.calls.filter(function (c) {
            return c.indexOf("publish|") === 0 }).length
        pubBtn.clicked()
        var n1 = backend.classAdmin.calls.filter(function (c) {
            return c.indexOf("publish|") === 0 }).length
        ck("点击发布 → classAdmin.publish 调了 1 次", n1 === n0 + 1)
        ck("调用参数 = 班码|类型|trim 标题|trim 正文",
           backend.classAdmin.calls.indexOf("publish|cls-fbe3c89dc4|class-notice|晚自习安排|第 3 节改答疑") >= 0)
        ck("发送中 sending === true", dlg.sending === true)
        ck("发送中按钮文案 = 发布中…", btnByText(dlg, "发布中…") !== null)
        ck("发送中按钮禁用（防连点）", dlg.canSend === false)
        pubBtn.clicked()
        pubBtn.clicked()
        var n2 = backend.classAdmin.calls.filter(function (c) {
            return c.indexOf("publish|") === 0 }).length
        ck("连点两下不重复发（不新增调用）", n2 === n1)

        // ── 五、回执链路 ──
        var tw0 = win.toasts.length
        backend.classAdmin.opFinished("create", true, "别的回执")
        ck("非 publish 回执不误动弹窗（仍在发送态、仍打开）",
           dlg.opened === true && dlg.sending === true)

        backend.classAdmin.opFinished("publish", false, "权限不足")
        ck("失败：错误 toast 带云端人话",
           win.toasts.length === tw0 + 1
           && win.toasts[win.toasts.length - 1][0].indexOf("权限不足") >= 0
           && win.toasts[win.toasts.length - 1][1] === true)
        ck("失败：弹窗保持打开（可改完重试）", dlg.opened === true)
        ck("失败：标题内容保留", form.titleText.indexOf("晚自习安排") >= 0)
        ck("失败：正文内容保留", form.bodyText.indexOf("第 3 节改答疑") >= 0)
        ck("失败：sending 解锁", dlg.sending === false)

        // 改类型后重发 → 新类型进调用参数
        form.type = "homework"
        btnByText(dlg, "发布到全班").clicked()
        ck("重发带新类型 homework",
           backend.classAdmin.calls.indexOf("publish|cls-fbe3c89dc4|homework|晚自习安排|第 3 节改答疑") >= 0)

        var tw1 = win.toasts.length
        backend.classAdmin.opFinished("publish", true, "已发布（送达 2 / 在线 3）")
        ck("成功：回执原文 toast（不标错）",
           win.toasts.length === tw1 + 1
           && win.toasts[win.toasts.length - 1][0].indexOf("已发布") >= 0
           && win.toasts[win.toasts.length - 1][1] === false)
        ck("成功：弹窗关闭", dlg.opened === false)
        ck("成功：表单复位（标题空）", form.titleText === "")
        ck("成功：表单复位（正文空）", form.bodyText === "")
        ck("成功：表单复位（类型回默认）", form.type === "class-notice")

        // ── 六、未订阅态 ──
        cs.subscribed = false
        dlg.open()
        ck("未订阅：canSend === false", dlg.canSend === false)
        ck("未订阅：发布按钮禁用", btnByText(dlg, "发布到全班").enabled === false)
        form.type = "class-notice"
        var hasUnsubText = false
        var allD = allOf(dlg)
        for (var di = 0; di < allD.length; ++di)
            if (typeof allD[di].text === "string" && allD[di].text.indexOf("未订阅班级") >= 0) hasUnsubText = true
        ck("未订阅提示文案「未订阅班级」在树上", hasUnsubText)
        cs.subscribed = true
        dlg.open()
        if (ti) ti.text = "x"
        ck("恢复订阅后 canSend 跟随内容", dlg.canSend === true)
        dlg.close()
        ck("close() 后 opened === false", dlg.opened === false)

        // ── 七、课堂工具：批量下发 ──
        var compT = Qt.createComponent(
            "../build/qml/Stelarith/pages/class/ClassToolPane.qml", Component.PreferSynchronous)
        ck("ClassToolPane 编译 Ready（fresh 解析）", compT.status === Component.Ready)
        if (compT.status !== Component.Ready) {
            console.log("  errorString: " + compT.errorString())
            probe.finish(bad + 1)
            return
        }

        var host = Qt.createQmlObject(
            'import QtQuick 2.15; QtObject {'
            + ' property var picked: [];'
            + ' property var calls: [];'
            + ' property var toasts: [];'
            + ' function runActionById(id) { calls.push(id) }'
            + ' }', win)

        var devices = [
            { uid: "u-on-1",  classCode: "cls-fbe3c89dc4", online: true },
            { uid: "u-on-2",  classCode: "cls-fbe3c89dc4", online: true },
            { uid: "u-off",   classCode: "cls-fbe3c89dc4", online: false },
            { uid: "u-other", classCode: "cls-other",       online: true }
        ]
        var tp = compT.createObject(win, {
            "host": host, "cs": cs, "devices": devices,
            "toastFn": function (m, e) { host.toasts.push([m, e === true]) }
        })
        ck("课堂工具实例化成功", tp !== null)
        if (!tp) { probe.finish(bad + 1); return }

        ck("在线 uid 只含本班在线（2 台）",
           JSON.stringify(tp.classOnlineUids()) === '["u-on-1","u-on-2"]')

        var volCases = [["静音", "pick-volume-mute"], ["30%", "pick-volume-low"], ["80%", "pick-volume-high"]]
        for (var vi = 0; vi < volCases.length; ++vi) {
            var b = btnByText(tp, volCases[vi][0])
            ck("找到音量按钮「" + volCases[vi][0] + "」", b !== null)
            if (b) {
                host.calls = []
                b.clicked()
                ck("「" + volCases[vi][0] + "」→ runActionById(" + volCases[vi][1] + ")",
                   host.calls.length === 1 && host.calls[0] === volCases[vi][1])
                ck("「" + volCases[vi][0] + "」→ picked = 本班 2 台在线",
                   JSON.stringify(host.picked) === '["u-on-1","u-on-2"]')
            }
        }

        var pairCases = [["开启专注", "pick-study-on"], ["结束专注", "pick-study-off"],
                         ["开启监测", "pick-noise-on"], ["结束监测", "pick-noise-off"]]
        for (var pi = 0; pi < pairCases.length; ++pi) {
            var pb = btnByText(tp, pairCases[pi][0])
            ck("找到按钮「" + pairCases[pi][0] + "」", pb !== null)
            if (pb) {
                host.calls = []
                pb.clicked()
                ck("「" + pairCases[pi][0] + "」→ " + pairCases[pi][1],
                   host.calls.length === 1 && host.calls[0] === pairCases[pi][1])
            }
        }

        // ── 八、边界：无在线设备 / 未订阅 ──
        tp.devices = [{ uid: "u-x", classCode: "cls-other", online: true }]
        host.calls = []
        var t0 = host.toasts.length
        btnByText(tp, "静音").clicked()
        ck("无本班在线：报错 toast", host.toasts.length === t0 + 1
           && host.toasts[host.toasts.length - 1][0].indexOf("没有在线设备") >= 0)
        ck("无本班在线：不下发", host.calls.length === 0)

        tp.devices = devices
        cs.subscribed = false
        ck("未订阅：静音按钮禁用", btnByText(tp, "静音").enabled === false)
        ck("未订阅：开启专注按钮禁用", btnByText(tp, "开启专注").enabled === false)
        cs.subscribed = true
        ck("恢复订阅：静音按钮启用", btnByText(tp, "静音").enabled === true)

        // ── 九、视觉实例（截图用：弹窗打开态 + 课堂工具）──
        dlg.open()
        form.type = "homework"
        if (ti) ti.text = "第三章 课后习题"
        if (te) te.text = "完成练习册 P12-P14，下周一交。"
        tp.width = 320
        tp.height = tp.implicitHeight
        tp.x = 20
        tp.y = 20

        // ── 十、本批改过的班级页：编译级检查（语法/import/重名 —— 0fad104 的
        //     actionLabel 重名就是这一层能抓到的 fatal）──
        var pages = [
            ["ClassDashboard.qml",   "班级工作台根页"],
            ["ClassActionPane.qml",  "全班动作组（发布入口）"],
            ["ClassPortraitStack.qml", "成员头像叠层"]
        ]
        for (var qi = 0; qi < pages.length; ++qi) {
            var cp = Qt.createComponent(
                "../build/qml/Stelarith/pages/class/" + pages[qi][0], Component.PreferSynchronous)
            ck(pages[qi][1] + "（" + pages[qi][0] + "）编译 Ready", cp.status === Component.Ready)
            if (cp.status !== Component.Ready) console.log("  errorString: " + cp.errorString())
        }

        console.log("CLASS-PUBLISH-PROBE 断言完毕（十节）")
        probe.finish(bad)
    }
}
