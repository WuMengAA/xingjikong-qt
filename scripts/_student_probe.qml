// 学生名单面板 · 运行时走查探针（2026-10-25 · 云端数据库第一批）
//
// 为什么必须跑：ClassStudentPane 是全新件，且在**加载时**就绑定 backend.classAdmin、
// 在 onCompleted 里发请求 —— qmllint 对"属性不存在/绑定到 undefined"一律 0 Error，
// 只有真引擎能验（本项目两次整页崩溃都是这么漏过去的）。
//
// 验什么：
//   ① 实例化即拉一次名单（subscribed/classCode 在实例化前就是真值时也不能漏拉）
//   ② 云端回包 → 列表出三行、座位/学号/无学号文案正确
//   ③ 输入行：空名禁用「加入」；填名后点击 → 调用参数正确 + 输入被清空
//   ④ 「移出」→ 调用 (班码, 学生 id)
//   ⑤ 未订阅：不拉名单、输入行隐藏、空态说人话
import QtQuick
import QtQuick.Window

import "../../Stelarith-shared-qml/ui/theme"
import "../build/qml/Stelarith/pages/class"

Window {
    id: win
    width: 760
    height: 720
    visible: true
    color: "#eceef1"

    Rectangle { anchors.fill: parent; z: -1; color: "#eceef1" }

    property int bad: 0
    property int step: 0
    property var toasts: []

    function ck(what, ok) {
        if (!ok) { bad++; console.log("✗ " + what) }
        else console.log("✓ " + what)
    }

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
    function textExists(root, t) {
        var all = allOf(root)
        for (var i = 0; i < all.length; ++i)
            if (all[i].text === t && all[i].visible !== false) return true
        return false
    }
    function btnByText(root, t) {
        var all = allOf(root)
        for (var i = 0; i < all.length; ++i)
            if (all[i].text === t && all[i].strong !== undefined) return all[i]
        return null
    }
    /** 找 Md3TextField（有 placeholder 与 setText）。 */
    function fieldByPlaceholder(root, ph) {
        var all = allOf(root)
        for (var i = 0; i < all.length; ++i)
            if (all[i].placeholder === ph && all[i].setText !== undefined) return all[i]
        return null
    }

    /** 有效可见性：自身 visible 为真但父链被隐藏时，用户其实看不见（控件仍可被找到）。
     *  2026-10-25：第一版直接判 all[i].visible，把"父 Column 已隐藏"误判成可见。 */
    function effectivelyVisible(o) {
        var n = o
        while (n) { if (n.visible === false) return false; n = n.parent }
        return true
    }
    function fieldVisible(root, ph) {
        var f = fieldByPlaceholder(root, ph)
        return f !== null && effectivelyVisible(f)
    }

    Component { id: paneComp; ClassStudentPane { } }

    property var pane: null

    Timer {
        id: tick
        interval: 140
        repeat: true
        running: true
        onTriggered: win.nextStep()
    }

    function nextStep() {
        ++step
        if (step === 1) {
            pane = paneComp.createObject(win, {
                "classCode": "cls-a", "subscribed": true,
                "toastFn": function (m, e) { win.toasts.push((e ? "ERR:" : "OK:") + m) }
            })
            pane.width = 520
            pane.height = 680
            pane.x = 20
            pane.y = 20
            ck("ClassStudentPane 实例化成功", pane !== null)
        } else if (step === 2) {
            // ① 实例化即拉名单
            ck("实例化时自动拉了本班名单",
               backend.classAdmin.calls.indexOf("students|cls-a") >= 0)
        } else if (step === 3) {
            backend.classAdmin.fakeStudentsList("cls-a", [
                { id: "s1", name: "王芳", studentNo: "20280102", seat: "01", note: "" },
                { id: "s2", name: "李明", studentNo: "20280101", seat: "03", note: "" },
                { id: "s3", name: "张伟", studentNo: "", seat: "", note: "" }
            ])
        } else if (step === 4) {
            ck("回包后列表出三行（三人姓名都在）",
               textExists(pane, "王芳") && textExists(pane, "李明") && textExists(pane, "张伟"))
            ck("无学号学生显示「无学号」", textExists(pane, "无学号"))
            ck("人数文案 = 3 人", textExists(pane, "3 人"))
        } else if (step === 5) {
            var add = btnByText(pane, "加入")
            ck("空姓名时「加入」禁用（不是点了没反应）", add !== null && add.disabled === true)
        } else if (step === 6) {
            var f1 = fieldByPlaceholder(pane, "姓名（必填）")
            var f2 = fieldByPlaceholder(pane, "学号")
            var f3 = fieldByPlaceholder(pane, "座位")
            ck("三个输入框都在", f1 !== null && f2 !== null && f3 !== null)
            if (f1) f1.setText("赵敏")
            if (f2) f2.setText("20280105")
            if (f3) f3.setText("05")
        } else if (step === 7) {
            var add2 = btnByText(pane, "加入")
            ck("填了姓名后「加入」可用", add2 !== null && add2.disabled === false)
            ck("输入回写到宿主属性", pane.inName === "赵敏" && pane.inNo === "20280105" && pane.inSeat === "05")
            if (add2) add2.clicked()
        } else if (step === 8) {
            var calls = backend.classAdmin.calls
            ck("「加入」调用参数 = (班码, 姓名, 学号, 座位)",
               calls.indexOf("stu-add|cls-a|赵敏|20280105|05") >= 0)
            ck("加完清空输入行（防重复加同一个人）",
               pane.inName === "" && pane.inNo === "" && pane.inSeat === "")
        } else if (step === 9) {
            // ④ 移出第一行
            var all = allOf(pane)
            var rm = null
            for (var i = 0; i < all.length; ++i)
                if (all[i].text === "移出" && all[i].strong !== undefined) { rm = all[i]; break }
            ck("列表里找到「移出」按钮", rm !== null)
            if (rm) rm.clicked()
        } else if (step === 10) {
            ck("「移出」调用 (班码, 学生 id)",
               backend.classAdmin.calls.indexOf("stu-rm|cls-a|s1") >= 0)
            ck("写操作走了 toast（成功也要说话）", win.toasts.length > 0)
        } else if (step === 11) {
            pane.subscribed = false
        } else if (step === 12) {
            ck("未订阅：输入行隐藏（父链不可见也算隐藏）", fieldVisible(pane, "姓名（必填）") === false)
            ck("未订阅：空态文案说清要先去选班",
               textExists(pane, "先在左上角选一个班级，再录入这个班的学生。"))
            tick.running = false
            console.log("STUDENT-PROBE 断言完毕（" + step + " 步）")
            probe.finish(bad)
        }
    }
}
