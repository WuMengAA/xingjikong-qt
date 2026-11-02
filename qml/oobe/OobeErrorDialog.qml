// 首次启动引导 · 错误提示对话框（2026-10-20）
// 用户明确要求：绑定失败必须在**当前页**弹一个友好的提示，不能崩、不能白屏。
// 这里包一层共享 AppDialog（不自造对话框外观），只统一「一个按钮 + 一句能照着做的话」口径。
//
// AppDialog 是"内容容器 + 受控 opened + closed() 信号"），故标题/正文/按钮改为显式摆。
// ⚠️ 对外 API 一字未动（opened / title / text / buttonText / dismissed），调用方零改动。
//
// ⚠️⚠️ 为什么本件**不叫** AppDialog.qml（2026-10-30 两次踩坑的结论）：
//   ① 同名自引用：文件若叫 AppDialog.qml，内层写 `AppDialog { }` 就是自引用，
//      该件编译失败，报错却指向调用方 `Cannot assign to non-existent property "onDismissed"`；
//   ② 更阴的是**静默抢占**：`OobeWizard.qml` 里有 `import "../components"`，
//      显式导入的优先级高于同目录隐式导入 ⇒ 那里的 `AppDialog` 一直解析到**共享库**
//      （只有 closed()、没有 dismissed()），本件从头到尾没被用上。
//      症状同样是那句 onDismissed 报错 —— OOBE 自 2026-10-29 引入共享 AppDialog 起
//      其实一直是坏的，只是没人跑 OOBE。
//   ⇒ 结论：包装件必须用不与共享件重名、也不与自身重名的名字；内层用别名 `Shared.AppDialog`。
import QtQuick
import QtQuick.Layouts
import "../components"
import "../components" as Shared
import "../components/theme"

Item {
    id: root
    property bool opened: false
    property string title: "这一步没成功"
    property string text: ""
    property string buttonText: "我知道了"
    signal dismissed()

    visible: opened
    z: 1000

    Shared.AppDialog {
        opened: root.opened
        onClosed: {
            root.opened = false
            root.dismissed()
        }

        Text {
            Layout.fillWidth: true
            text: root.title
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: Theme.color.textPrimary
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: root.text
            font.pixelSize: 14
            color: Theme.color.textSecondary
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing.sm

            Item { Layout.fillWidth: true }   // 撑开，按钮靠右

            AppButton {
                text: root.buttonText
                variant: "solid"
                onClicked: {
                    root.opened = false
                    root.dismissed()
                }
            }
        }
    }
}
