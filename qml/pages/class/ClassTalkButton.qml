// 班级工作台 · 「按住说话」对讲键（2026-10-26）
//
// 为什么单独成件：原按钮写的是 `pane.host.startSpeaking`（外壳上根本没这个方法），
// 点下去只弹一句"请到集控页" —— 是个按钮长得像功能、其实是跳转提示的假按钮。
// 真接口是 backend.startSpeaking()/stopSpeaking()（Q_PROPERTY speaking/speakLeftSec），
// 语义是**按住说话、松开结束**，且云端 audio.start 不带 uid ⇒ 现在是全班/全校可听。
//
// 三件必须做对：
//   ① 按住 → 开麦；松开/移出/取消 → 停。不能只做 onClicked（那是"点一下开麦、
//      再点一下才停"，老师会以为松手就完事，实际麦克风还开着）。
//   ② 失败要说人话：startSpeaking 返回 false 时把 backend.speakError 原样带出来
//      （Android 拒权限时那句"请到设置里开启麦克风"就靠它）。
//   ③ 页面销毁要收麦：面板被切走时不停，云端会一直挂着"老师在讲话"，
//      学生端提示不消失（C++ 侧有最长时长兜底，但那是兜底不是正常路径）。
import QtQuick
import "../../island"            // SvgIcon
import "../../components/theme"  // Md3 令牌（pragma Singleton）

Rectangle {
    id: root

    /** 由宿主注入的回执通道（toastFn(msg, isErr)），与右栏其它动作同一条。 */
    property var toastFn: null

    /** 云端是否在讲（真属性，不是本地猜的）。 */
    readonly property bool live: _hasBackend && backend.speaking === true
    readonly property int leftSec: _hasBackend ? (backend.speakLeftSec || 0) : 0
    readonly property bool _hasBackend: (typeof backend !== "undefined" && backend) ? true : false

    implicitHeight: 40
    radius: Md3.rM
    color: live ? (Md3.frDanger || "#DC2626")
                : (_hover ? (Md3.frGreenD || "#16A34A") : (Md3.frGreen || "#22C55E"))
    Behavior on color { ColorAnimation { duration: Md3.durShort } }

    property bool _hover: false

    function toast(msg, isErr) { if (root.toastFn) root.toastFn(msg, isErr === true) }

    function _start() {
        if (!root._hasBackend) { root.toast("还没连上云端，语音用不了", true); return }
        if (backend.speaking) return
        if (!backend.startSpeaking()) {
            // speakError 是 C++ 写的人话（含 Android 权限引导）；没有就用兜底
            root.toast(backend.speakError ? ("开麦失败：" + backend.speakError)
                                          : "开麦失败：麦克风被占用或没有权限", true)
        }
    }
    function _stop() {
        if (root._hasBackend && backend.speaking) backend.stopSpeaking()
    }

    // 面板被销毁（切段/切班/切页）时收麦，否则云端会一直认为有人在讲
    Component.onDestruction: root._stop()

    Row {
        anchors.centerIn: parent
        spacing: 8

        // Row 只管横向排布：子项不加竖向锚（否则 Row 的定位与锚互相拉扯）
        SvgIcon {
            name: "mic"
            px: 16
            tint: "#FFFFFF"
        }
        Text {
            text: root.live
                  ? ("正在讲话 · 还剩 " + root.leftSec + "s（松开结束）")
                  : (root._hover ? "按住说话（松开结束）" : "按住说话")
            color: "#FFFFFF"
            font.pixelSize: Md3.body13
            font.weight: Font.Medium
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        // 按下即开麦、松开即停：与按钮上的字一一对应（不是"点一下切换"）
        onPressed: root._start()
        onReleased: root._stop()
        onCanceled: root._stop()
        onEntered: root._hover = true
        onExited: root._hover = false
    }
}
