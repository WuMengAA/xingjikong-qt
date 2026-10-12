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
//   CameraCenter { theme: root.th; backend: root.backendRef; width: ...; height: ... }
//   ⚠️ 别写 `backend: backend`（自绑定循环，见 Main.qml 里 backendRef 的说明）。
import QtQuick
import QtQuick.Controls
// ⚠️ 2026-10-10：Settings 自 Qt 6.5 起迁进 QtCore，Qt.labs.settings 已弃用（启动就刷 deprecation）。
//   另外它的落点依赖 QCoreApplication 的 organizationName —— 团队已在 main.cpp 补设（见那里注释）。
import QtCore

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

    // ── 2026-10-12 · UI 评审第 4 条：界面只给人话，技术日志不上界面 ──
    //  被控端现在回三件套：error（人话，可直接显示）/ errorCode（机器码）/ errorRaw（FFmpeg 原生输出）。
    //  ⚠️ 红线：errorRaw **只进 console（供导出日志/诊断）**，绝不能贴到界面上——
    //     `Unable to find a matching device` 这类话老师看不懂，只会判定"软件坏了"。
    property string errCode: ""         // device_not_found / device_busy / permission / timeout / …
    property bool   canRetry: false     // 这个错误"再来一次有可能好" ⇒ 显示「重试」
    property string lastAction: ""      // 「重试」要点名重发的动作
    property var    camerasInfo: []     // 摄像头明细（含 virtual 标记）；为空时回落到 cc.cameras

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

    // ── 主体：TabBar（实时 / 录制回放 / 诊断）──
    // 一.2 UI 评审（2026-10-12）：监控中心拆标签页。原来左＝绑定 / 右＝实时+回放 平铺一屏，
    // 评审要求拆成「实时 / 录制回放 / 诊断」三档——默认看实时，回放与诊断不再挤在同一屏。
    // 新增组件 qml/components/TabBar.qml（令牌化，不用 Qt 原生 TabBar）。
    Column {
        id: body
        anchors.top: head.bottom
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusLine.top
        anchors.bottomMargin: 6
        spacing: 8

        StTabBar {
            id: tabs
            theme: cc.theme
            width: parent.width
            model: ["实时", "录制回放", "诊断"]
            currentIndex: 0
        }

        // 页面容器：三档互斥显示
        Item {
            width: parent.width
            height: parent.height - tabs.height - body.spacing

            // ── 实时：左＝摄像头绑定 / 右＝实时画面 ──
            Row {
                visible: tabs.currentIndex === 0
                anchors.fill: parent
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
                                        text: cc.camLabel(index, modelData)
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
                                var map = store.bindings || {}
                                map[cc.selUid] = dev
                                store.bindings = map
                                cc.backend.sendAction("camera_bind", { device: dev })
                                cc.status = "正在绑定 " + dev + " …"
                            }
                        }
                    }
                }

                // 右：实时画面
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
                        Text {
                            text: "实时画面"
                            color: cc.theme.fg3 || "#8A8A8A"
                            font.pixelSize: 11
                        }
                        // 实时帧（缩略图墙订阅的那台的帧）
                        Rectangle {
                            width: parent.width
                            height: parent.height - 40
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
                    }
                }
            }

            // ── 录制回放 ──
            Rectangle {
                visible: tabs.currentIndex === 1
                anchors.fill: parent
                radius: cc.theme.rCard || 12
                color: cc.theme.panel || "#181818"
                border.color: cc.theme.stroke || "#2A2A2A"
                border.width: 1
                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8
                    Text {
                        text: "录制回放（本机）"
                        color: cc.theme.fg3 || "#8A8A8A"
                        font.pixelSize: 11
                    }
                    // 回放列表（按 uid 分组）
                    Flickable {
                        width: parent.width
                        height: parent.height - 40
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

            // ── 诊断 ──
            // 把"设备连接状态 / 绑定 / 摄像头枚举 / 最近错误（人话）+ 错误码 / 重试"聚到一屏，
            // 老师排障时不用在动作区翻找（UI 评审一.2：诊断独立成档）。
            Rectangle {
                visible: tabs.currentIndex === 2
                anchors.fill: parent
                radius: cc.theme.rCard || 12
                color: cc.theme.panel || "#181818"
                border.color: cc.theme.stroke || "#2A2A2A"
                border.width: 1
                Flickable {
                    anchors.fill: parent
                    anchors.margins: 12
                    contentHeight: diagCol.height
                    clip: true
                    Column {
                        id: diagCol
                        width: parent.width
                        spacing: 10
                        Row { spacing: 8
                            Text { text: "选中设备"; color: cc.theme.fg3 || "#8A8A8A"; font.pixelSize: 12 }
                            Text { text: cc.selUid !== "" ? cc.selUid : "（未选设备）"; color: cc.theme.fg || "#FAFAFA"; font.pixelSize: 12 }
                        }
                        Row { spacing: 8
                            Text { text: "当前绑定"; color: cc.theme.fg3 || "#8A8A8A"; font.pixelSize: 12 }
                            Text { text: cc.boundDevice() !== "" ? cc.boundDevice()
                                        : (cc.currentDev() !== "" ? cc.currentDev() + "（本地缓存）" : "（未绑定，默认 video0）")
                                   color: cc.theme.fg || "#FAFAFA"; font.pixelSize: 12; elide: Text.ElideRight; width: parent.width - 120 }
                        }
                        Row { spacing: 8
                            Text { text: "摄像头"; color: cc.theme.fg3 || "#8A8A8A"; font.pixelSize: 12 }
                            Text { text: cc.loadingCams ? "枚举中…" : (cc.cameras.length + " 个"); color: cc.theme.fg || "#FAFAFA"; font.pixelSize: 12 }
                        }
                        Column { spacing: 2
                            Text { text: "最近状态"; color: cc.theme.fg3 || "#8A8A8A"; font.pixelSize: 12 }
                            Text { text: cc.status !== "" ? cc.status : "（无）"
                                   color: cc.theme.fg || "#FAFAFA"; font.pixelSize: 12; width: parent.width; wrapMode: Text.WrapAnywhere }
                        }
                        Row { visible: cc.errCode !== ""
                            spacing: 8
                            Text { text: "错误码"; color: cc.theme.fg3 || "#8A8A8A"; font.pixelSize: 12 }
                            Text { text: cc.errCode; color: cc.theme.fg || "#FAFAFA"; font.pixelSize: 12 }
                        }
                        Btn {
                            visible: cc.canRetry
                            theme: cc.theme
                            width: 80
                            text: "重试"
                            tip: "再试一次刚才没成功的操作"
                            onClicked: cc.retryLast()
                        }
                    }
                }
            }
        }
    }
    // ── 状态行 ──
    // 2026-10-12 · UI 评审第 4 条：状态只显示**人话**结论，可恢复的错误旁边给一个「重试」。
    // FFmpeg 原生输出已在 noteError() 里被拦下（只进 console），绝不会走到这一行。
    Text {
        id: statusLine
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: retryBtn.visible ? retryBtn.left : parent.right
        anchors.rightMargin: retryBtn.visible ? 8 : 0
        height: retryBtn.visible ? 26 : implicitHeight
        text: cc.status
        color: cc.theme.fg4 || "#707070"
        font.pixelSize: 10
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    Btn {
        id: retryBtn
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        visible: cc.canRetry
        width: 64
        height: 26
        theme: cc.theme
        text: "重试"
        tip: "再试一次刚才没成功的操作"
        onClicked: cc.retryLast()
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

    /** 列表里显示的名字：虚拟摄像头（OBS / Iriun 之类）**打不出真实画面**，
     *  标出来，免得老师选了半天发现"设备列出来了却没画面"（2026-10-12）。 */
    function camLabel(i, nm) {
        var inf = cc.camerasInfo
        if (Array.isArray(inf) && i < inf.length && inf[i] && inf[i].virtual === true)
            return nm + "（虚拟·无画面）"
        return nm
    }

    /** 失败统一收口（2026-10-12 · UI 评审第 4 条）：
     *  · 界面只吃 err —— 被控端现在给的 err 已经是**人话**，不再是 FFmpeg 原生输出；
     *  · errorCode 决定要不要给「重试」；
     *  · errorRaw 只进 console（导出日志/诊断用），**绝不贴到界面上**。
     *  ⚠️ 旧版被控端没有 errorCode/retryable，走下面的兜底分支，不会崩。 */
    function noteError(prefix, err, data) {
        var code = (data && data.errorCode) ? String(data.errorCode) : ""
        var raw  = (data && data.errorRaw)  ? String(data.errorRaw)  : ""
        cc.errCode = code
        if (data && data.retryable !== undefined) {
            cc.canRetry = (data.retryable === true)
        } else {
            // 没给就猜：除了"拔掉了 / 名字失效"这两种"重来也没用"的，都给一次机会。
            cc.canRetry = (code !== "device_not_found" && code !== "device_invalid")
        }
        if (raw !== "") console.warn("[camera] 错误码 " + code + " 原始输出：" + raw)
        cc.status = prefix + (err || "未知原因")
    }
    /** 「重试」：把最后一次动作原样重发一次。 */
    function retryLast() {
        var a = cc.lastAction
        if (a === "") return
        cc.canRetry = false
        if (a === "camera_list") cc.refreshCams()
        else if (a === "camera_test") cc.testCam()
        else if (a === "camera_snapshot") cc.snapshot()
        else if (a === "camera_record_start") cc.startRec()
    }

    function refreshCams() {
        if (cc.selUid === "") return
        cc.loadingCams = true
        cc.cameras = []
        cc.camerasInfo = []
        cc.errCode = ""; cc.canRetry = false
        cc.lastAction = "camera_list"
        cc.status = "正在枚举 " + cc.selUid + " 的摄像头…"
        cc.backend.sendAction("camera_list", {})
        // 回执由接线层调 cc.onCams()（Main.qml 现有回执处理转发）
    }
    /** 摄像头列表回执。⚠️ 现在收的是**整个 data**（要顺带取 boundDevice）。 */
    function onCams(data) {
        cc.loadingCams = false
        var cams = (data && data.cameras) ? data.cameras : []
        var info = (data && data.camerasDetailed) ? data.camerasDetailed : []
        var virt = (data && data.virtualCount !== undefined) ? data.virtualCount : 0
        cc.camerasInfo = Array.isArray(info) ? info : []
        if (data && typeof data.boundDevice === "string") cc.boundDev = data.boundDevice
        if (Array.isArray(cams) && cams.length) {
            cc.cameras = cams
            cc.errCode = ""; cc.canRetry = false
            cc.status = "发现 " + cams.length + " 个摄像头"
                     + (virt > 0 ? "（其中 " + virt + " 个是虚拟设备，无画面）" : "")
                     + (cc.boundDev !== "" ? "，已绑定 " + cc.boundDev : "（尚未绑定）")
        } else {
            cc.cameras = []
            cc.status = "没找到摄像头。请确认摄像头已插好、没被其它程序占用，然后点「摄像头列表」重新枚举"
            cc.errCode = "no_devices"; cc.canRetry = true
        }
    }
    function onBindResult(ok, err, data) {
        if (ok) {
            if (data && typeof data.boundDevice === "string") cc.boundDev = data.boundDevice
            cc.errCode = ""; cc.canRetry = false
            cc.status = "已绑定：" + (cc.boundDev !== "" ? cc.boundDev : "（已解绑）")
        } else {
            cc.noteError("绑定失败：", err, data)
        }
    }
    function onTestResult(ok, err, data) {
        if (ok) {
            var ms = (data && data.elapsedMs !== undefined) ? data.elapsedMs : "?"
            cc.errCode = ""; cc.canRetry = false
            cc.status = "自检通过：" + ((data && data.device) || "") + "，出帧 " + ms + "ms"
        } else {
            cc.noteError("自检失败：", err, data)
        }
    }
    function onShotResult(ok, msg) {
        if (ok) {
            cc.errCode = ""; cc.canRetry = false
            cc.status = "已抓拍（画面见右栏）"
        } else {
            // 抓拍回执只带 msg 不带 data，拿不到错误码 ⇒ 一律给一次重试机会。
            cc.noteError("抓拍失败：", msg, null)
        }
    }
    function snapshot() {
        cc.lastAction = "camera_snapshot"
        cc.errCode = ""; cc.canRetry = false
        cc.status = "抓拍 " + cc.selUid + (cc.useDev() !== "" ? "（device=" + cc.useDev() + "）" : "") + "…"
        cc.backend.sendAction("camera_snapshot", cc.paramsWithDev())
    }
    function testCam() {
        cc.lastAction = "camera_test"
        cc.errCode = ""; cc.canRetry = false
        cc.status = "正在自检 " + cc.selUid + "…"
        cc.backend.sendAction("camera_test", cc.paramsWithDev())
    }
    function startRec() {
        cc.lastAction = "camera_record_start"
        cc.errCode = ""; cc.canRetry = false
        cc.status = "正在开录 " + cc.selUid + "…"
        cc.backend.sendAction("camera_record_start", cc.paramsWithDev())
        cc.recording = true
        cc.backend.subscribeThumbnails([cc.selUid])   // 开启到帧刷新
    }
    function stopRec() {
        cc.status = "已停录，稍后可云端回放"
        cc.backend.sendAction("camera_record_stop", {})
        cc.recording = false
    }
}