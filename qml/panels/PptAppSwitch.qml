// PPT 面板 · 三档应用切换（2026-10-27 · I 批）
//
// 为什么必须给这个开关（ICC CE 原文坑）：WPS 的「相容第三方系统和软件」一旦开启，
// **MS Office 的 COM 接口就检测不到** —— 反过来也一样，两者只能取其一。
// 所以"控哪个"必须显式可选，且当前值要一眼看见；切换后由面板重新回读真实状态，
// **绝不悄悄切**。
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: sw

    property var theme: null
    property string preferred: "auto"     // auto / powerpoint / wps

    signal picked(string app)

    spacing: 8

    Text {
        text: "控制的程序"
        color: sw.theme ? sw.theme.fg3 : "#CAC4D0"
        font.pixelSize: 12
    }

    Repeater {
        model: [
            { key: "auto",       label: "自动" },
            { key: "powerpoint", label: "PowerPoint" },
            { key: "wps",        label: "WPS" }
        ]
        Rectangle {
            readonly property bool on: sw.preferred === modelData.key
            Layout.preferredWidth: pill.implicitWidth + 22
            Layout.preferredHeight: 26
            radius: 13
            color: on ? Qt.rgba(sw.theme ? sw.theme.acc.r : 0.4,
                                sw.theme ? sw.theme.acc.g : 0.3,
                                sw.theme ? sw.theme.acc.b : 0.85, 0.18)
                      : "transparent"
            border.width: 0
            border.color: on ? (sw.theme ? sw.theme.acc : "#FAFAFA")
                             : (sw.theme ? sw.theme.stroke2 : "#71717A")
            Text {
                id: pill
                anchors.centerIn: parent
                text: modelData.label
                color: parent.on ? (sw.theme ? sw.theme.acc : "#FAFAFA")
                                 : (sw.theme ? sw.theme.fg3 : "#CAC4D0")
                font.pixelSize: 12
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: sw.picked(modelData.key)
            }
        }
    }

    Item { Layout.fillWidth: true }
}
