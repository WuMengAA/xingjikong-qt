// 系统设置 · 分类「自动化」（2026-10-28 模块三：按用户蓝图重做设置页）
//
// 蓝图条目：「同步管理端自动化列表，支持本地新增」。
//
// ⚠️ 诚实的现状（不粉饰）：
//   · 规则引擎（AutomationEngine）**跑在被控端**，规则落在那台设备的
//     %LOCALAPPDATA%\xingjikong\automation.json；主入口是被控端灵动岛的「自」面。
//   · 管理端到被控端的下发/拉取**契约已定、代码未接**：
//     见 docs/自动化规则引擎-管理端契约spec-2026-10-27.md 里的 5 条指令
//     （automation_list / add / remove / toggle / run）。
//   · 所以这一栏现在只做两件事：说清规则在哪管、把契约指向说清楚。
//     等契约接线（被控端 executeAction 里那 5 个分支已在 0.6.50-rc.4 落地）之后，
//     这里再放真正的列表与编辑。
//
// 为什么不做成"假列表"：一张点了没反应的规则表，会让老师以为规则没生效。
import QtQuick

import "../../components"
import "../../components/theme"   // Md3 令牌（pragma Singleton）

SettingsCard {
    id: card

    title: "自动化"
    subtitle: "到点自动做什么"
    gap: 12

    property var ctx

    Text {
        width: parent.width
        text: "规则在哪管"
        color: Md3.cOnSurfaceVar
        font.pixelSize: 13
        font.weight: Font.Medium
    }
    Text {
        width: parent.width
        text: "规则跑在**被控端那台设备**上，不依赖管理端在线 —— 断网时照样到点执行。\n" +
              "增删与启停在**被控端灵动岛的「自」面**里：点开岛 → 翻到「自」→ 列表里启停/立即运行/删除，" +
              "底部「＋ 新建规则」建新规则。\n\n" +
              "支持的动作：通知 / 设置音量 / 静音 / 取消静音 / 锁屏 / 截图 / 启动指定程序 ——" +
              "全部复用既有指令白名单，没有新增权限面。"
        wrapMode: Text.Wrap
        color: Md3.cOnSurfaceVar
        font.pixelSize: 12
        lineHeight: 1.5
    }

    Rectangle {
        width: parent.width
        height: 1
        color: Md3.cOutlineVar
        opacity: 0.6
    }

    Text {
        width: parent.width
        text: "管理端批量下发（未接线）"
        color: Md3.cOnSurfaceVar
        font.pixelSize: 13
        font.weight: Font.Medium
    }
    Text {
        width: parent.width
        text: "契约已经把五条指令定好（列表 / 新增 / 删除 / 启停 / 立即运行），" +
              "被控端侧的执行分支也已落地；缺的是**管理端这一侧的 UI 与云端按班分发**。\n\n" +
              "在它接线之前，别在这里摆一个空表格 —— 老师会以为规则丢了。"
        wrapMode: Text.Wrap
        color: Md3.cOnSurfaceVar
        font.pixelSize: 12
        lineHeight: 1.5
    }
}
