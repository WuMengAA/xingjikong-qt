// 系统设置 · 分类「插件」（2026-10-28 模块三：按用户蓝图重做设置页）
//
// 蓝图条目：「插件市场、插件配置」。
//
// ⚠️ 现状：**管理端没有插件能力** —— 插件宿主（PluginHost）在被控端
// （control-qt/src/plugin_host.h，进程外 ABI + 托盘菜单里的「插件市场…」）。
// 管理端既没有插件页，也没有插件清单接口，更没有市场。
//
// 所以这一栏只写清楚"插件装在哪、由谁管"，不造假列表。
// 真要给管理端做插件市场，前提是被控端把已装插件清单经云端上报（现在没有这条链路）。
import QtQuick

import "../../components"
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

SettingsCard {
    id: card

    title: "插件"
    subtitle: "第三方能力扩展"
    gap: 12

    property var ctx

    Text {
        width: parent.width
        text: "插件跑在设备端，不在这里"
        color: Theme.color.textSecondary
        font.pixelSize: 13
        font.weight: Font.Medium
    }
    Text {
        width: parent.width
        text: "插件宿主在被控端（独立进程，ABI 隔离），入口是那台设备的托盘右键「插件市场…」" +
              "或控制面板的「插件」页。\n" +
              "管理端不下发、不安装、不启停插件 —— 它连「这台设备装了哪些插件」都还看不到。\n\n" +
              "安全边界（被控端既有约定，写在这里便于对齐）：插件只能用白名单里的宿主能力，" +
              "经本地 WS 推灵动岛时要过「允许第三方推送」开关，且限 5 条/秒、标题 120 字。"
        wrapMode: Text.Wrap
        color: Theme.color.textSecondary
        font.pixelSize: 12
        lineHeight: 1.5
    }

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.color.textSecondary
        opacity: 0.6
    }

    Text {
        width: parent.width
        text: "做到「管理端插件市场」需要三步（都没做）"
        color: Theme.color.textSecondary
        font.pixelSize: 13
        font.weight: Font.Medium
    }
    Text {
        width: parent.width
        text: "① 被控端上报已装插件清单（id / 版本 / 启停态）；\n" +
              "② 云端存清单 + 提供市场索引（下载源、签名校验）；\n" +
              "③ 管理端下发安装/启停指令，并接住回执。\n\n" +
              "三步之前，这里只留说明 —— 摆一个点不动的「市场」按钮是最没用的东西。"
        wrapMode: Text.Wrap
        color: Theme.color.textSecondary
        font.pixelSize: 12
        lineHeight: 1.5
    }
}
