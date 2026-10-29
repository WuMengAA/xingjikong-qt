// 监控中心 · 「实时」页（2026-10-29 从 CameraCenter.qml 抽出）
//
// 左＝摄像头列表卡（CameraCenterDevice），右＝实时画面卡（CameraCenterStage）。
// 整页由 main 控制 visible（tabs.currentIndex === 0）。
import QtQuick
import QtQuick.Layouts

Item {
    id: liveRoot
    property var cc

    Row {
        id: liveRow
        anchors.fill: parent
        spacing: cc.tSpM
        CameraCenterDevice { id: devPane; cc: cc; width: parent.width * 0.42; height: parent.height }
        CameraCenterStage { id: stagePane; cc: cc; liveRootVisible: liveRoot.visible; width: parent.width - devPane.width - parent.spacing; height: parent.height }
    }
}
