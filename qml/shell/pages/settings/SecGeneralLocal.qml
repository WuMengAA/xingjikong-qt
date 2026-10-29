// 被控端 · 设置页分类「通用 · 存储·高级·目录」（2026-10-28 拆分：原 AgentSettingsPage.qml 第 373-510 行）
//
// 拆分动机：主文件到 1016 行，台账拆分计划已升 [P0]；按蓝图再补四类会到 1500+。
// 拆法照 viewer 侧 pages/settings/Sec*.qml 的同构做法 —— 主文件只留
// 分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
//
// 「通用」拆成三件的原因：单件 337 行超 QML 红线 200（file-size 门禁判红），
// 按语义切为 设备与启动 / 服务与时间 / 存储·高级·目录，三段在主文件里按原顺序排列。
//
// 2026-10-30 用户点单「全部换新」：ash/ → 共享库 App* 体系，令牌走 Theme 单例。
//   · 动作结果条的 `#211F26` / `#E6E1E5` 两个 Hex → `Theme.color.surfaceVariant` / `textPrimary`；
//   · 说明文字统一 12 号次文字色（原为 11 号 + 兜底 Hex #CAC4D0）；
//   · 原 `Row { topPadding: 10 }` 无效（Row 无 padding）→ RowLayout + Layout.topMargin。
//
// ⚠️ QML 零逻辑（用户铁律）：本件不含 Math / 三元 / function / console.log / 通信，
//    所有值都来自 agentUi（C++）的结果属性。本拆分是**纯结构移动，零行为变化**。
import QtQuick
import QtQuick.Layouts

import "../../../components"
import "../../../components/theme"

Column {
    id: root

    property var ctx
    property var vh
    signal resetRequested()

    width: parent ? parent.width : 0
    spacing: 14

    // ── 蓝图「通用设置」第 5 项：存储与备份 ──────────────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            AppCardTitle { text: "存储与备份" }
            Text {
                Layout.fillWidth: true
                text: agentUi.storageHintText
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            AppInfoRow { label: "配置"; value: agentUi.storageConfigText }
            AppInfoRow { label: "数据"; value: agentUi.storageDataText }
            AppInfoRow { label: "日志"; value: agentUi.storageLogText }
            AppInfoRow { label: "合计"; value: agentUi.storageTotalText }
            RowLayout {
                Layout.topMargin: 10
                spacing: 8
                AppButton {
                    text: "统计用量"
                    variant: "solid"
                    onClicked: agentUi.refreshStorage()
                }
                AppButton {
                    text: "备份配置"
                    variant: "secondary"
                    onClicked: agentUi.backupConfigNow()
                }
                AppButton {
                    text: "清理旧日志"
                    variant: "ghost"
                    onClicked: agentUi.clearOldLogsNow()
                }
            }
        }
    }

    // ── 蓝图「通用设置」第 6 项：高级选项 ────────────────────────
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            AppCardTitle { text: "高级选项" }
            Text {
                Layout.fillWidth: true
                text: agentUi.resetScopeText
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            AppButton {
                text: "重置全部设置…"
                variant: "secondary"
                onClicked: root.resetRequested()
            }
        }
    }

    // ── 动作结果条：保存地址/对时/备份/清理/重置 统一在这里反馈 ──────
    // 没有它的话，点「备份配置」这类动作现场看不出任何变化（用户最反感
    // 的"点了没反应"）。显隐由 C++ 的 actionResultVisible 决定。
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        visible: agentUi.actionResultVisible
        height: resultText.implicitHeight + 20
        radius: Theme.radius.md
        color: Theme.color.surfaceVariant
        Text {
            id: resultText
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 12
            text: agentUi.actionResultText
            font.pixelSize: 12
            color: Theme.color.textPrimary
            wrapMode: Text.WordWrap
        }
    }

    // 托盘右键的三条目录入口（2026-10-28 移植）
    AppCard {
        anchors.left: parent.left
        anchors.right: parent.right
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            AppCardTitle { text: "本地目录" }
            Text {
                Layout.fillWidth: true
                text: "配置文件、运行数据与日志分三个位置存放。托盘右键里原本也有这三项，" +
                      "现在设置页同样能到 —— 现场排障不用再去摸托盘。"
                font.pixelSize: 12
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            AppInfoRow { label: "数据目录"; value: agentUi.dataDir }
            AppInfoRow { label: "日志文件"; value: agentUi.logPath }
            RowLayout {
                spacing: 10
                AppButton {
                    text: "打开配置目录"
                    variant: "secondary"
                    onClicked: agentUi.openConfigDir()
                }
                AppButton {
                    text: "打开数据目录"
                    variant: "secondary"
                    onClicked: agentUi.openDataDir()
                }
                AppButton {
                    text: "打开日志目录"
                    variant: "secondary"
                    onClicked: agentUi.openLogDir()
                }
            }
        }
    }
}
