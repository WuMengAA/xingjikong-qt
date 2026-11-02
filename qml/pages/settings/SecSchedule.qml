// 系统设置 · 分类「课表」（2026-10-25 拆出）
//
// 只放"入口"：排课编辑器是独立窗口（ScheduleEditor，来自 schedule-qt 并入），
// 本卡不重复实现它，只负责打开 + 说清它管什么。
import QtQuick

import "../../components"
import "../../components/theme"

SettingsCard {
    id: card

    title: "课表"
    subtitle: "排课、时间轴、科目与多周轮换（兼容 ClassIsland）"
    gap: 12

    property var ctx

    Btn {
        width: 148
        theme: ({ })
        text: "打开课表编辑器"
        tip: "编辑本机课表档案；保存后设备大屏按它显示今日课表"
        onClicked: if (card.ctx) card.ctx.window.openScheduleEditor()
    }
}
