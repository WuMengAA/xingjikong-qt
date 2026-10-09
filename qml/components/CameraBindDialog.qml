// 星集控 · 管理端摄像头绑定（2026-10-08 · 独立组件，监控功能）
//
// 作用：把「某台被控端机器 ↔ 它的某个摄像头设备名」绑定起来，之后
// 「拍一张/开录」的 device 参数自动用绑定的那个，不用每次手填 video0。
//
// 设计：
//   · 独立 Popup，由主界面弹（Loader 挂进 Main.qml，入口在控制页或命令面板）
//   · 列表走现有通道：backend.sendAction("camera_list") → 回执里 cameras[]
//   · 绑定存 QtQuick.Settings（"cameraBind/<uid>" = 设备名）——QML 侧直接
//     读写，**不动 viewerbackend.cpp**（该文件处于他人工作区）
//   · 「测试」按钮：sendAction("camera_snapshot", {device}) 验证拍得出来
//
// 用法（接入点，等 Main.qml 工作区解禁后加）：
//   CameraBindDialog { theme: root.th; backend: root.backend;
//                      deviceUid: selectedUid; // 被控端 uid
//                      visible: bindDlgOpen }
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.settings

Popup {
    id: dlg

    property var theme: ({})
    required property var backend   // ViewerBackend（sendAction / 回执都在它身上）
    property string deviceUid: ""   // 要绑定的被控端 uid

    // ── 绑定存储：QSettings 写 "cameraBind/<uid>" ──
    Settings {
        id: store
        category: "cameraBind"
        property var bindings: ({})
    }

    property string boundDevice: dlg.deviceUid
                                    ? (store.bindings[dlg.deviceUid] || "")
                                    : ""

    anchors.centerIn: parent
    width: 460
    height: 430
    modal: true
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: dlg.theme.panel || "#101010"
        radius: dlg.theme.rCard || 12
        border.color: dlg.theme.stroke || "#242424"
        border.width: 1
    }

    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Text {
            text: "摄像头绑定 · " + dlg.deviceUid
            color: dlg.theme.fg || "#FAFAFA"
            font.pixelSize: 14
            font.weight: Font.Medium
        }

        Row {
            width: parent.width
            spacing: 8
            Text {
                text: dlg.deviceUid ? ("当前绑定：" + (dlg.boundDevice || "（未绑定，默认 video0）")) : "请先选择被控端"
                color: dlg.theme.fg3 || "#8A8A8A"
                font.pixelSize: 12
                elide: Text.ElideRight
                // 预留宽度：让按钮不被挤没
                Layout.fillWidth: true
                width: parent.width - 90
            }
            Button {
                text: "重新枚举"
                onClicked: dlg.refresh()
            }
        }

        // 摄像头列表（来自 camera_list 回执）
        Rectangle {
            width: parent.width
            height: 180
            color: dlg.theme.cream || "#141414"
            radius: 8
            border.color: dlg.theme.stroke || "#242424"
            border.width: 1

            ListView {
                id: camList
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: dlg.cameras
                delegate: Item {
                    width: camList.width
                    height: 32
                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: dlg.selectedCam === modelData
                                ? (dlg.theme.inv || "#F0F0F0")
                                : "transparent"
                        border.color: dlg.selectedCam === modelData
                                      ? (dlg.theme.inv || "#F0F0F0") : "transparent"
                        border.width: 1
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        text: modelData
                        color: dlg.selectedCam === modelData
                                ? (dlg.theme.win || "#0A0A0A")
                                : (dlg.theme.fg || "#FAFAFA")
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        width: parent.width - 20
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: dlg.selectedCam = modelData
                    }
                }
            }
        }

        Text {
            visible: dlg.cameras.length === 0
            text: dlg.loading ? "正在枚举摄像头…" : "（枚举失败或无摄像头；先点「重新枚举」）"
            color: dlg.theme.fg4 || "#707070"
            font.pixelSize: 11
        }

        Row {
            width: parent.width
            spacing: 8
            Button {
                text: dlg.selectedCam ? "绑定：" + dlg.selectedCam : "选一个摄像头"
                enabled: dlg.selectedCam !== ""
                onClicked: dlg.bindSelected()
                Layout.fillWidth: true
                width: parent.width * 0.55
            }
            Button {
                text: "测试拍照"
                enabled: (dlg.boundDevice !== "" || dlg.selectedCam !== "")
                onClicked: dlg.testShot()
                Layout.fillWidth: true
                width: parent.width * 0.4
            }
        }

        Text {
            id: statusTxt
            width: parent.width
            text: dlg.status
            color: dlg.statusColor
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
    }

    // ── 状态 ──
    property var cameras: []
    property string selectedCam: ""
    property string status: ""
    property color statusColor: (dlg.theme.fg4 || "#707070")
    property bool loading: false

    function refresh() {
        dlg.selectedCam = ""
        dlg.status = ""
        dlg.loading = true
        dlg.cameras = []
        dlg.statusColor = dlg.theme.fg4 || "#707070"
        if (!dlg.backend || !dlg.deviceUid) {
            dlg.status = "没有可用的被控端（deviceUid 为空）"
            dlg.loading = false
            return
        }
        // 现有通道：sendAction → 云端 → 被控端 camera_list → 回执
        dlg.backend.sendAction("camera_list", {})
    }

    function bindSelected() {
        if (dlg.selectedCam === "") return
        const map = store.bindings || {}
        map[dlg.deviceUid] = dlg.selectedCam
        store.bindings = map
        dlg.status = "已绑定：" + dlg.deviceUid + " → " + dlg.selectedCam
        dlg.statusColor = dlg.theme && (dlg.theme.fg || "#FAFAFA")
    }

    function testShot() {
        const dev = dlg.selectedCam !== "" ? dlg.selectedCam : dlg.boundDevice
        if (!dev) { dlg.status = "请先选摄像头"; return }
        dlg.status = "正在拍照测试（device=" + dev + "）…"
        dlg.statusColor = dlg.theme.fg4 || "#707070"
        dlg.backend.sendAction("camera_snapshot", { device: dev })
    }

    // 由主界面把 camera_list 回执的 cameras 喂进来（qml 端 connect 回执）
    function onListResult(cams) {
        dlg.loading = false
        if (Array.isArray(cams) && cams.length) {
            dlg.cameras = cams
            dlg.status = "发现 " + cams.length + " 个摄像头"
        } else {
            dlg.cameras = []
            dlg.status = "没有摄像头（或被控端不支持枚举）"
            dlg.statusColor = (dlg.theme ? (dlg.theme.fg3 || "#8A8A8A") : "#8A8A8A")
        }
    }

    // 由主界面把 camera_snapshot 回执状态喂进来
    function onShotResult(ok, detail) {
        dlg.status = ok ? ("拍照成功：" + (detail || "已保存")) : ("拍照失败：" + (detail || "未知原因"))
        dlg.statusColor = ok ? (dlg.theme && (dlg.theme.fg || "#FAFAFA"))
                             : (dlg.theme ? (dlg.theme.fg3 || "#8A8A8A") : "#8A8A8A")
    }
}