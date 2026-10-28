// 被控端 · 设置页分类「关于与诊断」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 927-980 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// ⚠️ QML 零逻辑（用户铁律）：本件不含 Math / 三元 / function / console.log / 通信，
//    所有值都来自 agentUi（C++）的结果属性。本拆分是**纯结构移动，零行为变化**。
import QtQuick

import "../../../ash"

Column {
    id: root

    property var ctx
    property var vh
    property var ash

    width: parent ? parent.width : 0
    spacing: 14

// ─────────────────────────────────────────────────────
// ⑦ 关于与诊断：版本信息 + 自检 / 记录入口（托盘移植）
// ─────────────────────────────────────────────────────
    AshCard {
        th: root.ash
        anchors.left: parent.left
        anchors.right: parent.right
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 2
            AshCardTitle { text2: "关于星集控"; bottomPadding: 6 }
            AshInfoRow { label: "被控端版本"; value: agentUi.versionText }
            AshInfoRow { label: "设备用途";   value: agentUi.deviceRoleText }
            AshInfoRow { label: "归属码";     value: agentUi.classCodeText }
            AshInfoRow { label: "云端地址";   value: agentUi.cloudUrl }
        }
    }

    // 托盘右键的「配置自检…」「噪音记录…」「自习记录…」三条（2026-10-28 移植）
    AshCard {
        th: root.ash
        anchors.left: parent.left
        anchors.right: parent.right
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 2
            AshCardTitle { text2: "诊断与记录"; bottomPadding: 6 }
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "这三项原先只能从托盘右键进。内容各自在独立页里，这里只放入口 —— " +
                      "同一份实现在两处重复写，早晚会分叉。"
                font.pixelSize: 11
                color: root.ash ? root.ash.mutedFg : "#CAC4D0"
                wrapMode: Text.WordWrap
                bottomPadding: 6
            }
            Row {
                spacing: 10
                AshButton {
                    th: root.ash
                    text: "配置自检"
                    variant: "tonal"
                    onClicked: if (root.ctx) root.ctx.goToId("selfcheck")
                }
                // 「记录」页只有教室端有（广播机没有噪音/自习语义 —— 见 AgentPages.js）
                AshButton {
                    th: root.ash
                    text: "噪音与自习记录"
                    variant: "tonal"
                    onClicked: if (root.ctx) root.ctx.goToId("records")
                }
            }
        }
    }
}
