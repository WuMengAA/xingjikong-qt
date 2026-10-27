// 班级中心探针 · 态 2：右侧抽屉「班级设备面板」
//
// 验收点（蓝图模块一 2）：第一行固定标题「班级设备面板」；第二行 学校>届>班级 + [编辑]；
// 第三行 160×90 缩略图卡片（点击进入远程桌面）。
import QtQuick 2.15
import QtQuick.Window 2.15
import "../build/qml/Stelarith/pages/class" as ClassPages

Window {
    width: 1440
    height: 900
    visible: true
    title: "class-center-probe-drawer"

    ClassPages.ClassCenterPage {
        anchors.fill: parent
    }

    Component.onCompleted: timer.start()

    // 断言必须等布局完成（drawerX/viewWidth 由 Binding 回写，onCompleted 时是中间态）
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

        console.log("── 班级中心探针 态2：右侧抽屉 ──")
        // 抽屉由 C++ 管开合（QML 里没有 open/close 函数 —— 本批铁律）
        // 先按真入口选一个班（点卡片 = openClass）：抽屉里的设备/面包屑都挂在"当前班"上，
        // 不选班就开抽屉 = 空抽屉（这是设计行为，不是缺陷）。
        classCenter.openClass("cls-a")
        chk(classCenter.drawerOpen === true, "抽屉已开（drawerOpen=true）")
        chk(classCenter.overlayOpacity > 0, "遮罩透明度>0（" + classCenter.overlayOpacity + "）")
        chk(classCenter.drawerWidth > 0, "抽屉宽>0（" + classCenter.drawerWidth + "）")
        // 抽屉滑到位：x = 视口宽 - 抽屉宽（C++ 算）
        var wantX = classCenter.viewWidth - classCenter.drawerWidth
        chk(Math.abs(classCenter.drawerX - wantX) < 1.0,
            "抽屉 x 落在停靠位（" + Math.round(classCenter.drawerX) + " ≈ " + Math.round(wantX) + "）")

        // 「编辑」展开态也归 C++（抽屉里不再是 QML 的 bool）
        classCenter.toggleEditing()
        chk(classCenter.editing === true, "编辑态已展开（editing=true）")
        classCenter.toggleEditing()
        chk(classCenter.editing === false, "编辑态可收起（editing=false）")

        // 面包屑 / 设备名 / 本班设备清单（全部 C++ 给，QML 不拼字符串）
        chk(classCenter.breadcrumbText.length > 0, "面包屑非空：" + classCenter.breadcrumbText)
        chk(classCenter.targetName.length > 0, "设备名非空：" + classCenter.targetName)
        chk(classCenter.devices.length === 3,
            "本班设备数=3（" + classCenter.devices.length + "）")
        chk(classCenter.classOptions.length === 2,
            "改绑选项=2 个班（" + classCenter.classOptions.length + "）")
        // 缩略图：探针没有真图（ClassService 未订阅）⇒ 必须如实为空，界面走"暂无画面"空态
        chk(classCenter.thumbAvailable === false && classCenter.thumbSource === "",
            "无缩略图时 thumbSource 为空（界面走空态）")
        // 再开一次同班不应丢选择（幂等）
        var keepUid = classCenter.targetUid
        classCenter.openClass("cls-a")
        chk(classCenter.targetUid === keepUid, "重复开同班不重置目标设备")

        console.log("── 断言结束 bad=" + bad + " ──")
        // ⚠️ 抽屉 x 带 400ms Behavior 动画：断言读到的是 C++ 算的停靠位（瞬间就是对的），
        // 但**画面**要等动画跑完才滑进来。抓图必须等 settle，否则拍到的是屏外空抽屉
        // （2026-10-28 B3 首次出图就是这个问题：断言全绿、截图里没抽屉）。
        badCount = bad
        settle.start()
    }

    property int badCount: 0

    Timer {
        id: settle
        interval: 600
        onTriggered: probe.finish(badCount)
    }
}
