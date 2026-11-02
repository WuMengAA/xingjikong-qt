// 被控端 · 设置页分类「更新」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 767-869 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// 2026-10-30 用户点单「全部换新」：ash/ → 共享库 App* 体系，令牌走 Theme 单例。
//   · 渠道按钮的选中态仍由 C++ 定（`channelOptionList` 的 variant 字段），
//     但词汇由 ash 的 "filled/tonal" 改为 AppButton 的 "solid/secondary"（同步改 C++）。
//   · 「临时」角标的两个 Hex（#F59E0B / #3B2600）→ `Theme.color.warning` / `Theme.color.background`。
//   · 原 `Row { topPadding: 10 }` 无效（Row 无 padding）→ RowLayout + Layout.topMargin。
//
// ⚠️ QML 零逻辑（用户铁律）：本件不含 Math / 三元 / function / console.log / 通信，
//    所有值都来自 agentUi（C++）的结果属性。本拆分是**纯结构移动，零行为变化**。
import QtQuick
import QtQuick.Layouts
import Stelarith.Ota 1.0          // OtaChannelState 单例（渠道晋升进度；C++ 静态注册）

import "../../../components"
import "../../../components/theme"

Column {
    id: root

    property var ctx
    property var vh

    width: parent ? parent.width : 0
    spacing: 14

// ─────────────────────────────────────────────────────
// ⑤ 更新：自动升级台账 + 渠道（含临时切换）
// ─────────────────────────────────────────────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            AppCardTitle { text: "自动升级" }
            AppInfoRow {
                label: "升级方式"
                value: agentUi.otaModeText
                valueColor: agentUi.otaModeColor
            }
            AppInfoRow {
                label: "自动升级"
                value: agentUi.otaAutoText
                valueColor: agentUi.otaAutoColor
            }
            AppInfoRow { label: "上次自动升级"; value: agentUi.otaLastText }
            AppInfoRow { label: "下次自动升级"; value: agentUi.otaNextText }
            RowLayout {
                Layout.topMargin: 10
                spacing: 10
                AppButton {
                    text: "立即升级"
                    variant: "solid"
                    enabled: agentUi.canUpgrade
                    onClicked: agentUi.upgradeNow()
                }
                AppButton {
                    // 文案随状态变，值由 C++ 给（pauseBtnText）
                    text: agentUi.pauseBtnText
                    variant: "secondary"
                    onClicked: agentUi.toggleOtaPause()
                }
                AppButton {
                    text: "重新检查"
                    variant: "ghost"
                    onClicked: agentUi.refresh()
                }
            }
        }
    }

    // 更新渠道（2026-11-02 转正：临时测试开关 → 正式渠道类）
    // 三档 = alpha 灰度 / beta 测试 / stable 正式，与云端渠道自动晋升同表：
    // 灰度发布即可见 → 满 5 小时进测试 → 满 24 小时进正式。**版本号与安装包都不变**，
    // 变的只是"此刻哪些设备能拿到这一版"。
    // 生效面：g_channel（register 上报 + OTA 查询）+ channel.txt（重启/升级后仍生效）。
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            RowLayout {
                spacing: 8
                Text {
                    text: "更新渠道"
                    font.pixelSize: 14
                    font.weight: Font.Medium
                    color: Theme.color.textPrimary
                }
                // 2026-11-02：渠道从「临时测试开关」转正，撤掉「临时」角标。
                Item { Layout.fillWidth: true }
            }
            AppInfoRow { label: "当前渠道"; value: agentUi.updateChannel }
            // 渠道自动晋升进度（2026-11-02）：构建与版本号都不变，变的只是"此刻在哪一档"。
            // 文案整句由 C++ 拼（ota_channel_state.cpp），QML 零计算；旧云端没这字段时
            // promotionText 为空串 ⇒ 整行自动隐藏，不留一个"--"吓唬现场。
            AppInfoRow {
                visible: OtaChannelState.promotionText.length > 0
                label: "晋升进度"
                value: OtaChannelState.promotionText
            }
            RowLayout {
                spacing: 8
                Repeater {
                    model: agentUi.channelOptions
                    AppButton {
                        text: modelData.label
                        variant: modelData.variant
                        onClicked: agentUi.updateChannel = modelData.key
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: agentUi.channelNoteText
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
        }
    }
}
