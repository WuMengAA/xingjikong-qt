// 监控中心 · 「自习」页（2026-10-13 噪音异常自动录制；2026-10-29 抽出）
// 原样保留：噪音检测阈值/连续拍数/录制时长配置、当前噪音电平条、最近样本条带、异常记录列表。
// 全部经 `cc` 取状态/调逻辑。
import QtQuick
import QtQuick.Layouts
import "../island"
import "md3"

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
            Md3Btn {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                radius: cc.tRM
                variant: cc.backend.studyActive ? "outlined" : "filled"
                text: cc.backend.studyActive ? "关闭自习" : "开启自习"
                enabled: cc.selUid !== ""
                tip: cc.selUid === "" ? "先在「实时」页选一台设备"
                                      : "周期测教室噪音；连续超阈自动录一段摄像头并上传"
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

        // ③ 当前噪音：大字 dB + 电平条（白线=阈值）
        Rectangle {
            width: parent.width; height: 56
            radius: cc.tRM
            color: cc.tStage
            Row {
                anchors.left: parent.left
                anchors.leftMargin: cc.tSpM
                anchors.verticalCenter: parent.verticalCenter
                spacing: cc.tSpS
                Text {
                    text: cc.backend.noiseCaptureOk
                          ? (cc.backend.noiseDb <= -90 ? "静" : cc.backend.noiseDb.toFixed(1))
                          : "—"
                    color: cc.tFg
                    font.pixelSize: 20
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "dB"; color: cc.tFg3; font.pixelSize: cc.tLabelM
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: !cc.backend.noiseCaptureOk ? "采不到（无麦或被语音占用）"
                        : (cc.backend.noiseActive ? "偏高" : "正常")
                    color: cc.backend.noiseActive ? cc.tErr : cc.tFg3
                    font.pixelSize: cc.tLabelM
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Item {
                anchors.right: parent.right
                anchors.rightMargin: cc.tSpM
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * 0.40; height: 10
                Rectangle {
                    anchors.fill: parent; radius: 5
                    color: cc.tOutlineVar
                }
                Rectangle {
                    height: parent.height; radius: 5
                    width: parent.width * Math.max(0, Math.min(1, (cc.backend.noiseDb + 96) / 96))
                    color: cc.backend.noiseActive ? cc.tErr : cc.tAcc
                }
                Rectangle {
                    x: parent.width * Math.max(0, Math.min(1, (cc.studyThresholdDb + 96) / 96)) - 1
                    width: 2; height: parent.height + 6; y: -3
                    color: cc.tFg
                    opacity: 0.75
                }
            }
        }

        // ④ 最近噪音样本条带
        Text { text: "最近噪音（每拍一柱，白线=阈值）"; color: cc.tFg4; font.pixelSize: cc.tLabelS }
        Row {
            width: parent.width
            height: 38
            spacing: 1
            Repeater {
                model: cc.backend.noiseSamples
                delegate: Item {
                    width: Math.max(1, (parent.width - (cc.backend.noiseSamples.length || 1)) / Math.max(1, cc.backend.noiseSamples.length))
                    height: parent.height
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * Math.max(0, Math.min(1, (modelData.db + 96) / 96))
                        color: modelData.active ? cc.tErr : cc.tAcc
                        opacity: 0.85
                    }
                }
            }
            Text {
                visible: (cc.backend.noiseSamples.length || 0) === 0
                text: "（开启自习模式后才有数据）"
                color: cc.tFg4; font.pixelSize: cc.tLabelS
                anchors.verticalCenter: parent.verticalCenter
            }
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
