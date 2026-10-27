// 班级中心探针 · 态 1：网格阵列（班级中心主视图）
//
// 这是**探针**代码不是产品代码，允许写 JS 断言（产品 QML 一律禁逻辑）。
// 断言目标：页面真加载、C++ 属性真有值、映射表认识六个操作名且拒绝未知名。
import QtQuick 2.15
import QtQuick.Window 2.15
// 相对本文件（scripts/）解析 ⇒ ../build/qml/Stelarith/pages/class
// 与既有 _class_admin_probe.qml 同一套取径（它用 Qt.createComponent，这里改 import，
// 目的是让类型解析错误在**加载期**就暴露，而不是藏在 createObject 的 null 里）。
import "../build/qml/Stelarith/pages/class" as ClassPages

Window {
    width: 1440
    height: 900
    visible: true
    title: "class-center-probe-grid"

    ClassPages.ClassCenterPage {
        anchors.fill: parent
    }

    Component.onCompleted: {
        // 断言必须等布局完成（viewWidth/stageW 由 Binding 回写，onCompleted 时还是 0
        // ⇒ 断出来的全是 0/负中间态，2026-10-28 探针实测）→ 延后 800ms。
        timer.start()
    }

    Timer {
        id: timer
        interval: 800
        onTriggered: runAssert()
    }

    function runAssert() {
        var bad = 0
        function chk(cond, label) {
            if (cond) { console.log("  OK   " + label) }
            else { console.log("  FAIL " + label); bad++ }
        }

        console.log("── 班级中心探针 态1：网格阵列 ──")
        chk(classCenter !== null && classCenter !== undefined, "classCenter 已注入")
        chk(classCenter.classes.length > 0, "班级数据非空（条数=" + classCenter.classes.length + "）")
        chk(classCenter.anomalies.length > 0, "异常数据非空")
        chk(classCenter.totalDevices > 0, "设备总数>0（" + classCenter.totalDevices + "）")
        chk(classCenter.onlinePercent >= 0 && classCenter.onlinePercent <= 100,
            "在线率落在 0-100（" + classCenter.onlinePercent + "%）")
        chk(classCenter.onlinePercentText.length > 0, "在线率文本非空：" + classCenter.onlinePercentText)
        chk(classCenter.gridColumns >= 2, "网格列数>=2（" + classCenter.gridColumns + "）")
        chk(classCenter.gridCellWidth > 0, "格宽>0（" + Math.round(classCenter.gridCellWidth) + "）")
        // 圆饼端点：必须是有限数（NaN 会让 Path 整条不画，静态检查看不出来）
        chk(isFinite(classCenter.donutEndX) && isFinite(classCenter.donutEndY),
            "圆饼弧端点有限：" + classCenter.donutEndX.toFixed(2) + "," + classCenter.donutEndY.toFixed(2))
        // 舞台几何（远程桌面尚未进入，但几何应已算出）
        chk(classCenter.stageLeftW > 0, "舞台左区宽>0（" + Math.round(classCenter.stageLeftW) + "）")
        chk(classCenter.stageRightW > 0, "舞台右区宽>0（" + Math.round(classCenter.stageRightW) + "）")
        chk(classCenter.stageFrameH > 0, "16:9 画面高>0（" + Math.round(classCenter.stageFrameH) + "）")
        chk(classCenter.heroGradRadius > 0, "渐变半径>0（" + Math.round(classCenter.heroGradRadius) + "）")

        // ── 指令映射表：设备侧动作必须被接受、未知名必须被拒 ──
        // 判据：给控制器一个目标 uid 后，设备动作走到 actionOut 分支
        //（lastOpText="已把「X」发给 …"）；未知名走"未知操作"分支。
        // setter 是 Q_PROPERTY 的 WRITE，QML 直接赋值即可（无需 Q_INVOKABLE）。
        classCenter.targetUid = "dev-001"
        classCenter.targetName = "260901"
        // 「监控」不在这张表里：蓝图指向录像回放，被控端没有回放指令 ⇒ 控制器改走
        // navigateRequested("monitor") 跳页面。它必须**不**发设备指令（下面单独断言）。
        var ops = ["notify", "lock", "reboot", "shutdown", "schedule", "software"]
        for (var i = 0; i < ops.length; i++) {
            classCenter.requestOp(ops[i])
            var t = classCenter.lastOpText
            var okName = (t.indexOf("已把") >= 0)
            chk(okName, "映射表认识「" + ops[i] + "」→ 走到下发分支（" + t + "）")
        }
        // 紧急通知：长按 3 秒由 C++ 计时，走 notify（severity=urgent）
        classCenter.requestOp("emergency")
        chk(classCenter.lastOpText.indexOf("notify") >= 0,
            "紧急通知走 notify 分支（" + classCenter.lastOpText + "）")
        // 监控：跳运行监控页（回放入口在那），不下发设备指令
        classCenter.requestOp("monitor")
        chk(classCenter.lastOpText.indexOf("运行监控") >= 0,
            "监控→运行监控页（回放入口），不发假指令（" + classCenter.lastOpText + "）")
        chk(classCenter.lastOpOk === true, "监控跳转算成功（不是错误）")
        // 班级编辑：本地态开关（编辑态由 C++ 持有）
        var wasEditing = classCenter.editing
        classCenter.requestOp("class_edit")
        chk(classCenter.editing !== wasEditing, "班级编辑切换了 editing 态")
        classCenter.requestOp("class_edit")   // 复原，别影响后续截图的视觉
        classCenter.requestOp("__not_a_real_op__")
        chk(classCenter.lastOpText.indexOf("未知操作") >= 0,
            "未知名被拒：" + classCenter.lastOpText)
        chk(classCenter.lastOpOk === false, "未知名置 lastOpOk=false")
        // 无目标 uid 时必须明确拒发（不报错就是骗人）
        classCenter.targetUid = ""
        classCenter.requestOp("lock")
        chk(classCenter.lastOpText.indexOf("还没选设备") >= 0,
            "无目标时拒发：" + classCenter.lastOpText)
        classCenter.targetUid = "dev-001"

        // ── 长按紧急通知：3s 线性填充 + 松手 0.2s 回弹（蓝图定稿）──
        // 这段必须异步：进度是 C++ 的 QTimer 推的，同步断言只能看到 0。
        badCount = bad
        classCenter.beginEmergencyHold()
        holdMid.start()
    }

    property int badCount: 0

    function chk2(cond, label) {
        if (cond) { console.log("  OK   " + label) }
        else { console.log("  FAIL " + label); badCount++ }
    }

    Timer {
        id: holdMid
        interval: 600          // 3s 的 20% 左右
        onTriggered: {
            var p = classCenter.emergencyHoldProgress
            chk2(p > 0.05 && p < 0.6, "长按 600ms 后进度在 (0.05,0.6)：实测 " + p.toFixed(3))
            chk2(classCenter.emergencyArmed === false, "未到 3s 不算触发")
            classCenter.cancelEmergencyHold()
            // 松手瞬间不能归零（那是瞬断）—— 这是"200ms 回弹"的判据
            chk2(classCenter.emergencyHoldProgress > 0.0,
                 "松手瞬间进度未瞬断（" + classCenter.emergencyHoldProgress.toFixed(3) + "）")
            holdEnd.start()
        }
    }

    Timer {
        id: holdEnd
        interval: 320          // 回弹 200ms 后必然归零
        onTriggered: {
            chk2(classCenter.emergencyHoldProgress === 0.0,
                 "回弹结束归零（" + classCenter.emergencyHoldProgress.toFixed(3) + "）")
            chk2(classCenter.emergencyArmed === false, "回弹后未误触发")
            console.log("── 断言结束 bad=" + badCount + " ──")
            probe.finish(badCount)
        }
    }
}
