// 星集控 · 监控中心（2026-10-09 · 独立组件，补齐管理端监控管理面）
//
// 整合被控端摄像头的**管理动作** + 录制回放，替代 Main.qml 里分散的
// 「班级监控卡」位置（等 Main.qml 工作区解禁后挂载）：
//   · 选设备（在线优先）→ 摄像头列表（camera_list）→ 绑定/选择设备
//   · 抓拍一帧（camera_snapshot，device 参数）→ 回显 image://frames/<uid>
//   · 开录/停录（camera_record_start/stop）→ 停后自动上传云端
//   · 本机录制回放（backend.recordings 按 uid 分组）+ 多班缩略图墙
//
// 数据源：
//   backend.devices（设备表）→ backend.sendAction()（指令）→
//   backend.recordings（录制列表）→ backend.subscribeThumbnails()（实时帧）
// 绑定存储：Qt.labs.settings（cameraBind/<uid> = 设备名，与 CameraBindDialog 同key）
//
// 用法（等 Main.qml 解禁后挂）：
//   CameraCenter { theme: root.th; backend: root.backend; width: ...; height: ... }
import QtQuick
import QtQuick.Controls
import Qt.labs.settings

Item {
    id: cc

    property var theme: ({})
    required property var backend

    // 高度语义：Column 里直接放可用（Column 按 implicitHeight 排）。
    // 显式给高度（如嵌入固定卡）时以显式为准。
    implicitHeight: 360
    implicitWidth: 520

    // ── 绑定存储（与 CameraBindDialog 同 key，共用）──
    Settings {
        id: store
        category: "cameraBind"
        property var bindings: ({})
    }

    // ── 状态 ──
    property string selUid: ""          // 选中的设备
    property var cameras: []            // 该设备的摄像头列表
    property bool loadingCams: false
    property bool recording: false      // 本面板是否正在录制
    property string status: ""
    property var recordingGroup: ({})

    // ── 选设备 ──
    Row {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8
        Text {
            text: "监控中心"
            color: cc.theme.fg || "#FAFAFA"
            font.pixelSize: 14
            font.weight: Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
        ComboBox {
            id: devSel
            width: 180
            height: 26
            model: {
                var names = []
                var devs = cc.backend.devices || []
                for (var i = 0; i < devs.length; ++i)
                    if (devs[i] && devs[i].uid) names.push(devs[i].uid)
                return names
            }
            onActivated: {
                cc.selUid = devSel.currentText
                cc.refreshCams()
            }
            Component.onCompleted: {
                if (devSel.model.length > 0) cc.selUid = devSel.model[0]
            }
        }
        Button {
            text: "摄像头列表"
            enabled: cc.selUid !== ""
            onClicked: cc.refreshCams()
        }
        Button {
            text: "抓拍"
            enabled: cc.selUid !== "" && cc.currentDev() !== ""
            onClicked: cc.snapshot()
        }
        Button {
            text: cc.recording ? "停录" : "开录"
            enabled: cc.selUid !== "" && cc.currentDev() !== ""
            onClicked: cc.recording ? cc.stopRec() : cc.startRec()
        }
        Button {
            text: "刷新回放"
            onClicked: { cc.backend.fetchRecordings(); cc.status = "已刷新录制列表" }
        }
    }

    // ── 主体：左＝摄像头绑定 / 右＝回放 ──
    Row {
        anchors.top: parent.top
        anchors.topMargin: 40
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 12

        // 左：摄像头列表 + 绑定
        Rectangle {
            width: parent.width * 0.42
            height: parent.height
            radius: cc.theme.rCard || 12
            color: cc.theme.panel || "#181818"
            border.color: cc.theme.stroke || "#2A2A2A"
            border.width: 1
            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Text {
                    text: "摄像头 · " + cc.selUid
                    color: cc.theme.fg3 || "#8A8A8A"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: parent.width
                }
                // 摄像头列表
                Rectangle {
                    width: parent.width
                    height: 120
                    color: cc.theme.cream || "#141414"
                    radius: 8
                    border.color: cc.theme.stroke || "#2A2A2A"
                    border.width: 1
                    ListView {
                        id: camList
                        anchors.fill: parent
                        anchors.margins: 2
                        clip: true
                        model: cc.cameras
                        delegate: Item {
                            width: camList.width
                            height: 26
                            Rectangle {
                                anchors.fill: parent
                                radius: 4
                                color: camList.currentIndex === index
                                        ? (cc.theme.inv || "#F0F0F0") : "transparent"
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                text: modelData
                                color: camList.currentIndex === index
                                        ? (cc.theme.win || "#0A0A0A") : (cc.theme.fg || "#FAFAFA")
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                width: parent.width - 16
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: camList.currentIndex = index
                            }
                        }
                    }
                }
                Text {
                    text: cc.loadingCams ? "正在枚举…"
                         : (cc.cameras.length === 0 ? "（无摄像头或未枚举：点「摄像头列表」）" : cc.cameras.length + " 个摄像头")
                    color: cc.theme.fg4 || "#707070"
                    font.pixelSize: 10
                }
                Text {
                    text: "当前绑定：" + (cc.boundDevice() || "（未绑定，默认 video0）")
                    color: cc.theme.fg3 || "#8A8A8A"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: parent.width
                }
                Button {
                    text: "绑定选中的摄像头"
                    enabled: camList.currentIndex >= 0
                    onClicked: {
                        if (cc.selUid === "" || camList.currentIndex < 0) return
                        var map = store.bindings || {}
                        map[cc.selUid] = cc.cameras[camList.currentIndex]
                        store.bindings = map
                        cc.status = "已绑定 " + cc.selUid + " → " + cc.cameras[camList.currentIndex]
                    }
                }
            }
        }

        // 右：实时画面 + 回放列表
        Rectangle {
            width: parent.width * 0.58
            height: parent.height
            radius: cc.theme.rCard || 12
            color: cc.theme.panel || "#181818"
            border.color: cc.theme.stroke || "#2A2A2A"
            border.width: 1
            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                // 实时帧（缩略图墙订阅的那台的帧）
                Rectangle {
                    width: parent.width
                    height: 120
                    radius: 8
                    color: cc.theme.cream || "#141414"
                    border.color: cc.theme.stroke || "#2A2A2A"
                    border.width: 1
                    clip: true
                    Image {
                        anchors.fill: parent
                        anchors.margins: 4
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        property int tick: 0
                        source: cc.selUid ? ("image://frames/" + cc.selUid + "?t" + tick) : ""
                        Connections {
                            target: cc.backend
                            function onThumbnailChanged(uid) {
                                if (uid === cc.selUid || uid === "") parent.tick++
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: cc.selUid === "" ? "（选左侧设备看实时画面）" : ""
                        color: cc.theme.fg4 || "#707070"
                        font.pixelSize: 11
                    }
                }
                Text {
                    text: "录制回放（本机）"
                    color: cc.theme.fg3 || "#8A8A8A"
                    font.pixelSize: 11
                }
                // 回放列表（按 uid 分组）
                Flickable {
                    width: parent.width
                    height: parent.height - 200
                    clip: true
                    contentHeight: recCol.height
                    Column {
                        id: recCol
                        width: parent.width
                        spacing: 6
                        Repeater {
                            model: Object.keys(cc.backend.recordings || {}).sort()
                            delegate: Rectangle {
                                width: parent.width
                                height: 30
                                radius: 6
                                color: cc.theme.cream || "#141414"
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    spacing: 8
                                    Text {
                                        text: modelData + " · "
                                        color: cc.theme.fg || "#FAFAFA"
                                        font.pixelSize: 11
                                        width: 130
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: ((cc.backend.recordings[modelData] || []).length || 0) + " 段"
                                        color: cc.theme.fg3 || "#8A8A8A"
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                        Text {
                            visible: Object.keys(cc.backend.recordings || {}).length === 0
                            text: "暂无回放（点「开录」让被控端录一段并自动上传）"
                            color: cc.theme.fg4 || "#707070"
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
    }

    // ── 状态行 ──
    Text {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        text: cc.status
        color: cc.theme.fg4 || "#707070"
        font.pixelSize: 10
        elide: Text.ElideRight
    }

    // ── 逻辑 ──
    function currentDev() {
        if (cc.selUid === "") return ""
        var b = store.bindings || {}
        return b[cc.selUid] || ""
    }
    function boundDevice() { return cc.currentDev() }

    function refreshCams() {
        if (cc.selUid === "") return
        cc.loadingCams = true
        cc.cameras = []
        cc.status = "正在枚举 " + cc.selUid + " 的摄像头…"
        cc.backend.sendAction("camera_list", {})
        // 回执由接线层调 cc.onCams()（Main.qml 现有回执处理转发）
    }
    function onCams(cams) {
        cc.loadingCams = false
        if (Array.isArray(cams) && cams.length) {
            cc.cameras = cams
            cc.status = "发现 " + cams.length + " 个摄像头"
        } else {
            cc.status = "该设备无摄像头或枚举失败"
        }
    }
    function snapshot() {
        var dev = cc.currentDev() || "video0"
        cc.status = "抓拍 " + cc.selUid + "（device=" + dev + "）…"
        cc.backend.sendAction("camera_snapshot", { device: dev })
    }
    function startRec() {
        var dev = cc.currentDev() || "video0"
        cc.status = "正在开录 " + cc.selUid + "…"
        cc.backend.sendAction("camera_record_start", { device: dev })
        cc.recording = true
        cc.backend.subscribeThumbnails([cc.selUid])   // 开启到帧刷新
    }
    function stopRec() {
        cc.status = "已停录（录制自动上传云端，稍后刷新回放）"
        cc.backend.sendAction("camera_record_stop", {})
        cc.recording = false
    }
}