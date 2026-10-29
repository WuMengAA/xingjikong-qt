// 星集控 · 开关行（AppSwitchRow）—— 2026-10-29 QtShadcn 规范
//
// ── 定位 ────────────────────────────────────────────────────────────────
// 取代共享库 `md3/Md3SwitchRow`（共享库第 4 件 App 系补齐，2026-10-29 用户点单
// 「共享库新增 4 件」）。**调用方 API 一字未改**：text / checked / supporting /
// toggled(value) —— 既有 5 处消费方（个人中心偏好卡 4、本机设置 1）零改动换名即可。
//
// ── 与 Md3SwitchRow 的三处行为差异 ──────────────────────────────────────
// 1. **去边框**：旧件未选态靠 `border.width: 2` 描边勾出轨道（规范 §1 禁）。
//    新件改用底色差：未选轨道 surfaceVariant、thumb 用 textSecondary（在浅底上仍有对比）。
// 2. **颜色走 Theme 令牌**：旧件读 Md3.cPrimary / cSurfaceVar / cOutline，本件只读 Theme。
// 3. **thumb 位移加果冻回弹**：Easing.OutBack + Theme.motion.normal，与 AppButton 同语言。
//
// ── 尺寸为什么仍是 56 / 72 ──────────────────────────────────────────────
// 两者都是 8 的倍数（规范 §2），且是 MD3 列表项标准高（单行 56 / 两行 72）；
// 改高会让既有设置页整列跳动，故保留。轨道 52×32 同前。
//
// 用法：
//   AppSwitchRow { width: parent.width; text: "开机自动启动"; supporting: "登录 Windows 后…";
//                  checked: backend.autoStart; onToggled: function (on) { backend.setAutoStart(on) } }

import QtQuick
import "theme"

Item {
    id: root

    property string text: ""
    property bool checked: false
    /** 次行说明；空串 = 单行（高 56），非空 = 两行（高 72）。 */
    property string supporting: ""

    signal toggled(bool value)

    implicitWidth: 320
    implicitHeight: root.supporting === "" ? 56 : 72

    // 主标签：单行垂直居中；两行居上（y:12），说明在下（y:38）。
    // ⚠️ 不要写成 verticalCenter + 说明 anchors.top —— 两行态会重叠（旧件踩过）。
    Text {
        anchors.left: parent.left
        anchors.right: sw.left
        anchors.rightMargin: Theme.spacing.md
        y: root.supporting === "" ? (parent.height - implicitHeight) / 2 : 12
        text: root.text
        font.pixelSize: 14
        color: Theme.color.textPrimary
        elide: Text.ElideRight
    }

    Text {
        anchors.left: parent.left
        anchors.right: sw.left
        anchors.rightMargin: Theme.spacing.md
        y: 38
        visible: root.supporting !== ""
        text: root.supporting
        font.pixelSize: 12
        color: Theme.color.textSecondary
        elide: Text.ElideRight
    }

    Item {
        id: sw
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 52
        height: 32

        // 轨道（无边框：靠底色差表达开关态）
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: root.checked ? Theme.color.primary : Theme.color.surfaceVariant
            Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
        }

        // thumb：关闭 16、开启 24（emphasized 规格，保留）
        Rectangle {
            x: root.checked ? 24 : 6
            anchors.verticalCenter: parent.verticalCenter
            width: root.checked ? 24 : 16
            height: width
            radius: width / 2
            color: root.checked ? Theme.color.primaryForeground : Theme.color.textSecondary
            Behavior on x {
                NumberAnimation {
                    duration: Theme.motion.normal
                    easing.type: Easing.OutBack
                }
            }
            Behavior on width { NumberAnimation { duration: Theme.motion.normal } }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.checked = !root.checked
                root.toggled(root.checked)
            }
        }
    }
}
