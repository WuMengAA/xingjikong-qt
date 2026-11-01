// 系统设置 · 分类「主界面外观」（2026-10-28 模块三：按用户蓝图重做设置页）
//
// 蓝图条目：缩放 / 背景 / 透明度 / 圆角类型 / 动态模糊 / 颜色；主题浅深；字体；
//           组件（拖到灵动岛、官方预设组）；窗口（置顶 / 除全屏 / 保全屏 / 最大化）。
//
// 本件只把**已实现**的那条做实（主题三档），其余逐条说明缺口 ——
// 摆一排点了没反应的滑块，比明说"还没做"更让现场恼火。
//
// ⚠️ 主题选中态读 backend.themeMode（0 浅 / 1 深 / 2 跟随系统），
//    QML 里不写 `themeMode === 0 ? filled : tonal` —— 那是三元配色判断。
//    （旧的 backend.themeOptions/strong 那条路已随 Btn 一起退役，属性仍留在 C++ 侧未删。）
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
    // 2026-11-01（用户点单「外观按钮鼠标移上去会有两个选中效果」）：
    //   原来这一排用遗留 `Btn`（Md3 令牌）：**未选中按钮的文字就是品牌紫**（= 选中按钮的
    //   填充色），鼠标移上去再叠一层 8% 紫状态层 ⇒ 看上去"两个都是选中的"。
    //   换共享标准件 AppSegmentedButton：选中段唯一实底 accentStrong + 反白字，
    //   未选中段透明底 + 次级灰字 + 中性 hover 底（#F4F4F5）⇒ 任何时刻只有一个选中效果。
    //   顺带把本页统一到 Theme 令牌（本卡其余部分早就是 Theme.color.*，只有这排是旧的）。
    Text {
        width: parent.width
        text: "主题"
        color: Theme.color.textSecondary
        font.pixelSize: 13
    }
    AppSegmentedButton {
        model: [
            { value: 0, label: "浅色" },
            { value: 1, label: "深色" },
            { value: 2, label: "跟随系统" }
        ]
        current: backend.themeMode
        onActivated: function (v) { backend.themeMode = v }
    }

    // ② 分隔
    Rectangle {
        width: parent.width
        height: 1
        color: Theme.color.textSecondary
        opacity: 0.6
    }

    // ②b 新版外壳预览（2026-11-01）
    //   背景：新版外壳设计稿（qml/layout/DesktopConsole.qml，2026-10-14 起）一直没接生产，
    //   生产跑的是经典外壳（Sidebar/TopBar/ActionBar）。本开关切到设计稿的真数据版
    //   （qml/shell/PreviewConsole.qml）—— **预览闸**，确认满意后才考虑替换经典外壳。
    //   状态在 backend.previewShell（QSettings ui/previewShell），默认关。
    Text {
        width: parent.width
        text: "新版外观预览"
        color: Theme.color.textSecondary
        font.pixelSize: 13
    }
    Row {
        spacing: Theme.spacing.md
        Rectangle {
            width: 44; height: 26; radius: 13
            color: backend.previewShell ? Theme.color.primary : Theme.color.surfaceVariant
            Behavior on color { ColorAnimation { duration: Theme.motion.short } }
            Rectangle {
                x: backend.previewShell ? 22 : 3
                y: 3
                width: 20; height: 20; radius: 10
                color: Theme.color.surface
                Behavior on x { NumberAnimation { duration: Theme.motion.short } }
            }
            TapHandler { onTapped: backend.previewShell = !backend.previewShell }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: backend.previewShell ? "已开启（新版外壳）" : "已关闭（经典外壳）"
            font.pixelSize: 13
            color: Theme.color.textSecondary
        }
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
