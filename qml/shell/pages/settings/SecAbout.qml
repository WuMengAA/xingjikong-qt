// 被控端 · 设置页分类「关于与诊断」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 927-980 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// 2026-10-30 用户点单「全部换新」：ash/ → 共享库 App* 体系，令牌走 Theme 单例；
// 最后一段说明文字的兜底 Hex `#CAC4D0`（MD3 遗留）一并换成 `Theme.color.textSecondary`。
//
// ⚠️ QML 零逻辑（用户铁律）：本件不含 Math / 三元 / function / console.log / 通信，
//    所有值都来自 agentUi（C++）的结果属性。本拆分是**纯结构移动，零行为变化**。
import QtQuick
import QtQuick.Layouts

import "../../../components"
import "../../../components/theme"

Column {
    id: root

    property var ctx
    property var vh

    width: parent ? parent.width : 0
    spacing: 14

// ─────────────────────────────────────────────────────
// ⑦ 关于与诊断：版本信息 + 自检 / 记录入口（托盘移植）
// ─────────────────────────────────────────────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            AppCardTitle { text: "关于星集控" }
            AppInfoRow { label: "被控端版本"; value: agentUi.versionText }
            AppInfoRow { label: "设备用途";   value: agentUi.deviceRoleText }
            AppInfoRow { label: "归属码";     value: agentUi.classCodeText }
            AppInfoRow { label: "云端地址";   value: agentUi.cloudUrl }
        }
    }

    // 托盘右键的「配置自检…」「噪音记录…」「自习记录…」三条（2026-10-28 移植）
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            AppCardTitle { text: "诊断与记录" }
            Text {
                Layout.fillWidth: true
                Layout.bottomMargin: 6
                text: "这三项原先只能从托盘右键进。内容各自在独立页里，这里只放入口 —— " +
                      "同一份实现在两处重复写，早晚会分叉。"
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            Row {
                spacing: 10
                AppButton {
                    text: "配置自检"
                    variant: "secondary"
                    onClicked: if (root.ctx) root.ctx.goToId("selfcheck")
                }
                // 「记录」页只有教室端有（广播机没有噪音/自习语义 —— 见 AgentPages.js）
                AppButton {
                    text: "噪音与自习记录"
                    variant: "secondary"
                    onClicked: if (root.ctx) root.ctx.goToId("records")
                }
            }
        }
    }
}
