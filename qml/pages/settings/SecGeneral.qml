// 系统设置 · 分类「通用」（2026-10-28 模块三：按用户蓝图重做设置页）
//
// 内容 = 连接信息（沿用既有 SecConnection，真源只有一份）+ 启动项 + 本地目录。
// 蓝图里「通用」的原始条目是"开机自启 / url 配置 / 安全模式 / 时间配置 / 存储与备份 /
// 高级选项"——本件落地能落的那几条，其余在下面逐条说明为什么还是占位。
//
// 为什么用 Column 而不是单张卡：通用这一栏天生是「三块互不相干的信息」，
// 塞进一张卡会变成三层标题，反而更难扫。
import QtQuick

import "../../components"
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

Column {
    id: root

    width: parent ? parent.width : 0
    spacing: 14

    property var ctx

    // ① 连接（复用既有 Section：云端地址 / 状态 / 账号行 + 未登录时的登录入口）
    SecConnection {
        width: root.width
        ctx: root.ctx
    }

    // ② 启动项
    SettingsCard {
        width: root.width
        title: "启动"
        subtitle: "与「个人中心」里的同名开关是同一份设置，改哪边都一样"
        gap: 10

        Text {
            width: parent.width
            text: "开机自启与桌面快捷方式在「个人中心 › 启动」里改 —— 那里和这里读写同一份设置。"
            wrapMode: Text.Wrap
            color: Theme.color.textSecondary
            font.pixelSize: 13
        }
    }

    // ③ 本地目录与存储
    SettingsCard {
        width: root.width
        title: "本地数据"
        subtitle: "配置、日志与缓存都在用户目录下，卸载不会自动清理"
        gap: 10

        Text {
            width: parent.width
            text: "配置与日志位于 %LOCALAPPDATA%\\xingjikong\\；" +
                  "缓存（缩略图、课表档案副本）在 %LOCALAPPDATA%\\StarLiith\\viewer\\ 下。" +
                  "换机迁移时带上这两个目录即可保留全部本机设置。"
            wrapMode: Text.Wrap
            color: Theme.color.textSecondary
            font.pixelSize: 13
        }
    }

    // ④ 蓝图里"还没做"的那几条，说清楚而不是摆假开关
    SettingsCard {
        width: root.width
        title: "尚未接入"
        subtitle: "蓝图列了但代码里确实还没有的项 —— 摆假开关比不摆更糟"
        gap: 8

        Text {
            width: parent.width
            text: "· 云端 URL 可视化配置：当前走 agent.env / 首次引导写入，界面里不提供文本框" +
                  "（改错地址会让整台设备失联，不适合放在设置页一键改）。\n" +
                  "· 安全模式（崩溃后自动降级启动）：未实现。\n" +
                  "· 时间源配置：已有 NTP 校时与课表对时，但没有暴露成可配项。\n" +
                  "· 存储与备份（导出/导入设置包）：未实现。"
            wrapMode: Text.Wrap
            color: Theme.color.textSecondary
            font.pixelSize: 12
            lineHeight: 1.5
        }
    }
}
