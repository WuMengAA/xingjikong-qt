// 本机设置窗布局模板（560×680 · 需求 #3「被控端本地开关」界面预演）
//
// 需求原文：本地也能控制被控端各项功能开关（语音接收/摄像头/广播接收/
// 通知弹窗/桌面被监控提示），存 QSettings，启动前 check。
// 策略待定项在卡内明说：本机拒绝的，云端能否强制打开。
import QtQuick
import "../components"
import "../components/md3"

Item {
    id: root
    width: 560; height: 680
    // MD3 令牌：内部自建实例（深/浅由 dark 驱动）。
    // ⚠️ 不许让宿主晚赋值替换整个 md3 实例——创建期 md3 为空会让 Behavior 时长
    //    变 NaN 卡死、颜色停默认值、空实例子树盖住宿主内容（2026-10-14 探针实测）。
    //    与手机端 mobile/ 页面同一模式：页面自建 Md3，宿主只传 dark 布尔。
    property bool dark: false
    readonly property var md3: Md3 { dark: root.dark }
    property var switches: [
        { k: "语音对讲接收", s: "关闭后听不到管理端语音，课堂安静模式", on: true },
        { k: "摄像头允许",   s: "关闭后监控画面不可用（管理端显示离线）", on: true },
        { k: "屏幕广播接收", s: "关闭后不接收广播推流", on: true },
        { k: "通知弹窗",     s: "关闭后灵动岛只显示状态不出通知", on: true },
        { k: "桌面被监控提示", s: "关闭后不显示「正在被监控」角标", on: false }
    ]

    Rectangle { anchors.fill: parent; color: md3.cContLowest }
    Rectangle {
        anchors.fill: parent
        anchors.margins: 16
        radius: md3.rXL
        color: md3.cContLow
        Column {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top; anchors.margins: 28
            spacing: 18

            Text { text: "本机设置"; font.pixelSize: md3.headlineS; font.weight: Font.Medium; color: md3.cOnSurface }
            Text {
                width: parent.width
                text: "这些开关只影响本机。关闭的项目云端不可见——是否需要「云端可强制打开」的策略，待学校确认。"
                wrapMode: Text.Wrap
                font.pixelSize: md3.bodyS; color: md3.cOnSurfaceVar
            }

            // 开关组（filled 卡 + Md3SwitchRow）
            Md3Card {
                md3: root.md3
                variant: "filled"
                width: parent.width
                Column {
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: root.switches
                        delegate: Column {
                            required property var modelData
                            width: parent.width
                            Md3SwitchRow {
                                md3: root.md3
                                width: parent.width
                                text: modelData.k
                                supporting: modelData.s
                                checked: modelData.on
                                onToggled: modelData.on = value
                            }
                            Rectangle {
                                width: parent.width; height: 1
                                color: md3.cOutlineVar
                                visible: index < root.switches.length - 1
                            }
                        }
                    }
                }
            }

            // 底部动作
            Row {
                anchors.right: parent.right
                spacing: 12
                Md3Btn { md3: root.md3; text: "恢复默认"; variant: "text" }
                Md3Btn { md3: root.md3; text: "保存"; variant: "filled"; onClicked: {} }
            }
        }
    }
}
