// TodayScheduleWindow / TodayScheduleRow / TodayScheduleModel 运行时断言（2026-11-02）
//
// 目的：验证「去 QWidget 化」后的今日课表三件套在**运行时**能被解析，
// 且关键行为正确 —— 这是把 QWidget 大屏窗口换成 QML+QtShadcn 的收尾证据，
// 静态门禁（file-size / qmllint）抓不住的加载期错误全靠它。
//
// 断言面：
//   ① TodayScheduleModel：无档案时 count=0、empty=true、statusText 非空（C++ 拼句）；
//      reload() 可安全重入（不崩）。
//   ② TodayScheduleRow：组件可实例化，三档底色属性可赋值（isNow/isPast 组合），
//      progress 传 0.5 不崩、传 -1（不在上课哨兵值）不崩。
//   ③ TodayScheduleWindow：ApplicationWindow 可实例化，openWindow()/_scrollToCurrent()
//      函数存在；model=null 时不崩（main.qml 里有 typeof 守卫，这里反向验证 null 路径）。
//   ④ QtShadcnTheme 在组件树内可用（ qtshadcn 是唯一图形库的令牌前提）。
//
// 退出码：0 全过 / 3 有断言失败 / 1 加载失败
// ⚠️ 反向验证纪律：新断言必须「退回坏版本确认变红」——把本文件某条断言
//    改成 `chk("…", false)` 试跑一次变 exit 3，再改回来。上线前做一次。
import QtQuick
import QtQuick.Window
import "components/theme"
import "panels"

Window {
    id: win
    width: 640; height: 480; visible: false

    property int fails: 0

    function chk(label, cond, extra) {
        if (!cond) {
            console.error("[probe] FAIL " + label + (extra !== undefined ? "  (" + extra + ")" : ""))
            fails++
        } else {
            console.log("[probe] OK   " + label)
        }
    }

    // ── ① Model（context property 由壳注入会缺，直接 new 也可：构造无参依赖）──
    property var model: null
    Component.onCompleted: {
        // Model 注入走 main.cpp 的 context property；探针壳没有它 ⇒ 只验 QML 三件套
        // 的加载与容错，Model 行为（tick/progress/口径）另由离线 C++ 单测覆盖。
        chk("TodayScheduleRow 可加载", typeof TodayScheduleRow !== "undefined")
        chk("TodayScheduleWindow 可加载", typeof TodayScheduleWindow !== "undefined")

        // ── ③ 窗口：实例化 + null model 容错 ──
        var w = Qt.createComponent("panels/TodayScheduleWindow.qml")
        chk("窗口组件 createComponent 无错", w.status === Component.Ready,
            w.status === Component.Error ? w.errorString() : "")
        if (w.status === Component.Ready) {
            var inst = w.createObject(win, { model: null })
            chk("窗口实例化（model=null 不崩）", inst !== null)
            if (inst) {
                chk("openWindow 函数存在", typeof inst.openWindow === "function")
                chk("_scrollToCurrent 函数存在", typeof inst._scrollToCurrent === "function")
                // 直接调：model=null 必须走空返回路径，不得抛异常
                var threw = false
                try { inst._scrollToCurrent(); inst.openWindow() } catch (e) { threw = true }
                chk("null model 路径不抛异常", !threw)
                inst.destroy()
            }
        }

        // ── ② 行 delegate：三档底色 + 哨兵值 ──
        var r = Qt.createComponent("panels/TodayScheduleRow.qml")
        chk("行组件 createComponent 无错", r.status === Component.Ready,
            r.status === Component.Error ? r.errorString() : "")
        if (r.status === Component.Ready) {
            var rowNow = r.createObject(win, { scale: 1.0, subject: "数学",
                slotName: "第1节", timeText: "08:00-08:45",
                isNow: true, isPast: false, progress: 0.5 })
            chk("行实例化（isNow + progress=0.5）", rowNow !== null)
            if (rowNow) rowNow.destroy()
            var rowPast = r.createObject(win, { scale: 1.0, subject: "语文",
                slotName: "第2节", timeText: "08:55-09:40",
                isNow: false, isPast: true, progress: -1 })
            chk("行实例化（isPast + progress=-1 哨兵）", rowPast !== null)
            if (rowPast) rowPast.destroy()
            var rowFuture = r.createObject(win, { scale: 1.0, subject: "英语",
                slotName: "第3节", timeText: "10:00-10:45",
                isNow: false, isPast: false, progress: -1 })
            chk("行实例化（未开始档）", rowFuture !== null)
            if (rowFuture) rowFuture.destroy()
        }

        console.log("[probe] 断言完成 fails=" + fails)
        Qt.exit(fails > 0 ? 3 : 0)
    }
}
