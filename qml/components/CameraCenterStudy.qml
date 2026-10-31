// 监控中心 · 「自习」页（2026-10-13 噪音异常自动录制；2026-10-29 抽出）
// 原样保留：噪音检测阈值/连续拍数/录制时长配置、当前噪音电平条、最近样本条带、异常记录列表。
// 2026-10-31：加「语音提醒安静」按钮（管理端联动，见 StudyNoiseReminder）。
// 全部经 `cc` 取状态/调逻辑。
import QtQuick
import QtQuick.Layouts
import "../island"

Rectangle {
    id: studyCard
    property var cc
    anchors.fill: parent
    color: cc.tCard
    radius: cc.tRM

    Column {
        id: studyTop
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: cc.tSpM
        anchors.leftMargin: cc.tSpM
        anchors.rightMargin: cc.tSpM
        spacing: cc.tSpM

        // ① 标题 + 状态 + 开关
        Item {
            width: parent.width
            height: 36
            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: cc.tSpS
                Text {
                    text: "自习模式"
                    color: cc.tFg
                    font.pixelSize: cc.tTitleS
                    font.weight: Font.Medium
                    anchors.verticalCenter: parent.verticalCenter
                }
                Rectangle {
                    width: 52
                    height: 20
                    radius: cc.tRM
                    color: cc.backend.studyActive ? cc.tSegBg : cc.tCardHi
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        anchors.centerIn: parent
                        text: cc.backend.studyActive ? "监测中" : "未开启"
                        color: cc.backend.studyActive ? cc.tSegFg : cc.tFg3
                        font.pixelSize: cc.tLabelS
                    }
                }
                Text {
                    text: cc.selUid === "" ? "（先在「实时」页选设备）" : "· " + cc.deviceName(cc.selUid)
                    color: cc.tFg3
                    font.pixelSize: cc.tLabelM
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            AppButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                radius: 12
                variant: cc.backend.studyActive ? "secondary" : "primary"
                text: cc.backend.studyActive ? "关闭自习" : "开启自习"
                enabled: cc.selUid !== ""
                onClicked: {
                    if (cc.backend.studyActive) {
                        cc.backend.stopStudyMode()
                    } else {
                        cc.backend.startStudyMode({
                            "thresholdDb": cc.studyThresholdDb,
                            "streakNeed": cc.studyStreakNeed,
                            "recordSec": cc.studyRecordSec,
                            "intervalMs": 2000
                        })
                    }
                }
            }
        }

        // ② 配置行：阈值 / 连续拍数 / 录制时长
        Row {
            spacing: cc.tSpS
            Text { text: "阈值"; color: cc.tFg3; font.pixelSize: cc.tLabelM; anchors.verticalCenter: parent.verticalCenter }
            InputField {
                id: thIn
                th: cc.theme
                width: 64; height: 28
                text: String(cc.studyThresholdDb)
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onEditingFinished: {
                    var v = parseFloat(thIn.text)
                    if (!isNaN(v) && v <= 0 && v >= -96) { cc.studyThresholdDb = v; cc.status = "阈值 → " + v + " dB" }
                    else thIn.text = String(cc.studyThresholdDb)
                }
            }
            Text { text: "dB"; color: cc.tFg4; font.pixelSize: cc.tLabelM; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "连续"; color: cc.tFg3; font.pixelSize: cc.tLabelM; anchors.verticalCenter: parent.verticalCenter }
            InputField {
                id: stIn
                th: cc.theme
                width: 44; height: 28
                text: String(cc.studyStreakNeed)
                inputMethodHints: Qt.ImhDigitsOnly
                onEditingFinished: {
                    var v = parseInt(stIn.text)
                    if (!isNaN(v) && v >= 1 && v <= 60) cc.studyStreakNeed = v
                    else stIn.text = String(cc.studyStreakNeed)
                }
            }
            Text { text: "拍后录"; color: cc.tFg4; font.pixelSize: cc.tLabelM; anchors.verticalCenter: parent.verticalCenter }
            InputField {
                id: rsIn
                th: cc.theme
                width: 44; height: 28
                text: String(cc.studyRecordSec)
                inputMethodHints: Qt.ImhDigitsOnly
                onEditingFinished: {
                    var v = parseInt(rsIn.text)
                    if (!isNaN(v) && v >= 5 && v <= 300) cc.studyRecordSec = v
                    else rsIn.text = String(cc.studyRecordSec)
                }
            }
            Text { text: "秒"; color: cc.tFg4; font.pixelSize: cc.tLabelM; anchors.verticalCenter: parent.verticalCenter }
        }

        // ②b 管理端联动（2026-10-31）：语音提醒安静（StudyNoiseReminder 独立件保持 ≤200 行）
        StudyNoiseReminder { cc: studyCard.cc }

        // ③④ 噪音电平 + 最近样本条带（已拆出为 StudyNoisePanel）
        StudyNoisePanel {
            cc: studyCard.cc
        }

        Text { text: "异常记录"; color: cc.tFg4; font.pixelSize: cc.tLabelS }
    }

    // ⑤ 异常记录列表（占满剩余高度）
    Flickable {
        anchors.top: studyTop.bottom
        anchors.topMargin: cc.tSpS
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: cc.tSpM
        anchors.rightMargin: cc.tSpM
        anchors.bottomMargin: cc.tSpM
        clip: true
        contentHeight: altCol.height
        Column {
            id: altCol
            width: parent.width
            spacing: cc.tSpXS
            Repeater {
                model: cc.backend.noiseAlerts
                delegate: Rectangle {
                    width: altCol.width
                    height: 26
                    radius: cc.tRM
                    color: cc.tCardHi
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: altMsgTx.left
                        anchors.rightMargin: cc.tSpS
                        anchors.verticalCenter: parent.verticalCenter
                        text: Qt.formatTime(new Date(modelData.t), "hh:mm:ss")
                        color: cc.tFg4; font.pixelSize: cc.tLabelS
                    }
                    Text {
                        id: altMsgTx
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width - 110)
                        text: modelData.event === "record_start"
                              ? "检测到噪音异常 · 开始录制"
                              : (modelData.ok ? "录制完成（" + Math.round((modelData.bytes || 0) / 1024) + " KB，已上传）"
                                              : "录制失败：" + (modelData.reason || "未知"))
                        color: (modelData.event === "record_stop" && modelData.ok === false)
                               ? cc.tErr : cc.tFg
                        font.pixelSize: cc.tLabelS
                        elide: Text.ElideRight
                    }
                }
            }
            Text {
                visible: (cc.backend.noiseAlerts.length || 0) === 0
                text: "暂无异常（噪音连续超阈会自动录一段并记在这里）"
                color: cc.tFg4; font.pixelSize: cc.tLabelS
            }
        }
    }
}
