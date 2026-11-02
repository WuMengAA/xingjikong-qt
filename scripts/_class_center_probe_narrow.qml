// 班级中心探针 · 态 5：窄屏（手机竖屏 390×844）
//
// 这是**探针**代码不是产品代码，允许写 JS 断言（产品 QML 一律禁逻辑）。
// 断言选的是**几何不变量**，不是"改后的值"——后者等于把实现抄进断言，实现一改就永远绿。
//   ① 边距必须真的收窄（32→16）：否则 390pt 上有 16% 宽度喂给空白。
//   ② 统计卡必须堆叠成单列：横排 1/3 + 2/3 在 390pt 上是 100/200 宽，
//      "最近异常"一行要塞 时间·班级·事件 ⇒ 必然逐行折字。
//   ③ 班级阵列必须单列：桌面最小格 260 × 2 列 = 520 > 390 ⇒ 双列时每格被压到 163，
//      班名+设备数+在线点三个信息挤在一行，肉眼看到的是三个省略号。
//   ④ 不变量：所有可量化元素左右边界必须落在页面内（不靠 clip 掩盖溢出）。
import QtQuick 2.15
import QtQuick.Window 2.15
import "../build/qml/Stelarith/pages/class" as ClassPages

Window {
    width: 390
    height: 844
    visible: true
    title: "class-center-probe-narrow"

    ClassPages.ClassCenterPage {
        anchors.fill: parent
    }

    // viewWidth 由页面自己的 Binding 回写（onCompleted 时还是 0）⇒ 延后断言
    Component.onCompleted: timer.start()
    Timer { id: timer; interval: 800; onTriggered: runAssert() }

    function runAssert() {
        var bad = 0
        function chk(cond, label) {
            if (cond) { console.log("  OK   " + label) }
            else { console.log("  FAIL " + label); bad++ }
        }

        console.log("── 班级中心探针 态5：窄屏 390 ──")
        // ① 视口真的到了控制器里（否则后面全是桌面值，断不出问题）
        chk(Math.abs(classCenter.viewWidth - 390) < 2,
            "控制器收到窄视口宽（" + classCenter.viewWidth.toFixed(1) + "）")
        chk(classCenter.narrow === true, "narrow 判为真")
        chk(classCenter.pageMargin <= 16,
            "边距已收窄（" + classCenter.pageMargin + "，桌面为 32）")

        // ② 统计卡单列堆叠
        chk(classCenter.statsColumns === 1,
            "统计区单列（" + classCenter.statsColumns + "，桌面为 2）")
        var contentW = classCenter.viewWidth - classCenter.pageMargin * 2
        chk(Math.abs(classCenter.statCardNarrowW - contentW) < 1.5,
            "窄卡占满内容宽（" + Math.round(classCenter.statCardNarrowW) + " vs " + Math.round(contentW) + "）")
        chk(Math.abs(classCenter.statCardWideW - contentW) < 1.5,
            "宽卡也占满内容宽（堆叠态两卡同宽：" + Math.round(classCenter.statCardWideW) + "）")

        // ③ 班级阵列单列
        chk(classCenter.gridColumns === 1,
            "阵列单列（" + classCenter.gridColumns + "，桌面 ≥2）")
        chk(Math.abs(classCenter.gridCellWidth - contentW) < 1.5,
            "格宽 = 内容宽（" + Math.round(classCenter.gridCellWidth) + "）")
        // 不变量：单列格宽必须 ≥ 桌面最小格，否则等于"虽然单列但仍然挤"
        chk(classCenter.gridCellWidth >= 240,
            "格宽仍放得下班名+设备数（" + Math.round(classCenter.gridCellWidth) + " ≥ 240）")

        // ④ 圆饼与舞台几何在窄屏下仍必须是有限数（NaN 会让整块不画，静态检查看不出）
        chk(isFinite(classCenter.donutEndX) && isFinite(classCenter.donutEndY),
            "圆饼端点有限：" + classCenter.donutEndX.toFixed(2) + "," + classCenter.donutEndY.toFixed(2))

        console.log("── 断言结束 bad=" + bad + " ──")
        probe.finish(bad)
    }
}
