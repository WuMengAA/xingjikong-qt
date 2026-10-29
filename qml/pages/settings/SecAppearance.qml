// 系统设置 · 分类「主界面外观」（2026-10-28 模块三：按用户蓝图重做设置页）
//
// 蓝图条目：缩放 / 背景 / 透明度 / 圆角类型 / 动态模糊 / 颜色；主题浅深；字体；
//           组件（拖到灵动岛、官方预设组）；窗口（置顶 / 除全屏 / 保全屏 / 最大化）。
//
// 本件只把**已实现**的那条做实（主题三档），其余逐条说明缺口 ——
// 摆一排点了没反应的滑块，比明说"还没做"更让现场恼火。
//
// ⚠️ 主题选中态由 C++ 判（backend.themeOptions 的 strong 字段），
//    QML 里不写 `themeMode === 0 ? filled : tonal` —— 那是三元配色判断。
import QtQuick

import "../../components"
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

SettingsCard {
    id: card

    title: "主界面"
    subtitle: "外观与窗口行为"
    gap: 12

    property var ctx

    // ① 主题三档（浅色 / 深色 / 跟随系统）—— 这条是真实现的
    Text {
        width: parent.width
        text: "主题"
        color: Theme.color.textSecondary
        font.pixelSize: 13
    }
    Row {
        spacing: 8
        Repeater {
            model: backend.themeOptions
            Btn {
                theme: Md3
                text: modelData.label
                strong: modelData.strong
                onClicked: backend.themeMode = modelData.key
            }
        }
    }

    // ② 分隔
    Rectangle {
        width: parent.width
        height: 1
        color: Theme.color.textSecondary
        opacity: 0.6
    }

    // ③ 蓝图里还没实现的那些
    Text {
        width: parent.width
        text: "以下各项代码里还没有，先列清楚免得反复问："
        color: Theme.color.textSecondary
        font.pixelSize: 13
        font.weight: Font.Medium
    }
    Text {
        width: parent.width
        text: "· 缩放 / 背景图 / 不透明度 / 圆角类型 / 颜色主题：未实现（当前用固定令牌）。\n" +
              "· 动态模糊（背景毛玻璃）：未实现；Qt 6.12 的 MultiEffect 可用，但需要改窗口合成方式。\n" +
              "· 字体选择：未实现（跟随系统）。\n" +
              "· 组件拖到灵动岛 / 官方预设组：未实现 —— 岛的面是固定注册表（见 IslandRoot 的 faceModel），" +
              "拖拽排序要先改这套注册机制。\n" +
              "· 窗口置顶 / 除全屏 / 保全屏 / 最大化：未实现为可配项（当前是固定行为）。"
        wrapMode: Text.Wrap
        color: Theme.color.textSecondary
        font.pixelSize: 12
        lineHeight: 1.5
    }
}
