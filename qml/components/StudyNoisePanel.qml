// 监控中心 · 自习页 · 噪音电平 + 最近样本条带（2026-10-30 从 CameraCenterStudy 拆出）
// 原样保留：大字 dB + 电平条（白线=阈值）、每拍一柱的最近样本条带。
// 全部经 `cc`（CameraController）取状态/令牌。
import QtQuick
import QtQuick.Layouts

Column {
    id: panel
    property var cc
    width: parent.width
    spacing: cc.tSpM

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
}
