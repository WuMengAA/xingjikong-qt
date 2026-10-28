// 被控端 · 设置页分类「插件」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 881-915 行）
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

import "../../../components/md3" as M

Column {
    id: root

    property var ctx
    property var vh
    readonly property var tok: (ctx && ctx.theme) ? ctx.theme : null

    width: parent ? parent.width : 0
    spacing: 14

// ─────────────────────────────────────────────────────
// ⑥ 插件：内置模块 + 第三方插件 + 插件市场入口（托盘移植）
// ─────────────────────────────────────────────────────
    M.Md3Card {
        anchors.left: parent.left
        anchors.right: parent.right
        variant: "filled"
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 8
            SecCardTitle { text2: "插件与模块" }
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "插件的安装、启停与卸载在「插件」页；托盘右键的「插件市场」也是那一页。" +
                      "这里放快捷入口，省一次底导航来回。"
                font.pixelSize: 11
                color: root.tok ? root.tok.cOnSurfaceVar : "#CAC4D0"
                wrapMode: Text.WordWrap
            }
            Row {
                topPadding: 4
                spacing: 10
                M.Md3Btn {
                    text: "打开插件与模块"
                    variant: "filled"
                    onClicked: if (root.ctx) root.ctx.goToId("plugins")
                }
                M.Md3Btn {
                    text: "刷新清单"
                    variant: "tonal"
                    onClicked: agentUi.pluginsRefresh()
                }
            }
        }
    }
}
