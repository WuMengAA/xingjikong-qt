// 三件新组件（AppErrorState / AppSkeleton / AppStatTile）的渲染探针
//
// 为什么必须有它：qmllint 对"给组件传了不存在的属性"、"Layout 用法错"这类问题
// **报 0 Error**，但运行时是整页不加载（AGENTS.md §9.6 有同类事故记录：
// TopBar 收了不存在的属性，qmllint 全绿、页面直接崩）。
// 本探针不进 exe、不参与打包，只用来回答一个问题：**这三件能不能真的画出来**。
//
// 用法：
//   node .../test-qml-structural.mjs  → 查结构
//   python scripts/_probe_ui_tiles.py → 编译并离屏渲染本文件
import QtQuick
import QtQuick.Window
// ⚠️ Layout 是**附加对象**，按文件解析 —— 用 Layout.fillWidth 的文件必须自己 import，
//    组件内部 import 了不算（实测报 "Non-existent attached object"，且指向的是用它的那行）
import QtQuick.Layouts
// 组件真源在 monorepo 根的 Stelarith-shared-qml/ui（构建期由 CMake 拷进 qml/components）。
// 探针直接指真源，避免"验的是拷贝件、真源已坏"。
import "../../Stelarith-shared-qml/ui"
import "../../Stelarith-shared-qml/ui/theme"

Window {
    id: win
    width: 720
    height: 780
    visible: true
    // 离屏渲染时窗口不会真的出现，但 sceneGraph 初始化需要它 visible
    color: Theme.color.background
    title: "AppErrorState / AppSkeleton / AppStatTile 渲染探针"

    // 探针用深色分支：被控端实际就是深色（Theme.dark 由 AgentShell 设 true）
    Component.onCompleted: Theme.dark = true

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 64
        clip: true

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 24
            anchors.top: parent.top
            anchors.topMargin: 24
            spacing: 20

            // ══════════ 1. AppStatTile：四个数字卡 ══════════
            Text {
                text: "1 · AppStatTile（四格均分，数字 21 / 单位 12 / 说明 12）"
                font.pixelSize: 12
                color: Theme.color.textSecondary
            }

            Row {
                spacing: 8
                // Row 不参与 Layout，故给每格显式宽度（在 AppCard 里由 Layout 均分）
                AppStatTile { width: 158; value: "3";     unit: "天"; label: "有噪音数据" }
                AppStatTile { width: 158; value: "5,830"; unit: "条"; label: "采样总数" }
                AppStatTile { width: 158; value: "2";     unit: "次"; label: "自习会话" }
                AppStatTile { width: 158; value: "91.2";  unit: "dB"; label: "最高峰值"; emphasized: true }
            }

            // ══════════ 2. AppSkeleton：加载中 ══════════
            Text {
                text: "2 · AppSkeleton（5 行，末行短一截，扫光错相位）"
                font.pixelSize: 12
                color: Theme.color.textSecondary
            }

            AppCard {
                width: parent.width
                variant: "muted"

                Text {
                    Layout.fillWidth: true
                    text: "插件市场"
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    color: Theme.color.textPrimary
                }
                AppSkeleton { Layout.fillWidth: true; lines: 5 }
            }

            // ══════════ 3. AppSkeleton：动画关闭态 ══════════
            Text {
                text: "3 · AppSkeleton animated:false（软件渲染退化态，必须仍是可见灰条）"
                font.pixelSize: 12
                color: Theme.color.textSecondary
            }

            AppCard {
                width: parent.width
                variant: "muted"
                AppSkeleton { Layout.fillWidth: true; lines: 2; animated: false }
            }

            // ══════════ 4. AppErrorState：带重试 ══════════
            Text {
                text: "4 · AppErrorState（有重试：说原因 + 说影响 + 给动作）"
                font.pixelSize: 12
                color: Theme.color.textSecondary
            }

            AppErrorState {
                width: parent.width
                title: "无法获取插件列表"
                reason: "云端没有响应（超时 8 秒）。"
                impact: "本机已安装的插件仍可正常使用，只是看不到新插件。"
                retryText: "重试"
                onRetried: console.log("[probe] rejected: retried 被触发")
            }

            // ══════════ 5. AppErrorState：无重试 ══════════
            Text {
                text: "5 · AppErrorState（无重试：用户自己重试无效的场景，不出按钮）"
                font.pixelSize: 12
                color: Theme.color.textSecondary
            }

            AppErrorState {
                width: parent.width
                title: "授权失效"
                reason: "登录凭证已过期。"
                impact: "请联系管理员重新绑定这台设备；正在进行的广播不受影响。"
                // retryText 留空 ⇒ 不出按钮（本探针验证"按钮消失后布局不塌"）
            }
        }
    }
}
