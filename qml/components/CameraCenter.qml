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
    property string boundDev: ""        // 被控端回执的当前绑定（权威；空 = 未绑定）

    // ── 表头：第一行＝标题 + 设备选择；第二行＝动作按钮 ──
    // ⚠️ 2026-10-09 重做：原来一行硬塞「标题 + 原生 ComboBox + 4 个原生 Button」，
    //    ① 宽度必然溢出（520 的容器塞 ~575 的内容）—— 就是用户看到的"没放对位置"；
    //    ② 原生 ComboBox/Button 走**系统调色板**，暗色界面里渲染成白底（"杂乱"的元凶）。
    //    项目铁律：不用 Qt 原生 Button/ComboBox/TextField，一律走 Btn / 自绘。
    Column {
        id: head
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 8

        // 第一行：标题（固定宽）+ 设备选择（吃掉剩余宽度）
        Item {
            width: parent.width
            height: 30
            Text {
                id: headTitle
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "监控中心"
                color: cc.theme.fg || "#FAFAFA"
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            // 设备选择：自绘（Btn 同款长相）+ Popup 下拉，避开原生 ComboBox
            Rectangle {
                id: devSel
                anchors.left: headTitle.right
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 30
                radius: cc.theme.rCtrl !== undefined ? cc.theme.rCtrl : 8
                color: devMa.containsMouse ? (cc.theme.hover || "#1E1E1E") : "transparent"
                border.color: cc.theme.stroke || "#2A2A2A"
                border.width: 1

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: caret.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: cc.selUid !== "" ? cc.selUid : (cc.deviceUids().length === 0 ? "（无在线设备）" : "请选择设备")
                    color: cc.theme.fg || "#FAFAFA"
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                Text {
                    id: caret
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"
                    color: cc.theme.fg3 || "#8A8A8A"
                    font.pixelSize: 12
                }
                MouseArea {
                    id: devMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: devMenu.opened ? devMenu.close() : devMenu.open()
                }
                Popup {
                    id: devMenu
                    x: 0
                    y: devSel.height + 4
                    width: devSel.width
                    padding: 4
                    background: Rectangle {
                        color: cc.theme.panel || "#181818"
                        radius: cc.theme.rCtrl !== undefined ? cc.theme.rCtrl : 8
                        border.color: cc.theme.stroke || "#2A2A2A"
                        border.width: 1
                    }
                    contentItem: ListView {
                        implicitHeight: Math.min(contentHeight, 220)
                        clip: true
                        model: cc.deviceUids()
                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 28
                            radius: 4
                            color: (cc.selUid === modelData)
                                   ? (cc.theme.inv || "#F0F0F0")
                                   : (devItemMa.containsMouse ? (cc.theme.hover || "#1E1E1E") : "transparent")
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                text: modelData
                                color: (cc.selUid === modelData) ? (cc.theme.win || "#0A0A0A")
                                                                 : (cc.theme.fg || "#FAFAFA")
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: devItemMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    cc.selUid = modelData
                                    devMenu.close()
                                    cc.refreshCams()
                                }
                            }
                        }
                    }
                }
            }
        }

        // 第二行：动作按钮（全用 Btn，宽度加起来 88+56+56+56+80 + 4×8 = 368，稳稳放得下）
        Row {
            spacing: 8
            Btn {
                theme: cc.theme
                width: 88
                text: cc.loadingCams ? "枚举中…" : "摄像头列表"
                disabled: cc.selUid === ""
                onClicked: cc.refreshCams()
            }
            Btn {
                theme: cc.theme
                width: 56
                text: "测试"
                disabled: cc.selUid === ""
                onClicked: cc.testCam()
            }
            Btn {
                theme: cc.theme
                width: 56
                text: "抓拍"
                disabled: cc.selUid === ""
                onClicked: cc.snapshot()
            }
            Btn {
                theme: cc.theme
                width: 56
                text: cc.recording ? "停录" : "开录"
                disabled: cc.selUid === ""
                onClicked: cc.recording ? cc.stopRec() : cc.startRec()
            }
            Btn {
                theme: cc.theme
                width: 80
                text: "刷新回放"
                onClicked: { cc.backend.fetchRecordings(); cc.status = "已刷新录制列表" }
            }
        }
    }

    // ── 主体：左＝摄像头绑定 / 右＝回放 ──
    Row {
        anchors.top: head.bottom
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusLine.top
        anchors.bottomMargin: 6
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
                    text: "当前绑定：" + (cc.boundDev !== "" ? cc.boundDev
                                        : (cc.currentDev() !== "" ? cc.currentDev() + "（本地缓存）" : "（未绑定，默认 video0）"))
                    color: cc.theme.fg3 || "#8A8A8A"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    width: parent.width
                }
                Btn {
                    theme: cc.theme
                    width: parent.width
                    text: "绑定选中的摄像头"
                    disabled: camList.currentIndex < 0
                    onClicked: {
                        if (cc.selUid === "" || camList.currentIndex < 0) return
                        var dev = cc.cameras[camList.currentIndex]
                        // 权威绑定落在**被控端**（只有它能枚举/打开摄像头）；本地 Settings
                        // 只留一份镜像给 CameraBindDialog 复用同一个 key（2026-10-09）。
                        var map = store.bindings || {}
                        map[cc.selUid] = dev
                        store.bindings = map
                        cc.backend.sendAction("camera_bind", { device: dev })
                        cc.status = "正在绑定 " + dev + " …"
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
        id: statusLine
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        text: cc.status
        color: cc.theme.fg4 || "#707070"
        font.pixelSize: 10
        elide: Text.ElideRight
    }

    // ── 逻辑 ──
    /** 可选设备（在线清单里的 uid）。原来直接喂给原生 ComboBox；现在喂自绘下拉。 */
    function deviceUids() {
        var out = []
        var devs = cc.backend.devices || []
        for (var i = 0; i < devs.length; ++i)
            if (devs[i] && devs[i].uid) out.push(devs[i].uid)
        return out
    }
    /** 本地缓存的绑定（镜像；权威在被控端 cc.boundDev）。 */
    function currentDev() {
        if (cc.selUid === "") return ""
        var b = store.bindings || {}
        return b[cc.selUid] || ""
    }
    function boundDevice() { return cc.boundDev !== "" ? cc.boundDev : cc.currentDev() }
    /** 抓拍/录制要用的设备：权威绑定 > 本地镜像 > 空（让被控端自己回落 video0）。 */
    function useDev() {
        if (cc.boundDev !== "") return cc.boundDev
        if (cc.currentDev() !== "") return cc.currentDev()
        return ""
    }
    function paramsWithDev() {
        var d = cc.useDev()
        return d === "" ? {} : { device: d }
    }

    function refreshCams() {
        if (cc.selUid === "") return
        cc.loadingCams = true
        cc.cameras = []
        cc.status = "正在枚举 " + cc.selUid + " 的摄像头…"
        cc.backend.sendAction("camera_list", {})
        // 回执由接线层调 cc.onCams()（Main.qml 现有回执处理转发）
    }
    /** 摄像头列表回执。⚠️ 现在收的是**整个 data**（要顺带取 boundDevice）。 */
    function onCams(data) {
        cc.loadingCams = false
        var cams = (data && data.cameras) ? data.cameras : []
        if (data && typeof data.boundDevice === "string") cc.boundDev = data.boundDevice
        if (Array.isArray(cams) && cams.length) {
            cc.cameras = cams
            cc.status = "发现 " + cams.length + " 个摄像头"
                     + (cc.boundDev !== "" ? "，已绑定 " + cc.boundDev : "（尚未绑定）")
        } else {
            cc.cameras = []
            cc.status = "该设备无摄像头或枚举失败（确认被控端装了 ffmpeg，且机器上有摄像头）"
        }
    }
    function onBindResult(ok, err, data) {
        if (ok) {
            if (data && typeof data.boundDevice === "string") cc.boundDev = data.boundDevice
            cc.status = "已绑定：" + (cc.boundDev !== "" ? cc.boundDev : "（已解绑）")
        } else {
            cc.status = "绑定失败：" + (err || "未知原因")
        }
    }
    function onTestResult(ok, err, data) {
        if (ok) {
            var ms = (data && data.elapsedMs !== undefined) ? data.elapsedMs : "?"
            cc.status = "自检通过：" + ((data && data.device) || "") + "，出帧 " + ms + "ms"
        } else {
            cc.status = "自检失败：" + (err || "未知原因")
        }
    }
    function onShotResult(ok, msg) {
        cc.status = ok ? "已抓拍（画面见右栏）" : ("抓拍失败：" + (msg || "未知原因"))
    }
    function snapshot() {
        cc.status = "抓拍 " + cc.selUid + (cc.useDev() !== "" ? "（device=" + cc.useDev() + "）" : "") + "…"
        cc.backend.sendAction("camera_snapshot", cc.paramsWithDev())
    }
    function testCam() {
        cc.status = "正在自检 " + cc.selUid + "…"
        cc.backend.sendAction("camera_test", cc.paramsWithDev())
    }
    function startRec() {
        cc.status = "正在开录 " + cc.selUid + "…"
        cc.backend.sendAction("camera_record_start", cc.paramsWithDev())
        cc.recording = true
        cc.backend.subscribeThumbnails([cc.selUid])   // 开启到帧刷新
    }
    function stopRec() {
        cc.status = "已停录（录制自动上传云端，稍后刷新回放）"
        cc.backend.sendAction("camera_record_stop", {})
        cc.recording = false
    }
}