// 组件升级验证页（2026-10-14 · MD3 迁移批）
//
// 用途：三个共享旧组件（Btn / ToggleRow / InputField）内部升到 MD3 规格后，
// 用**旧调用方 API**（theme: Md3实例）驱动它们，离屏截图验证渲染正确。
// 与 MaterialLab（MD3 组件库验收）、LayoutLab（布局模板验收）并列的第三张验收页。
import QtQuick
import "components"
import "components/theme"   // Md3 令牌（pragma Singleton，2026-10-25；dark 由主外壳统一写）

Item {
    id: root
    width: 480; height: 640
    property bool dark: false
    readonly property var md3: Md3

    Rectangle { anchors.fill: parent; color: md3.cSurface }

    Column {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.margins: 20
        spacing: 18

        Text { text: "旧 API → MD3 组件升级验证"; font.pixelSize: md3.titleL; font.weight: Font.Medium; color: md3.cOnSurface }

        // ── Btn：默认(outlined) / strong(filled) / disabled ──
        Text { text: "Btn（旧 API：theme/text/strong/disabled/tip）"; font.pixelSize: md3.labelM; color: md3.cOnSurfaceVar }
        Row {
            spacing: 10
            Btn { theme: md3; width: 130; text: "默认 outlined"; tip: "悬停有提示" }
            Btn { theme: md3; width: 130; text: "主操作 filled"; strong: true }
            Btn { theme: md3; width: 110; text: "禁用"; disabled: true }
        }
        Row {
            spacing: 10
            Btn { theme: md3; text: "宽按钮独占整行"; strong: true; width: 300 }
        }

        // ── ToggleRow：单行 / 两行 ──
        Text { text: "ToggleRow（旧 API：theme/text/note/checked）"; font.pixelSize: md3.labelM; color: md3.cOnSurfaceVar }
        Rectangle {
            width: parent.width; height: 170
            radius: md3.rM
            color: md3.cContHigh
            Column {
                anchors.left: parent.left; anchors.right: parent.left
                anchors.margins: 16
                spacing: 4
                ToggleRow { theme: md3; width: 400; text: "操作完成时提示"; checked: true }
                ToggleRow { theme: md3; width: 400; text: "设备离线时提醒"; note: "与云端断开时弹一条（后台也能看见）"; checked: false }
            }
        }

        // ── InputField：空 / 有内容 ──
        Text { text: "InputField（旧 API：th/placeholderText）"; font.pixelSize: md3.labelM; color: md3.cOnSurfaceVar }
        InputField { th: md3; width: 400; placeholderText: "广播内容（聚焦描边升 2dp）" }
        InputField { th: md3; width: 400; text: "已输入的内容"; placeholderText: "占位" }
    }
}
