// 被控端 · 设置页分类「通知与音量」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 586-685 行）
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
// ③ 通知与音量：弹窗 / 朗读 / 噪音监测 / VoiceHub 联动
// ─────────────────────────────────────────────────────
    M.Md3Card {
        anchors.left: parent.left
        anchors.right: parent.right
        variant: "filled"
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0
            SecCardTitle { text2: "通知" }
            M.Md3SwitchRow {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "通知与弹窗"
                supporting: "关掉后管理端下发的通知不显示（指令仍会执行）"
                checked: agentUi.notifyEnabled
                onToggled: function (v) { agentUi.notifyEnabled = v }
            }
            M.Md3SwitchRow {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "语音播报（通知朗读）"
                supporting: "用本机 TTS 念通知标题与正文"
                checked: agentUi.voiceEnabled
                onToggled: function (v) { agentUi.voiceEnabled = v }
            }
            M.Md3SwitchRow {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "噪音监测"
                supporting: "采集本机麦克风环境音量；记录在「记录」页"
                checked: agentUi.noiseEnabled
                onToggled: function (v) { agentUi.noiseEnabled = v }
            }
        }
    }

    // VoiceHub 联动（#329）：只读展示服务端的权威广播状态 + 两个偏好开关。
    // ⚠️ 跨模块债务：状态文案仍在 QML 里拼（模块没加载 / 正在播放 / 站内状态三态），
    //    因为把这三态收进 voicehub 模块属于改别的模块，超出本批范围 —— 已记归档。
    M.Md3Card {
        anchors.left: parent.left
        anchors.right: parent.right
        variant: "filled"
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 2
            SecCardTitle { text2: "VoiceHub 联动"; bottomPadding: 6 }
            SecInfoRow {
                label: "连接状态"
                value: {
                    if (!root.vh) return "模块未加载";
                    if (root.vh.active)
                        return "正在播放：" + (root.vh.title || "（未知曲目）");
                    return root.vh.statusText;
                }
            }
            SecInfoRow {
                visible: root.vh && root.vh.active
                label: "下一首"
                value: (root.vh && root.vh.nextTitle) || "（无）"
            }
            // 服务地址：改完即重连（模块内部 stopClient+startClient）
            M.Md3TextField {
                anchors.left: parent.left
                anchors.right: parent.right
                placeholder: "服务地址（默认官方站）"
                text: root.vh ? root.vh.baseUrl : ""
                onEditingFinished: if (root.vh) root.vh.baseUrl = text
            }
            M.Md3SwitchRow {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "开播提醒"
                supporting: "有新广播时灵动岛弹一次（考试模式静默）"
                checked: root.vh ? root.vh.autoNotify : false
                onToggled: function (v) { if (root.vh) root.vh.autoNotify = v }
            }
            M.Md3SwitchRow {
                anchors.left: parent.left
                anchors.right: parent.right
                text: "控制接口（点歌 / 歌词 / 广播）"
                supporting: "留桩：接线后生效，当前只读展示不受影响"
                checked: root.vh ? root.vh.controlEnabled : false
                onToggled: function (v) { if (root.vh) root.vh.controlEnabled = v }
            }
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 6
                text: "控制接口需要 VoiceHub Open API 密钥（x-api-key，songs:request 权限）。" +
                      "密钥存放与审计口径未定之前不接线 —— 开关只是预留。"
                font.pixelSize: 11
                color: root.tok ? root.tok.cOnSurfaceVar : "#CAC4D0"
                wrapMode: Text.WordWrap
            }
        }
    }
}
