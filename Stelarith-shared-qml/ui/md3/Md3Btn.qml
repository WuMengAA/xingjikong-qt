// MD3 按钮：filled / tonal / elevated / outlined / text / error 六变体
// 规范：高 40、全圆 20、左右内边距 24（text 12）、状态层 hover 8% / press 10%。
// 2026-10-19：加 radius 覆写（默认 -1 = 跟随令牌 rFull 药丸，行为不变）；
//             调用方要统一方角（如监控中心按用户要求全按钮 12px）时显式传 radius。
//             同批加 tip（悬停说明）——需要 QtQuick.Controls 的 ToolTip。
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property var md3: ({})
    property string text: ""
    property string variant: "filled"
    property string icon: ""          // emoji 占位（Lucide 接入后换 SVG）
    // 注：enabled 直接复用 Item.enabled（基类已提供），不要在此重复声明，
    //     否则会触发 "Member enabled ... overrides a member of the base object" 告警。
    property real radius: -1          // <0 = 令牌 rFull；>=0 = 显式覆写圆角
    property string tip: ""           // 悬停说明（2026-10-19 补：与 Btn.tip 同 API）
    signal clicked()

    implicitWidth: innerRow.implicitWidth + (variant === "text" ? 24 : 48)
    implicitHeight: 40
    opacity: enabled ? 1 : 0.38

    function bgColor() {
        if (variant === "filled") return md3.cPrimary
        if (variant === "tonal") return md3.cSecondaryCont
        if (variant === "elevated") return md3.cContLow
        if (variant === "error") return md3.cError
        return "transparent"
    }
    function fgColor() {
        if (variant === "filled") return md3.cOnPrimary
        if (variant === "tonal") return md3.cOnSecondaryCont
        if (variant === "elevated") return md3.cPrimary
        if (variant === "error") return md3.cOnError
        return md3.cPrimary            // outlined / text
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius >= 0 ? root.radius : md3.rFull
        color: root.bgColor()
        border.width: variant === "outlined" ? 1 : 0
        border.color: md3.cOutline
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: root.fgColor()
            opacity: ma.pressed ? md3.stPress : (ma.containsMouse ? md3.stHover : 0)
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }
    Row {
        id: innerRow
        anchors.centerIn: parent
        spacing: 8
        Text {
            visible: root.icon !== ""
            text: root.icon
            font.pixelSize: 18
            color: root.fgColor()
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.text
            font.pixelSize: md3.labelL
            font.weight: Font.Medium
            color: root.fgColor()
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        onClicked: root.clicked()
    }
    ToolTip {
        visible: ma.containsMouse && root.tip !== ""
        delay: 450
        text: root.tip
    }
}
