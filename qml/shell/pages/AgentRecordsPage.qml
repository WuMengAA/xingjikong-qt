import QtQuick
import QtQuick.Layouts
import "../../components"
import "../../components/theme"

// 被控端 · 记录页（openNoiseLogDialog + openStudyLogDialog 的真实迁移 · U5-A3 第二批）
//
// 2026-10-31 「组件显示雏形」批次重做呈现层：
//   · 旧版把 C++ 生成的**一段等宽纯文本**塞进 Text —— 现场看到的是两行目录路径 +
//     大片空白，看不出"这里到底有没有记录"。改成「汇总一行 + 按天/按次的行」；
//   · 行与汇总全部由 C++ 算好（noiseSummaryText / noiseRecords …，见
//     src/p3log/p3_record_view.cpp）：QML 不分组不倒序不统计（零业务逻辑铁律）；
//   · 空态用共享件 AppEmptyState（说明"为什么空 + 怎么让它不空"），不再只留一行灰字；
//   · 颜色/圆角/间距一律走 Theme 令牌，无 Hex、无 border.*。
//
// 生成与落盘仍在采集链路（噪音采样 / 自习会话），本页只负责"看"。

Item {
    id: page
    anchors.fill: parent

    property var ctx

    // 打开页面就重读一次：记录是持续写入的，切页回来应当看到最新一天
    Component.onCompleted: agentUi.refresh()

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.height + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 20
            anchors.top: parent.top
            anchors.topMargin: 16
            spacing: 16

            // ── 页头：标题 + 重读 ───────────────────────────────────
            RowLayout {
                width: parent.width
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: "记录"
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        color: Theme.color.textPrimary
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "噪音采样与自习会话的长期留档，按天/按次汇总。"
                        font.pixelSize: 12
                        color: Theme.color.textSecondary
                        wrapMode: Text.Wrap
                    }
                }
                AppButton {
                    text: "刷新"
                    variant: "secondary"
                    size: "sm"
                    onClicked: agentUi.refresh()
                }
            }

            // ── 噪音记录 ───────────────────────────────────────────
            AppCard {
                anchors.left: parent.left
                anchors.right: parent.right
                variant: "muted"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        AppCardTitle {
                            text: "噪音记录"
                            // 空态时不给汇总：否则"还没有噪音记录"在标题副行和空态块里各说一遍
                            supporting: agentUi.noiseRecords.length > 0 ? agentUi.noiseSummaryText : ""
                        }
                        AppButton {
                            text: "打开文件夹"
                            variant: "ghost"
                            size: "sm"
                            visible: agentUi.noiseRecords.length > 0
                            onClicked: agentUi.openNoiseFolder()
                        }
                    }

                    // 每天一行：左=日期，右=采样次数与均值/峰值（两列排好，不在 QML 拼串）
                    Repeater {
                        model: agentUi.noiseRecords
                        delegate: AppInfoRow {
                            required property var modelData
                            label: modelData.label
                            value: modelData.value
                        }
                    }

                    // ⚠️ 错误态与空态**互斥**：读取失败时不能再显示「还没有记录」。旧实现把
                    // 「文件打不开 / 内容全坏」折叠成空，等于在用户记录完好时告诉他"没有"。
                    AppErrorState {
                        visible: agentUi.noiseLoadFailed
                        title: "噪音记录读取失败"
                        reason: agentUi.noiseFailReason
                        impact: agentUi.noiseFailImpact
                        retryText: "重新读取"
                        onRetried: agentUi.refresh()
                    }

                    AppEmptyState {
                        visible: !agentUi.noiseLoadFailed && agentUi.noiseRecords.length === 0
                        title: "还没有噪音记录"
                        description: "开启「噪音监测」或「自习模式」后，每次采样都会记到这里；"
                                   + "开启后本页会按天汇总采样次数与音量区间。"
                        actionText: "打开记录文件夹"
                        onActionClicked: agentUi.openNoiseFolder()
                    }
                }
            }

            // ── 自习记录 ───────────────────────────────────────────
            AppCard {
                anchors.left: parent.left
                anchors.right: parent.right
                variant: "muted"

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        AppCardTitle {
                            text: "自习记录"
                            supporting: agentUi.studyRecords.length > 0 ? agentUi.studySummaryText : ""
                        }
                        AppButton {
                            text: "打开文件夹"
                            variant: "ghost"
                            size: "sm"
                            visible: agentUi.studyRecords.length > 0
                            onClicked: agentUi.openStudyFolder()
                        }
                    }

                    // 每次会话一行：左=开始时间，右=时长与异常录制次数
                    Repeater {
                        model: agentUi.studyRecords
                        delegate: AppInfoRow {
                            required property var modelData
                            label: modelData.label
                            value: modelData.value
                        }
                    }

                    // 同噪音段：错误态与空态互斥（见上方注释）
                    AppErrorState {
                        visible: agentUi.studyLoadFailed
                        title: "自习记录读取失败"
                        reason: agentUi.studyFailReason
                        impact: agentUi.studyFailImpact
                        retryText: "重新读取"
                        onRetried: agentUi.refresh()
                    }

                    AppEmptyState {
                        visible: !agentUi.studyLoadFailed && agentUi.studyRecords.length === 0
                        title: "还没有自习记录"
                        description: "开启「自习模式」后，每次会话的开始/结束、时长与异常录制次数都会记在这里。"
                        actionText: "打开记录文件夹"
                        onActionClicked: agentUi.openStudyFolder()
                    }
                }
            }

            // ── 说明：数据落在哪 ───────────────────────────────────
            Text {
                width: parent.width
                text: "记录按天写在本机数据目录（设置 › 通用 › 目录可定位），只保留最近的数据，"
                    + "不会上传到云端。需要备份时用「打开文件夹」自行拷贝。"
                font.pixelSize: 12
                lineHeight: 1.5
                color: Theme.color.textSecondary
                wrapMode: Text.Wrap
            }
        }
    }
}
