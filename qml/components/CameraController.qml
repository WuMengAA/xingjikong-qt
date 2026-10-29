// 星集控 · 监控中心控制器（2026-10-29 拆分）
//
// 从 CameraCenter.qml 抽出的「状态 + 逻辑」控制器：持有全部状态属性、Md3 令牌便捷取值
// （t* 别名）、以及 ~35 个逻辑函数。视觉块（四档 Tab 页面 + 状态行 + 底栏）由
// CameraCenter.qml 及其子组件渲染，统一通过 `cc` 引用本控制器。
//
// 行为零差异：属性名 / 函数名 / 取值逻辑全部原样保留。唯一调整：
//   · 原 bind 按钮里的 store 写入抽成 bindCamera(dev)，供 CameraCenterDevice 调用；
//   · camSync() 依赖 camPane 可见性，下沉到 CameraCenterStage 本地持有。
import QtQuick
import QtCore

Item {
    id: ctrl

    property var theme: ({})
    property var backend
    property var panelCtx: null

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
    property string boundDev: ""        // 被控端回执的当前绑定（权威；空 = 未绑定）

    // ── 四·双画面：布局模式 ──
    // 0=双画面（屏幕 60% + 摄像头 40%）；1=仅屏幕；2=仅摄像头。
    property int layoutMode: 0
    // 摄像头第二轨状态（2026-10-12 · 方案 A：RTC second track "camera"）
    property string camStreamUid: ""   // 当前已请求推流的设备（""=未请求）
    property bool camHasFrame: false   // 是否已收到至少一帧（收到后隐藏占位）
    property bool screenHasFrame: false // 屏幕帧是否到达（新增：占位文案判据，连接中/暂无画面）

    // ── 自习模式配置（2026-10-13）：噪音检测阈值 + 异常自动录制 ──
    property real studyThresholdDb: -45   // 噪音阈值（dB；教室安静≈-60，说话≈-35，越小越灵敏）
    property int  studyStreakNeed: 3      // 连续几个采样都超阈才算"异常"（防瞬时爆音误报）
    property int  studyRecordSec: 30      // 判异常后自动录多久

    // ── 2026-10-12 · UI 评审第 4 条：界面只给人话，技术日志不上界面 ──
    property string errCode: ""         // device_not_found / device_busy / permission / timeout / …
    property bool   canRetry: false     // 这个错误"再来一次有可能好" ⇒ 显示「重试」
    property string lastAction: ""      // 「重试」要点名重发的动作
    property var    camerasInfo: []     // 摄像头明细（含 virtual 标记）；为空时回落到 ctrl.cameras

    // ── MD3 令牌便捷取值 ──
    // theme 即 Md3 实例（深浅两套随 darkMode 切）；缺键时退回 MD3 暗色基线，绝不崩。
    readonly property color tSurface:    (theme && theme.cSurface !== undefined) ? theme.cSurface : "#1C1B1F"
    readonly property color tCard:       (theme && theme.cContLow !== undefined) ? theme.cContLow : "#1D1B20"
    readonly property color tCardHi:     (theme && theme.cContHigh !== undefined) ? theme.cContHigh : "#2B2930"
    readonly property color tCardHighest:(theme && theme.cContHighest !== undefined) ? theme.cContHighest : "#36343B"
    readonly property color tStage:      (theme && theme.cContLowest !== undefined) ? theme.cContLowest : "#0F0D13"
    readonly property color tFg:         (theme && theme.cOnSurface !== undefined) ? theme.cOnSurface : "#E6E1E5"
    readonly property color tFg3:        (theme && theme.cOnSurfaceVar !== undefined) ? theme.cOnSurfaceVar : "#CAC4D0"
    readonly property color tFg4:        (theme && theme.cOutline !== undefined) ? theme.cOutline : "#938F99"
    readonly property color tAcc:        (theme && theme.cPrimary !== undefined) ? theme.cPrimary : "#D0BCFF"
    readonly property color tErr:        (theme && theme.cError !== undefined) ? theme.cError : "#F2B8B5"
    readonly property color tSegBg:      (theme && theme.cSecondaryCont !== undefined) ? theme.cSecondaryCont : "#4A4458"
    readonly property color tSegFg:      (theme && theme.cOnSecondaryCont !== undefined) ? theme.cOnSecondaryCont : "#EADDFF"
    readonly property color tOutlineVar: (theme && theme.cOutlineVar !== undefined) ? theme.cOutlineVar : "#49454F"
    readonly property int  tRM:    (theme && theme.rM !== undefined) ? theme.rM : 12
    readonly property int  tSpXS:  (theme && theme.spXS !== undefined) ? theme.spXS : 4
    readonly property int  tSpS:   (theme && theme.spS !== undefined) ? theme.spS : 8
    readonly property int  tSpM:   (theme && theme.spM !== undefined) ? theme.spM : 16
    readonly property int  tTitleM:(theme && theme.titleM !== undefined) ? theme.titleM : 16
    readonly property int  tTitleS:(theme && theme.titleS !== undefined) ? theme.titleS : 14
    readonly property int  tLabelL:(theme && theme.labelL !== undefined) ? theme.labelL : 14
    readonly property int  tLabelM:(theme && theme.labelM !== undefined) ? theme.labelM : 12
    readonly property int  tLabelS:(theme && theme.labelS !== undefined) ? theme.labelS : 11

    // ── 逻辑 ──
    /** 可选设备（在线清单里的 uid）。喂给自绘下拉。 */
    function deviceUids() {
        var out = []
        var devs = ctrl.backend.devices || []
        for (var i = 0; i < devs.length; ++i)
            if (devs[i] && devs[i].uid) out.push(devs[i].uid)
        return out
    }

    /** 设备显示名：有 name 用 name，没有回落 uid（标题/诊断/回放列表共用）。 */
    function deviceName(uid) {
        if (uid === "") return ""
        var devs = ctrl.backend.devices || []
        for (var i = 0; i < devs.length; ++i) {
            var d = devs[i]
            if (d && d.uid === uid && d.name) return d.name
        }
        return uid
    }
    /** 本地缓存的绑定（镜像；权威在被控端 ctrl.boundDev）。 */
    function currentDev() {
        if (ctrl.selUid === "") return ""
        var b = store.bindings || {}
        return b[ctrl.selUid] || ""
    }
    function boundDevice() { return ctrl.boundDev !== "" ? ctrl.boundDev : ctrl.currentDev() }
    /** 抓拍/录制要用的设备：权威绑定 > 本地镜像 > 空（让被控端自己回落 video0）。 */
    function useDev() {
        if (ctrl.boundDev !== "") return ctrl.boundDev
        if (ctrl.currentDev() !== "") return ctrl.currentDev()
        return ""
    }
    function paramsWithDev() {
        var d = ctrl.useDev()
        return d === "" ? {} : { device: d }
    }

    /** 列表里显示的名字：虚拟摄像头（OBS / Iriun 之类）**打不出真实画面**，
     *  标出来，免得老师选了半天发现"设备列出来了却没画面"（2026-10-12）。 */
    function camLabel(i, nm) {
        var inf = ctrl.camerasInfo
        if (Array.isArray(inf) && i < inf.length && inf[i] && inf[i].virtual === true)
            return nm + "（虚拟·无画面）"
        return nm
    }

    /** 失败统一收口（2026-10-12 · UI 评审第 4 条）：界面只吃 err，errorRaw 只进 console。 */
    function noteError(prefix, err, data) {
        var code = (data && data.errorCode) ? String(data.errorCode) : ""
        var raw  = (data && data.errorRaw)  ? String(data.errorRaw)  : ""
        ctrl.errCode = code
        if (data && data.retryable !== undefined) {
            ctrl.canRetry = (data.retryable === true)
        } else {
            ctrl.canRetry = (code !== "device_not_found" && code !== "device_invalid")
        }
        if (raw !== "") console.warn("[camera] 错误码 " + code + " 原始输出：" + raw)
        ctrl.status = prefix + (err || "未知原因")
    }
    /** 「重试」：把最后一次动作原样重发一次。 */
    function retryLast() {
        var a = ctrl.lastAction
        if (a === "") return
        ctrl.canRetry = false
        if (a === "camera_list") ctrl.refreshCams()
        else if (a === "camera_test") ctrl.testCam()
        else if (a === "camera_snapshot") ctrl.snapshot()
        else if (a === "camera_record_start") ctrl.startRec()
    }

    function refreshCams() {
        if (ctrl.selUid === "") return
        ctrl.loadingCams = true
        ctrl.cameras = []
        ctrl.camerasInfo = []
        ctrl.errCode = ""; ctrl.canRetry = false
        ctrl.lastAction = "camera_list"
        ctrl.status = "正在枚举 " + ctrl.deviceName(ctrl.selUid) + " 的摄像头…"
        ctrl.backend.sendAction("camera_list", {})
    }
    /** 摄像头列表回执。⚠️ 现在收的是**整个 data**（要顺带取 boundDevice）。 */
    function onCams(data) {
        ctrl.loadingCams = false
        var cams = (data && data.cameras) ? data.cameras : []
        var info = (data && data.camerasDetailed) ? data.camerasDetailed : []
        var virt = (data && data.virtualCount !== undefined) ? data.virtualCount : 0
        ctrl.camerasInfo = Array.isArray(info) ? info : []
        if (data && typeof data.boundDevice === "string") ctrl.boundDev = data.boundDevice
        if (Array.isArray(cams) && cams.length) {
            ctrl.cameras = cams
            ctrl.errCode = ""; ctrl.canRetry = false
            ctrl.status = "发现 " + cams.length + " 个摄像头"
                     + (virt > 0 ? "（其中 " + virt + " 个是虚拟设备，无画面）" : "")
                     + (ctrl.boundDev !== "" ? "，已绑定 " + ctrl.boundDev : "（尚未绑定）")
        } else {
            ctrl.cameras = []
            ctrl.status = "没找到摄像头。请确认摄像头已插好、没被其它程序占用，然后点右上角刷新重新枚举"
            ctrl.errCode = "no_devices"; ctrl.canRetry = true
        }
    }
    function onBindResult(ok, err, data) {
        if (ok) {
            if (data && typeof data.boundDevice === "string") ctrl.boundDev = data.boundDevice
            ctrl.errCode = ""; ctrl.canRetry = false
            ctrl.status = "已绑定：" + (ctrl.boundDev !== "" ? ctrl.boundDev : "（已解绑）")
        } else {
            ctrl.noteError("绑定失败：", err, data)
        }
    }
    function onTestResult(ok, err, data) {
        if (ok) {
            var ms = (data && data.elapsedMs !== undefined) ? data.elapsedMs : "?"
            ctrl.errCode = ""; ctrl.canRetry = false
            ctrl.status = "自检通过：" + ((data && data.device) || "") + "，出帧 " + ms + "ms"
        } else {
            ctrl.noteError("自检失败：", err, data)
        }
    }
    function onShotResult(ok, msg) {
        if (ok) {
            ctrl.errCode = ""; ctrl.canRetry = false
            ctrl.status = "已抓拍（画面见右栏）"
        } else {
            ctrl.noteError("抓拍失败：", msg, null)
        }
    }
    function snapshot() {
        ctrl.lastAction = "camera_snapshot"
        ctrl.errCode = ""; ctrl.canRetry = false
        ctrl.status = "抓拍 " + ctrl.deviceName(ctrl.selUid) + (ctrl.useDev() !== "" ? "（device=" + ctrl.useDev() + "）" : "") + "…"
        ctrl.backend.sendAction("camera_snapshot", ctrl.paramsWithDev())
    }
    function testCam() {
        ctrl.lastAction = "camera_test"
        ctrl.errCode = ""; ctrl.canRetry = false
        ctrl.status = "正在自检 " + ctrl.deviceName(ctrl.selUid) + "…"
        ctrl.backend.sendAction("camera_test", ctrl.paramsWithDev())
    }
    function startRec() {
        ctrl.lastAction = "camera_record_start"
        ctrl.errCode = ""; ctrl.canRetry = false
        ctrl.status = "正在开录 " + ctrl.deviceName(ctrl.selUid) + "…"
        ctrl.backend.sendAction("camera_record_start", ctrl.paramsWithDev())
        ctrl.recording = true
        ctrl.backend.subscribeThumbnails([ctrl.selUid])
    }
    function stopRec() {
        ctrl.status = "已停录，稍后可云端回放"
        ctrl.backend.sendAction("camera_record_stop", {})
        ctrl.recording = false
    }
    /** 绑定选中的摄像头（store 镜像 + 发 camera_bind）。从 UI 抽取，避免子组件直接碰 store。 */
    function bindCamera(dev) {
        if (ctrl.selUid === "" || dev === undefined) return
        var map = store.bindings || {}
        map[ctrl.selUid] = dev
        store.bindings = map
        ctrl.backend.sendAction("camera_bind", { device: dev })
        ctrl.status = "正在绑定 " + dev + " …"
    }

    // ── 底栏新动作（MD3 重构批）──
    /** 屏幕截图（screenshot）：与抓拍（camera_snapshot）是两回事——这个截整屏。 */
    function screenShot() {
        if (ctrl.selUid === "") return
        ctrl.lastAction = ""; ctrl.errCode = ""; ctrl.canRetry = false
        ctrl.status = "正在截图 " + ctrl.deviceName(ctrl.selUid) + "…"
        ctrl.backend.sendAction("screenshot", {})
    }
    /** 推文件：把当前设备设为 backend.currentUid（file 面板按它选目标），再开 file 面板。 */
    function pushFile() {
        if (ctrl.selUid === "") { ctrl.status = "先选一台设备"; return }
        if (ctrl.backend.currentUid !== ctrl.selUid) ctrl.backend.currentUid = ctrl.selUid
        if (ctrl.panelCtx && typeof ctrl.panelCtx.open === "function") ctrl.panelCtx.open("file")
        else ctrl.status = "当前宿主不支持在此打开发文件面板"
    }
    /** 一键收口：停录 + 停摄像头推流。 */
    function stopAll() {
        if (ctrl.recording) ctrl.stopRec()
        if (ctrl.camStreamUid !== "") {
            ctrl.backend.sendAction("camera_stream_stop", {})
            ctrl.camStreamUid = ""
            ctrl.camHasFrame = false
        }
        ctrl.status = "已停止（录制 / 推流均已收口）"
    }
    /** 关闭面板（宿主上下文注入的 closePanels；老宿主没有时不崩，只没反应）。 */
    function closePanel() {
        if (ctrl.panelCtx && typeof ctrl.panelCtx.closePanels === "function") ctrl.panelCtx.closePanels()
    }
}
