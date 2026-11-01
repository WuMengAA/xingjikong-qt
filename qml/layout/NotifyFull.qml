// 通知全屏布局模板（960×600 · 被控端）
//
// 语境：需求 #4「灵动岛是唯一信息出口」之前的全屏通知形态（2026-10-26 P0 起
// 通知语义收敛为 弹窗/角标/跑马灯/全屏 四类，本模板即「全屏」的版式参考）。
// 本模板演示全屏态的 MD3 形态：severity 着色 + 来源信息 + 倒计时条 + 单按钮。
import QtQuick
import "../components"
import "../components/theme"   // Theme 设计令牌单例（pragma Singleton，2026-10-25；dark 由主外壳统一写）

Item {
    id: root
    width: 960; height: 600
    // MD3 令牌：内部自建实例（深/浅由 dark 驱动）。
    // ⚠️ 不许让宿主晚赋值替换整个 md3 实例——创建期 md3 为空会让 Behavior 时长
    //    变 NaN 卡死、颜色停默认值、空实例子树盖住宿主内容（2026-10-14 探针实测）。
    //    与手机端 mobile/ 页面同一模式：页面自建 Md3，宿主只传 dark 布尔。
    property bool dark: false
    property string severity: "inform"    // urgent | inform | remind
    property int remainSec: 8
    property string title: "课间操音乐即将开始"
    property string body: "请各班同学停止当前操作，回到座位准备。广播站将于 09:55 播放第三套广播体操。"
    property string source: "广播站 · 教务处"

    readonly property color sevColor: root.severity === "urgent" ? Theme.color.destructive
                                     : (root.severity === "remind" ? Theme.color.success : Theme.color.warning)

    Rectangle { anchors.fill: parent; color: Theme.color.background }

    // 全屏卡（severity 淡染 + 左侧色条）
    Rectangle {
        anchors.centerIn: parent
        width: 880; height: 480
        radius: Theme.radius.xl
        color: Theme.color.background
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: root.sevColor
            opacity: 0.12
        }
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 24; anchors.bottomMargin: 24
            width: 6
            radius: 3
            color: root.sevColor
        }
        Column {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.top: parent.top; anchors.margins: 48
            spacing: 24

            // 头部：图标 + 标题
            Row {
                spacing: 18
                Rectangle {
                    width: 56; height: 56; radius: 28
                    color: root.sevColor
                    opacity: 0.16
                    Text { anchors.centerIn: parent; text: "📢"; font.pixelSize: 26 }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.title
                    font.pixelSize: 20; font.weight: Font.Medium
                    color: Theme.color.textPrimary
                }
            }

            // 正文
            Text {
                width: parent.width
                text: root.body
                wrapMode: Text.Wrap
                font.pixelSize: 14
                color: Theme.color.textSecondary
            }

            // 来源
            Row {
                spacing: 8
                Text { text: "来自："; font.pixelSize: 12; color: Theme.color.textSecondary; anchors.verticalCenter: parent.verticalCenter }
                Rectangle {
                    width: srcTxt.implicitWidth + 24; height: 28; radius: 14
                    color: Theme.color.primary
                    Text { id: srcTxt; anchors.centerIn: parent; text: root.source; font.pixelSize: 14; font.weight: Font.Medium; color: Theme.color.primaryForeground }
                }
            }
        }

        // 倒计时进度条（severity 色）
        Column {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: okBtn.top; anchors.bottomMargin: 24
            anchors.leftMargin: 48; anchors.rightMargin: 48
            spacing: 8
            Rectangle {
                width: parent.width; height: 6; radius: 3
                color: Theme.color.surfaceVariant
                Rectangle {
                    width: parent.width * 0.62
                    height: parent.height; radius: 3
                    color: root.sevColor
                }
            }
            Text {
                text: root.remainSec + " 秒后自动关闭"
                font.pixelSize: 14; color: Theme.color.textSecondary
            }
        }

        // 操作
        Row {
            id: okBtn
            anchors.right: parent.right; anchors.rightMargin: 48
            anchors.bottom: parent.bottom; anchors.bottomMargin: 32
            spacing: 12
            AppButton {
                text: "知道了"
                variant: "primary"
                // 模板占位：真实实现见被控端通知域（Esc / 超时 / 点击关闭三条通路）
                onClicked: {}
            }
        }
    }
}
