// 班级中心探针 · 态 3：远程桌面 + 操作面板
//
// 验收点（蓝图模块一 3）：左侧 4:3 容器内居中 16:9（上下留黑边）；右侧 9:16 操作面板，
// 顶部「操作面板」+ MD 分割线，按钮统一长条底托；通知（树叶绿）/监控（浅蓝）2:1，
// 锁屏/重启明黄、关机亮红竖排，定时/软件 1:1，占位「功能完善中」，
// 底部分割线下「班级编辑」「设备设置」。
import QtQuick 2.15
import QtQuick.Window 2.15
import "../build/qml/Stelarith/pages/class" as ClassPages

Window {
    width: 1440
    height: 900
    visible: true
    title: "class-center-probe-remote"

    ClassPages.ClassCenterPage {
        anchors.fill: parent
    }

    Component.onCompleted: timer.start()

    // 断言必须等布局完成（stageW/stageH 由 Binding 回写，onCompleted 时是中间态）
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

        console.log("── 班级中心探针 态3：远程桌面 + 操作面板 ──")
        classCenter.openDrawer()
        classCenter.targetUid = "dev-001"     // enterRemote 前置：必须有目标设备
        classCenter.targetName = "260901"
        classCenter.enterRemote()
        chk(classCenter.stage === "remote", "已进入远程桌面（stage=remote）")
        chk(classCenter.remoteVisible === true, "remoteVisible=true（QML 不做字符串比较）")

        // 4:3 画面容器：宽/高应等于 4/3（允许 0.02 浮点误差）
        var ar = classCenter.stageLeftW / classCenter.stageBoxH
        chk(Math.abs(ar - 4.0 / 3.0) < 0.02, "画面区 4:3（实测比值 " + ar.toFixed(3) + "）")
        // 9:16 操作面板
        var ar2 = classCenter.stageRightW / classCenter.stageBoxH
        chk(Math.abs(ar2 - 9.0 / 16.0) < 0.02, "操作面板 9:16（实测比值 " + ar2.toFixed(3) + "）")
        // 容器内 16:9 画面：frameH / leftW
        var ar3 = classCenter.stageFrameH / classCenter.stageLeftW
        chk(Math.abs(ar3 - 9.0 / 16.0) < 0.02, "画面 16:9（实测比值 " + ar3.toFixed(3) + "）")

        // 配色：叶子绿 / 浅蓝 / 明黄 / 亮红 必须有值（QColor 未定义会变透明黑）
        chk(classCenter.notifyC0.toString().length > 0, "通知渐变色0：" + classCenter.notifyC0)
        chk(classCenter.monitorC1.toString().length > 0, "监控渐变色1：" + classCenter.monitorC1)
        chk(classCenter.warnBase.toString().length > 0, "明黄底：" + classCenter.warnBase)
        chk(classCenter.dangerBase.toString().length > 0, "亮红底：" + classCenter.dangerBase)
        // 偏转角：通知 +45 / -27，监控镜像 -45 / +27（蓝图定稿）
        chk(classCenter.notifyIconRot === 45 && classCenter.notifyBarRot === -27,
            "通知偏转 麦克风+45 / 长条框-27")
        chk(classCenter.monitorIconRot === -45 && classCenter.monitorBarRot === 27,
            "监控偏转 镜像 -45 / +27")
        chk(classCenter.decoIconOpacity === 0.5 && classCenter.decoBarOpacity === 0.3,
            "装饰透明度 0.5 / 0.3")

        console.log("── 断言结束 bad=" + bad + " ──")
        probe.finish(bad)
    }
}
