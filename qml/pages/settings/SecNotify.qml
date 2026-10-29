// 系统设置 · 分类「通知与音量」（2026-10-28 模块三：按用户蓝图重做设置页）
//
// 蓝图条目：提供方列表（地震预警 / 上下课提醒 / 天气 / 放学 / 行动 / 广播，可单独开关）；
//           音量、TTS 说话人、通知音效；通知方式（自定义）。
//
// ⚠️ 这一栏**一个真开关都没有**：代码里不存在"按提供方分流"的机制 ——
//    当前所有通知走同一条 `notify` 指令（notifyFromParams 里的黑名单/考试模式是全局的，
//    不分提供方）。所以这里只列清单 + 说清现状，不摆六个点了没反应的 switch。
//    真要做得先在被控端 notifyFromParams 前面加一层"按 from 字段过滤"，
//    那是被控端改造，不在设置页这一层。
import QtQuick

import "../../components"
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

SettingsCard {
    id: card

    title: "通知与音量"
    subtitle: "哪些提醒能弹、弹出来念不念"
    gap: 12

    property var ctx

    Text {
        width: parent.width
        text: "当前状态"
        color: Theme.color.textSecondary
        font.pixelSize: 13
        font.weight: Font.Medium
    }
    Text {
        width: parent.width
        text: "· 通知统一走一条通道：管理端下发什么，设备就弹什么（受考试模式与本机总开关约束）。\n" +
              "· 语音播报（TTS）的开关在**被控端**的设置页里 —— 那台设备念不念，由那台设备决定。\n" +
              "· 本机（管理端）不播放通知音；它只负责下发。"
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
        text: "蓝图里的提供方清单（尚未接入）"
        color: Theme.color.textSecondary
        font.pixelSize: 13
        font.weight: Font.Medium
    }
    Text {
        width: parent.width
        text: "地震预警 · 上下课提醒 · 天气 · 放学 · 行动 · 广播 —— " +
              "这六类目前**没有**独立的开关或分流逻辑：设备侧收到的都是同一种 notify，" +
              "没有 from 字段可用于过滤。\n\n" +
              "要落地需要两步（都没做）：① 被控端 notifyFromParams 前加按 from 过滤的一层；" +
              "② 云端下发时带上 provider 字段。两步都做完，这个页面才谈得上放开关。"
        wrapMode: Text.Wrap
        color: Theme.color.textSecondary
        font.pixelSize: 12
        lineHeight: 1.5
    }
}
