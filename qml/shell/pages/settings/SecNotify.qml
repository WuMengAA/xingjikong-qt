// 被控端 · 设置页分类「通知与音量」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 586-685 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// 2026-10-30 用户点单「全部换新」：ash/ → 共享库 App* 体系，令牌走 Theme 单例；
// AshInput → AppInput（同样是无边框自绘，但高度 40 且聚焦靠底色变化而非描边）。
//
// ⚠️ QML 零逻辑（用户铁律）：本件不含 Math / 三元 / function / console.log / 通信，
//    所有值都来自 agentUi（C++）的结果属性。本拆分是**纯结构移动，零行为变化**。
import QtQuick
import QtQuick.Layouts

import Stelarith.Mic 1.0            // MicSelector 单例（麦克风枚举/切换，C++ 侧算好文案）

import "../../../components"
import "../../../components/theme"

Column {
    id: root

    property var ctx
    property var vh


    width: parent ? parent.width : 0
    spacing: 14

// ─────────────────────────────────────────────────────
// ③ 通知与音量：弹窗 / 朗读 / 噪音监测 / VoiceHub 联动
// ─────────────────────────────────────────────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            AppCardTitle { text: "通知" }
            AppSwitchRow {
                text: "通知与弹窗"
                supporting: "关掉后管理端下发的通知不显示（指令仍会执行）"
                checked: agentUi.notifyEnabled
                onToggled: function (v) { agentUi.notifyEnabled = v }
            }
            AppSwitchRow {
                text: "语音播报（通知朗读）"
                supporting: "用本机 TTS 念通知标题与正文"
                checked: agentUi.voiceEnabled
                onToggled: function (v) { agentUi.voiceEnabled = v }
            }
            AppSwitchRow {
                text: "噪音监测"
                supporting: "采集本机麦克风环境音量；记录在「记录」页"
                checked: agentUi.noiseEnabled
                onToggled: function (v) { agentUi.noiseEnabled = v }
            }
        }
    }

    // 麦克风选择（2026-10-31 用户点单「麦克风切换」）：采集此前写死系统默认麦。
    // 数据面全在 C++（Stelarith.Mic 单例：waveIn 枚举 + QSettings 持久化），
    // 本件零逻辑：文案与"当前使用"由 C++ 的 options 算好，这里只列出来、点一下调 select()。
    // 课中换麦当场生效（采集实例在跑），课前换麦下次采集生效。
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            AppCardTitle { text: "噪音采集麦克风" }
            Text {
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                text: MicSelector.statusText
                color: Theme.color.textSecondary
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            Repeater {
                model: MicSelector.options
                delegate: AppListItem {
                    required property var modelData
                    Layout.fillWidth: true
                    headline: modelData.name
                    supporting: modelData.supporting
                    onClicked: MicSelector.select(modelData.id)
                }
            }
        }
    }

    // VoiceHub 联动（#329）：只读展示服务端的权威广播状态 + 两个偏好开关。
    // ⚠️ 跨模块债务：状态文案仍在 QML 里拼（模块没加载 / 正在播放 / 站内状态三态），
    //    因为把这三态收进 voicehub 模块属于改别的模块，超出本批范围 —— 已记归档。
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            AppCardTitle { text: "VoiceHub 联动" }
            AppInfoRow {
                label: "连接状态"
                value: {
                    if (!root.vh) return "模块未加载";
                    if (root.vh.active)
                        return "正在播放：" + (root.vh.title || "（未知曲目）");
                    return root.vh.statusText;
                }
            }
            AppInfoRow {
                visible: root.vh && root.vh.active
                label: "下一首"
                value: (root.vh && root.vh.nextTitle) || "（无）"
            }
            // 服务地址：改完即重连（模块内部 stopClient+startClient）
            AppInput {
                Layout.fillWidth: true
                placeholder: "服务地址（默认官方站）"
                text: root.vh ? root.vh.baseUrl : ""
                onAccepted: {
                    if (root.vh) root.vh.baseUrl = text
                }
            }
            AppSwitchRow {
                text: "开播提醒"
                supporting: "有新广播时灵动岛弹一次（考试模式静默）"
                checked: root.vh ? root.vh.autoNotify : false
                onToggled: function (v) { if (root.vh) root.vh.autoNotify = v }
            }
            AppSwitchRow {
                text: "控制接口（点歌 / 歌词 / 广播）"
                supporting: "留桩：接线后生效，当前只读展示不受影响"
                checked: root.vh ? root.vh.controlEnabled : false
                onToggled: function (v) { if (root.vh) root.vh.controlEnabled = v }
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: "控制接口需要 VoiceHub Open API 密钥（x-api-key，songs:request 权限）。" +
                      "密钥存放与审计口径未定之前不接线 —— 开关只是预留。"
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
        }
    }
}
