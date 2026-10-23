// 星集控 · 监控中心（2026-10-19 · MD3 重构批）
//
// 本次按 MD3 规范重构（用户点单），彻底消除旧版"拼凑感"。规格对照：
//   ① 净化边框：卡片/区域一律无 border 无阴影，用**背景色阶**区分层级——
//      页面 cSurface → 卡片 cContLow → 嵌套/选中 cContHigh → 画面台 cContLowest；
//      仅保留 1px 分隔线（cOutlineVar）作小节分隔；浮层（Popup）按 MD3 用最高色阶。
//   ② 统一圆角：按钮/卡片/分段/徽标一律 rM（12px）；弹窗外壳是浮层，按 MD3 对话窗 rXL（28px）。
//   ③ Tab：StTabBar 已重做成标准下划线 Tab（主色 3px 指示器），不再是实心药丸。
//   ④ 精简文字：黑屏占位只剩「连接中...」/「暂无画面」并居中；长说明移到「诊断」页与 Tooltip。
//   ⑤ 分层：顶栏只有标题 + 右侧无边框图标按钮（测试/抓拍/关闭），空间留给标题。
//   ⑥ 网格对齐：面板左右外边距 spL(24)（弹窗给），组件间距 spM(16)，紧凑组内 spS(8)（令牌见 Md3.qml）。
//
// 功能零删减（逻辑函数全部原样保留）：设备下拉 / 摄像头枚举 / 绑定 / 自检 / 抓拍 /
// 开录停录 / 录制回放 / 自习模式（噪音 + 异常自动录）/ 诊断（人话错误 + 重试）。
// 行为变化仅两处：删死属性 recordingGroup（从未被引用）；新增 panelCtx 注入
// （推文件要开 file 面板、返回/关闭要 closePanels，由 CameraCenterDialog 传入）。
//
// 数据源：backend.devices → backend.sendAction() → backend.recordings → backend.subscribeThumbnails()
// 绑定存储：Qt.labs.settings → QtCore Settings（cameraBind/<uid> = 设备名，与 CameraBindDialog 同 key）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// ⚠️ 2026-10-10：Settings 自 Qt 6.5 起迁进 QtCore，Qt.labs.settings 已弃用（启动就刷 deprecation）。
//   另外它的落点依赖 QCoreApplication 的 organizationName —— 团队已在 main.cpp 补设（见那里注释）。
import QtCore
import "../island"   // SvgIcon 唯一真源在共享岛目录（构建期拷到 qml/island/）
import "md3"         // MD3 组件库（构建期由 Stelarith-shared-qml/ui/md3/ 拷入 components/md3/）

Item {
    id: cc

    property var theme: ({})
    required property var backend
    // ⚠️⚠️ 2026-10-19 实测坑（探针抓到，生产同中招）：**不能叫 `ctx`**！
    //   CameraCenterDialog 里对本组件的赋值写的是 `theme: ctx.window.th` /
    //   `backend: ctx.window.backendRef` / `ctx: ctx` —— 右值 `ctx` 在**本对象作用域**
    //   优先解析到本组件自己的属性。本组件一旦也声明 `ctx`，这三行全部变成自引用：
    //   theme/backend/ctx 恒为 undefined/null，面板看似能开实则全死（无令牌、无 backend）。
    //   旧版本组件没有 ctx 属性所以无事；MD3 重构批新增注入时命名必须避开宿主 ctx 名。
    property var panelCtx: null   // 宿主上下文（UiPanelHost 注入：window / open / closePanels）

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
    //  被控端现在回三件套：error（人话，可直接显示）/ errorCode（机器码）/ errorRaw（FFmpeg 原生输出）。
    //  ⚠️ 红线：errorRaw **只进 console（供导出日志/诊断）**，绝不能贴到界面上——
    //     `Unable to find a matching device` 这类话老师看不懂，只会判定"软件坏了"。
    property string errCode: ""         // device_not_found / device_busy / permission / timeout / …
    property bool   canRetry: false     // 这个错误"再来一次有可能好" ⇒ 显示「重试」
    property string lastAction: ""      // 「重试」要点名重发的动作
    property var    camerasInfo: []     // 摄像头明细（含 virtual 标记）；为空时回落到 cc.cameras

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

    // ── 无边框图标按钮（IconButton）：顶栏/刷新用，状态层 8%/10%，无底无边 ──
    component IconBtn: Rectangle {
        id: iconBtn
        property var th: ({})
        property string icon: ""
        property string tip: ""
        property bool enabled: true
        signal clicked()
        width: 40
        height: 40
        radius: (th && th.rM !== undefined) ? th.rM : 12
        color: "transparent"
        opacity: iconBtn.enabled ? 1 : 0.38
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: (th && th.cPrimary !== undefined) ? th.cPrimary : "#D0BCFF"
            opacity: ma.pressed ? 0.10 : (ma.containsMouse ? 0.08 : 0)
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        SvgIcon {
            anchors.centerIn: parent
            name: iconBtn.icon
            px: 20
            tint: (iconBtn.th && iconBtn.th.cOnSurface !== undefined) ? iconBtn.th.cOnSurface : "#E6E1E5"
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            enabled: iconBtn.enabled
            cursorShape: iconBtn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (iconBtn.enabled) iconBtn.clicked()
        }
        ToolTip {
            visible: ma.containsMouse && iconBtn.tip !== ""
            delay: 450
            text: iconBtn.tip
        }
    }

    // ═══ 整体骨架：顶栏 / Tab / 页面 / 状态行 / 底栏（组件间距 spM=16） ═══
    ColumnLayout {
        anchors.fill: parent
        spacing: cc.tSpM

        // ① 顶栏：标题吃剩余宽度，右侧无边框图标按钮（测试/抓拍/关闭）
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Text {
                id: headTitle
                anchors.left: parent.left
                anchors.right: headBtns.left
                anchors.rightMargin: cc.tSpS
                anchors.verticalCenter: parent.verticalCenter
                text: "监控中心" + (cc.selUid !== "" ? " · " + cc.deviceName(cc.selUid) : "")
                color: cc.tFg
                font.pixelSize: cc.tTitleM
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Row {
                id: headBtns
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: cc.tSpXS
                IconBtn {
                    th: cc.theme
                    icon: "circle-check"
                    tip: "摄像头自检：发一条测试指令，看能不能出帧"
                    enabled: cc.selUid !== ""
                    onClicked: cc.testCam()
                }
                IconBtn {
                    th: cc.theme
                    icon: "video"
                    tip: "抓拍一帧（camera_snapshot）"
                    enabled: cc.selUid !== ""
                    onClicked: cc.snapshot()
                }
                IconBtn {
                    th: cc.theme
                    icon: "x"
                    tip: "关闭监控中心"
                    onClicked: cc.closePanel()
                }
            }
        }

        // ② Tab：标准下划线 Tab（实时 / 录制回放 / 自习 / 诊断）
        StTabBar {
            id: tabs
            theme: cc.theme
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            model: ["实时", "录制回放", "自习", "诊断"]
            currentIndex: 0
        }

        // ③ 页面容器：四档互斥显示
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ── 实时：左＝摄像头列表 / 右＝实时画面 ──
            Row {
                id: liveRow
                visible: tabs.currentIndex === 0
                anchors.fill: parent
                spacing: cc.tSpM

                // 左：摄像头列表卡（无边框，底色 cContLow 区分区域）
                Rectangle {
                    id: camCard
                    width: parent.width * 0.42
                    height: parent.height
                    color: cc.tCard
                    radius: cc.tRM

                    Column {
                        id: camTopCol
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: cc.tSpM
                        spacing: cc.tSpS

                        // 设备选择（功能保留：自绘下拉；无边框，底色区分，MD3 12 圆角）
                        Item {
                            width: parent.width
                            height: 32
                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: "设备"
                                color: cc.tFg3
                                font.pixelSize: cc.tLabelM
                            }
                            Rectangle {
                                id: devSel
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 44
                                height: 32
                                radius: cc.tRM
                                color: devMa.containsMouse ? cc.tCardHighest : cc.tCardHi
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: cc.tSpM
                                    anchors.right: caret.left
                                    anchors.rightMargin: cc.tSpS
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: cc.selUid !== "" ? cc.deviceName(cc.selUid)
                                                         : (cc.deviceUids().length === 0 ? "（无在线设备）" : "请选择设备")
                                    color: cc.tFg
                                    font.pixelSize: cc.tLabelM
                                    elide: Text.ElideRight
                                }
                                Text {
                                    id: caret
                                    anchors.right: parent.right
                                    anchors.rightMargin: cc.tSpS
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "▾"
                                    color: cc.tFg3
                                    font.pixelSize: cc.tLabelM
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
                                        color: cc.tCardHighest
                                        radius: cc.tRM
                                    }
                                    contentItem: ListView {
                                        implicitHeight: Math.min(contentHeight, 220)
                                        clip: true
                                        model: cc.deviceUids()
                                        delegate: Rectangle {
                                            width: ListView.view.width
                                            height: 28
                                            radius: cc.tRM
                                            color: (cc.selUid === modelData)
                                                   ? cc.tSegBg
                                                   : (devItemMa.containsMouse ? cc.tCardHi : "transparent")
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.left: parent.left
                                                anchors.leftMargin: cc.tSpS
                                                anchors.right: parent.right
                                                anchors.rightMargin: cc.tSpS
                                                text: modelData
                                                color: (cc.selUid === modelData) ? cc.tSegFg : cc.tFg
                                                font.pixelSize: cc.tLabelM
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

                        // 小节标题行：摄像头列表 + 右侧刷新（图标按钮）
                        Item {
                            width: parent.width
                            height: 24
                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: "摄像头列表"
                                color: cc.tFg3
                                font.pixelSize: cc.tLabelM
                                font.weight: Font.Medium
                            }
                            Rectangle {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 28
                                height: 28
                                radius: cc.tRM
                                color: refMa.containsMouse ? cc.tCardHi : "transparent"
                                SvgIcon {
                                    anchors.centerIn: parent
                                    name: "refresh-cw"
                                    px: 16
                                    tint: cc.tFg3
                                }
                                MouseArea {
                                    id: refMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: cc.refreshCams()
                                }
                                ToolTip {
                                    visible: refMa.containsMouse
                                    delay: 400
                                    text: "重新枚举摄像头"
                                }
                            }
                        }
                        Rectangle {
                            width: parent.width
                            height: 1
                            color: cc.tOutlineVar
                        }
                        // 计数短文案（长说明移「诊断」页）
                        Text {
                            width: parent.width
                            text: cc.loadingCams ? "枚举中…"
                                 : (cc.cameras.length === 0 ? "未枚举" : cc.cameras.length + " 个摄像头")
                            color: cc.tFg4
                            font.pixelSize: cc.tLabelS
                            elide: Text.ElideRight
                            ToolTip {
                                id: camCountTip
                                delay: 400
                                visible: camCountTip.text !== ""
                                text: cc.loadingCams ? ""
                                     : (cc.cameras.length === 0
                                        ? "点右上角刷新重新枚举；或确认摄像头已插好、没被其它程序占用"
                                        : "")
                            }
                        }
                    }

                    // 列表区（填满卡片中部）
                    Rectangle {
                        anchors.top: camTopCol.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: bindBtn.top
                        anchors.topMargin: cc.tSpS
                        anchors.leftMargin: cc.tSpM
                        anchors.rightMargin: cc.tSpM
                        anchors.bottomMargin: cc.tSpS
                        color: "transparent"
                        ListView {
                            id: camList
                            anchors.fill: parent
                            clip: true
                            model: cc.cameras
                            delegate: Item {
                                width: camList.width
                                height: 30
                                Rectangle {
                                    anchors.fill: parent
                                    radius: cc.tRM
                                    color: camList.currentIndex === index
                                           ? cc.tSegBg
                                           : (rowMa.containsMouse ? cc.tCardHi : "transparent")
                                }
                                SvgIcon {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "video"
                                    px: 14
                                    tint: camList.currentIndex === index ? cc.tSegFg : cc.tFg3
                                }
                                Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 32
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: cc.camLabel(index, modelData)
                                    color: camList.currentIndex === index ? cc.tSegFg : cc.tFg
                                    font.pixelSize: cc.tLabelM
                                    elide: Text.ElideRight
                                }
                                MouseArea {
                                    id: rowMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: camList.currentIndex = index
                                }
                            }
                        }
                    }

                    // 绑定选中的摄像头（主操作，filled，统一 12 圆角）
                    Md3Btn {
                        id: bindBtn
                        md3: cc.theme
                        radius: cc.tRM
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: cc.tSpM
                        variant: "filled"
                        text: "＋ 绑定选中"
                        enabled: camList.currentIndex >= 0
                        tip: "把选中的摄像头设为该设备的采集源（camera_bind）"
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

                // 右：实时画面卡（无边框，底色 cContLow）
                Rectangle {
                    id: liveCard
                    width: parent.width - camCard.width - parent.spacing
                    height: parent.height
                    color: cc.tCard
                    radius: cc.tRM
                    Column {
                        anchors.fill: parent
                        anchors.margins: cc.tSpM
                        spacing: cc.tSpS

                        // 模式切换：共享组件 Md3SegmentedButton（2026-10-23 #461 收编）
                        // 原先是这里一段 56 行内联实现 —— 班级工作台中栏「设备↔展板」
                        // 是同一范式，收编到 Stelarith-shared-qml/ui/md3/ 后全库一份。
                        // ⚠️ current 是 var：layoutMode 是 int（0/1/2），段 value 必须同型。
                        Md3SegmentedButton {
                            id: segRow
                            anchors.horizontalCenter: parent.horizontalCenter
                            md3: cc.theme
                            model: [
                                { value: 1, label: "仅屏幕",   icon: "monitor",     tip: "只看被控端屏幕" },
                                { value: 0, label: "",         icon: "layout-grid", tip: "双画面：屏幕 + 摄像头" },
                                { value: 2, label: "仅摄像头", icon: "video",       tip: "只看摄像头实时流" }
                            ]
                            current: cc.layoutMode
                            onActivated: function (v) { cc.layoutMode = v }
                        }

                        // 画面台（最深底色，像一块"屏幕"）
                        Rectangle {
                            id: stage
                            width: parent.width
                            height: parent.height - segRow.height - parent.spacing
                            color: cc.tStage
                            radius: cc.tRM
                            clip: true
                            Row {
                                id: paneRow
                                anchors.fill: parent
                                anchors.margins: 4
                                spacing: 4
                                // 屏幕画面（真实源 image://frames/<uid>）
                                Rectangle {
                                    visible: cc.layoutMode !== 2
                                    width: cc.layoutMode === 0
                                           ? ((paneRow.width - 4) * 3 / 5)
                                           : paneRow.width
                                    height: paneRow.height
                                    color: "transparent"
                                    clip: true
                                    Image {
                                        id: screenImg
                                        anchors.fill: parent
                                        anchors.margins: 2
                                        fillMode: Image.PreserveAspectFit
                                        cache: false
                                        property int tick: 0
                                        source: cc.selUid ? ("image://frames/" + cc.selUid + "?t" + tick) : ""
                                        Connections {
                                            target: cc.backend
                                            function onThumbnailChanged(uid) {
                                                screenImg.tick++
                                                if (uid === cc.selUid || uid === "") cc.screenHasFrame = true
                                            }
                                        }
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        width: parent.width - 24
                                        horizontalAlignment: Text.AlignHCenter
                                        text: cc.selUid === "" ? "暂无画面" : (cc.screenHasFrame ? "" : "连接中...")
                                        color: cc.tFg3
                                        font.pixelSize: cc.tLabelM
                                        ToolTip {
                                            id: scrTip
                                            delay: 400
                                            visible: scrTip.text !== ""
                                            text: cc.selUid === ""
                                                  ? "请先在左侧列表中选择一台设备"
                                                  : (cc.screenHasFrame ? ""
                                                     : "已请求画面，还没收到首帧；被控端不在线时会一直连不上")
                                        }
                                    }
                                }
                                // 摄像头画面（2026-10-12 · 四·双画面 · 方案 A：RTC second track）
                                //   帧源 = image://frames/cam（backend.camFrame()，来自 "camera" RTC 轨）。
                                //   可见（双画面/仅摄像头 且 在实时页）⇒ 请被控端按需推流；不可见 ⇒ 停。
                                Rectangle {
                                    id: camPane
                                    visible: cc.layoutMode !== 1 && tabs.currentIndex === 0
                                    width: cc.layoutMode === 0
                                           ? ((paneRow.width - 4) * 2 / 5)
                                           : paneRow.width
                                    height: paneRow.height
                                    color: "transparent"
                                    clip: true
                                    Image {
                                        id: camImg
                                        anchors.fill: parent
                                        anchors.margins: 2
                                        fillMode: Image.PreserveAspectFit
                                        cache: false
                                        property int camTick: 0
                                        source: (cc.camStreamUid !== "" && cc.camHasFrame)
                                                ? ("image://frames/cam?t" + camTick) : ""
                                        Connections {
                                            target: cc.backend
                                            function onCameraFrameChanged() {
                                                cc.camHasFrame = true
                                                camImg.camTick++
                                            }
                                        }
                                    }
                                    Text {
                                        anchors.centerIn: parent
                                        width: parent.width - 24
                                        horizontalAlignment: Text.AlignHCenter
                                        text: cc.camStreamUid === "" ? "暂无画面" : (cc.camHasFrame ? "" : "连接中...")
                                        color: cc.tFg3
                                        font.pixelSize: cc.tLabelM
                                        ToolTip {
                                            id: camTip
                                            delay: 400
                                            visible: camTip.text !== ""
                                            text: cc.camStreamUid === ""
                                                  ? "切到「双画面 / 仅摄像头」即按需请被控端推流（不看不采）"
                                                  : (cc.camHasFrame ? ""
                                                     : "已请求推流，还没收到首帧；被控端无摄像头/被占用时会一直无画面")
                                        }
                                    }
                                    // 可见性变化（切标签/切单屏/切双画面）→ 同步推流状态
                                    onVisibleChanged: cc.camSync()
                                    Component.onCompleted: cc.camSync()
                                }
                            }
                        }
                    }
                }
            }

            // ── 录制回放 ──
            Rectangle {
                visible: tabs.currentIndex === 1
                anchors.fill: parent
                color: cc.tCard
                radius: cc.tRM
                Column {
                    anchors.fill: parent
                    anchors.margins: cc.tSpM
                    spacing: cc.tSpS
                    // 标题行：录制回放（本机） + 开录/停录 + 刷新
                    Item {
                        width: parent.width
                        height: 32
                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: "录制回放（本机）"
                            color: cc.tFg3
                            font.pixelSize: cc.tLabelM
                            font.weight: Font.Medium
                        }
                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: cc.tSpS
                            Md3Btn {
                                md3: cc.theme
                                radius: cc.tRM
                                variant: cc.recording ? "outlined" : "filled"
                                text: cc.recording ? "停录" : "开录"
                                enabled: cc.selUid !== ""
                                tip: cc.selUid === "" ? "先在「实时」页选一台设备"
                                                      : "让被控端录一段摄像头，停录后自动上传云端"
                                onClicked: cc.recording ? cc.stopRec() : cc.startRec()
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 32
                                height: 32
                                radius: cc.tRM
                                color: recRefMa.containsMouse ? cc.tCardHi : "transparent"
                                SvgIcon {
                                    anchors.centerIn: parent
                                    name: "refresh-cw"
                                    px: 16
                                    tint: cc.tFg3
                                }
                                MouseArea {
                                    id: recRefMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { cc.backend.fetchRecordings(); cc.status = "已刷新录制列表" }
                                }
                                ToolTip {
                                    visible: recRefMa.containsMouse
                                    delay: 400
                                    text: "刷新录制列表"
                                }
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: cc.tOutlineVar
                    }
                    // 回放列表（按 uid 分组）
                    Flickable {
                        width: parent.width
                        height: parent.height - 32 - 1 - 2 * cc.tSpS
                        clip: true
                        contentHeight: recCol.height
                        Column {
                            id: recCol
                            width: parent.width
                            spacing: cc.tSpS
                            Repeater {
                                model: Object.keys(cc.backend.recordings || {}).sort()
                                delegate: Rectangle {
                                    width: parent.width
                                    height: 32
                                    radius: cc.tRM
                                    color: cc.tCardHi
                                    SvgIcon {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        name: "video"
                                        px: 14
                                        tint: cc.tFg3
                                    }
                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 32
                                        anchors.right: recCountTx.left
                                        anchors.rightMargin: cc.tSpS
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: cc.deviceName(modelData)
                                        color: cc.tFg
                                        font.pixelSize: cc.tLabelM
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        id: recCountTx
                                        anchors.right: parent.right
                                        anchors.rightMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: ((cc.backend.recordings[modelData] || []).length || 0) + " 段"
                                        color: cc.tFg3
                                        font.pixelSize: cc.tLabelM
                                    }
                                }
                            }
                            Text {
                                visible: Object.keys(cc.backend.recordings || {}).length === 0
                                text: "暂无回放"
                                color: cc.tFg4
                                font.pixelSize: cc.tLabelM
                                ToolTip {
                                    delay: 400
                                    text: "在本页点「开录」让被控端录一段，停录后自动上传云端，再点刷新"
                                }
                            }
                        }
                    }
                }
            }

            // ── 自习（2026-10-13）：噪音检测 + 噪音异常自动录制 ──
            // 静默监控：周期测教室噪音；**连续 N 拍超阈**才判异常 → 自动录一段摄像头
            // 并上传云端 → 记一条异常。不干扰学生（区别于考试模式的全屏拦截）。
            Rectangle {
                visible: tabs.currentIndex === 2
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
                            md3: cc.theme
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

            // ── 诊断 ──
            // 把"设备连接状态 / 绑定 / 摄像头枚举 / 最近错误（人话）+ 错误码 / 重试"聚到一屏，
            // 老师排障时不用在动作区翻找（UI 评审一.2：诊断独立成档）。
            // MD3 重构④：实时页删掉的长说明统一挪到这里（画面占位两种态是什么意思、虚拟摄像头等）。
            Rectangle {
                visible: tabs.currentIndex === 3
                anchors.fill: parent
                color: cc.tCard
                radius: cc.tRM
                Flickable {
                    anchors.fill: parent
                    anchors.margins: cc.tSpM
                    contentHeight: diagCol.height
                    clip: true
                    Column {
                        id: diagCol
                        width: parent.width
                        spacing: cc.tSpM
                        Row { spacing: cc.tSpS
                            Text { text: "选中设备"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                            Text { text: cc.selUid !== "" ? cc.deviceName(cc.selUid) : "（未选设备）"; color: cc.tFg; font.pixelSize: cc.tLabelM }
                        }
                        Row { spacing: cc.tSpS
                            Text { text: "当前绑定"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                            Text { text: cc.boundDevice() !== "" ? cc.boundDevice()
                                        : (cc.currentDev() !== "" ? cc.currentDev() + "（本地缓存）" : "（未绑定，默认 video0）")
                                   color: cc.tFg; font.pixelSize: cc.tLabelM; elide: Text.ElideRight; width: parent.width - 120 }
                        }
                        Row { spacing: cc.tSpS
                            Text { text: "摄像头"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                            Text { text: cc.loadingCams ? "枚举中…" : (cc.cameras.length + " 个"); color: cc.tFg; font.pixelSize: cc.tLabelM }
                        }
                        Column { spacing: 2
                            Text { text: "最近状态"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                            Text { text: cc.status !== "" ? cc.status : "（无）"
                                   color: cc.tFg; font.pixelSize: cc.tLabelM; width: parent.width; wrapMode: Text.WrapAnywhere }
                        }
                        Row { visible: cc.errCode !== ""
                            spacing: cc.tSpS
                            Text { text: "错误码"; color: cc.tFg3; font.pixelSize: cc.tLabelM }
                            Text { text: cc.errCode; color: cc.tFg; font.pixelSize: cc.tLabelM }
                        }
                        Md3Btn {
                            visible: cc.canRetry
                            md3: cc.theme
                            radius: cc.tRM
                            variant: "tonal"
                            text: "重试"
                            tip: "再试一次刚才没成功的操作"
                            onClicked: cc.retryLast()
                        }

                        // ── 画面说明（MD3 重构④：从实时页移来的长文字）──
                        Rectangle { width: parent.width; height: 1; color: cc.tOutlineVar }
                        Text { text: "画面说明"; color: cc.tFg3; font.pixelSize: cc.tLabelM; font.weight: Font.Medium }
                        Text {
                            width: parent.width
                            wrapMode: Text.WrapAnywhere
                            color: cc.tFg3
                            font.pixelSize: cc.tLabelS
                            text: "· 「实时」页两个画面格：左侧是被控端**屏幕**帧，右侧是**摄像头**实时流。\n"
                                  + "· 占位「连接中...」= 已经请求、还没收到首帧；「暂无画面」= 没选设备 / 没请求流 / 被控端没有摄像头。\n"
                                  + "· 摄像头格可见时会按需请被控端推流，不可见自动停（隐私红线：不看不采）。\n"
                                  + "· 虚拟摄像头（OBS / Iriun 之类）能列出来但采不到画面，列表里会标「（虚拟·无画面）」。\n"
                                  + "· 自检/抓拍/开录都会优先用「当前绑定」的摄像头；没绑定时由被控端自己回落 video0。"
                        }
                    }
                }
            }
        }

        // ④ 状态行（人话结论；可恢复的错误旁边给「重试」）
        // 2026-10-12 · UI 评审第 4 条：状态只显示**人话**结论，可恢复的错误旁边给一个「重试」。
        // FFmpeg 原生输出已在 noteError() 里被拦下（只进 console），绝不会走到这一行。
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            Text {
                id: statusLine
                anchors.left: parent.left
                anchors.right: retryTxt.visible ? retryTxt.left : parent.right
                anchors.rightMargin: retryTxt.visible ? cc.tSpS : 0
                anchors.verticalCenter: parent.verticalCenter
                text: cc.status
                color: cc.tFg4
                font.pixelSize: cc.tLabelS
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                id: retryTxt
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: cc.canRetry
                text: "重试"
                color: cc.tAcc
                font.pixelSize: cc.tLabelS
                font.weight: Font.Medium
                font.underline: retryMa.containsMouse
                MouseArea {
                    id: retryMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: cc.retryLast()
                }
            }
        }

        // ⑤ 底栏：左＝截图/推文件，右＝停止/返回（MD3 按钮，统一 12 圆角）
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: cc.tSpS
                Md3Btn {
                    md3: cc.theme
                    radius: cc.tRM
                    variant: "tonal"
                    text: "截图"
                    enabled: cc.selUid !== ""
                    tip: "对被控端屏幕截图（screenshot）"
                    onClicked: cc.screenShot()
                }
                Md3Btn {
                    md3: cc.theme
                    radius: cc.tRM
                    variant: "filled"
                    text: "文件分发"
                    enabled: cc.selUid !== ""
                    tip: "将本机文件分发到该设备"
                    onClicked: cc.pushFile()
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: cc.tSpS
                Md3Btn {
                    md3: cc.theme
                    radius: cc.tRM
                    variant: "outlined"
                    text: "停止"
                    enabled: cc.recording || cc.camStreamUid !== ""
                    tip: "一键收口：停录 + 停摄像头推流"
                    onClicked: cc.stopAll()
                }
                Md3Btn {
                    md3: cc.theme
                    radius: cc.tRM
                    variant: "text"
                    text: "返回"
                    tip: "关闭监控中心"
                    onClicked: cc.closePanel()
                }
            }
        }
    }

    // ── 逻辑 ──
    /** 可选设备（在线清单里的 uid）。喂给自绘下拉。 */
    function deviceUids() {
        var out = []
        var devs = cc.backend.devices || []
        for (var i = 0; i < devs.length; ++i)
            if (devs[i] && devs[i].uid) out.push(devs[i].uid)
        return out
    }
    /** 设备显示名：有 name 用 name，没有回落 uid（标题/诊断/回放列表共用）。 */
    function deviceName(uid) {
        if (uid === "") return ""
        var devs = cc.backend.devices || []
        for (var i = 0; i < devs.length; ++i) {
            var d = devs[i]
            if (d && d.uid === uid && d.name) return d.name
        }
        return uid
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
        cc.status = "正在枚举 " + cc.deviceName(cc.selUid) + " 的摄像头…"
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
            cc.status = "没找到摄像头。请确认摄像头已插好、没被其它程序占用，然后点右上角刷新重新枚举"
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
        cc.status = "抓拍 " + cc.deviceName(cc.selUid) + (cc.useDev() !== "" ? "（device=" + cc.useDev() + "）" : "") + "…"
        cc.backend.sendAction("camera_snapshot", cc.paramsWithDev())
    }
    function testCam() {
        cc.lastAction = "camera_test"
        cc.errCode = ""; cc.canRetry = false
        cc.status = "正在自检 " + cc.deviceName(cc.selUid) + "…"
        cc.backend.sendAction("camera_test", cc.paramsWithDev())
    }
    function startRec() {
        cc.lastAction = "camera_record_start"
        cc.errCode = ""; cc.canRetry = false
        cc.status = "正在开录 " + cc.deviceName(cc.selUid) + "…"
        cc.backend.sendAction("camera_record_start", cc.paramsWithDev())
        cc.recording = true
        cc.backend.subscribeThumbnails([cc.selUid])   // 开启到帧刷新
    }
    function stopRec() {
        cc.status = "已停录，稍后可云端回放"
        cc.backend.sendAction("camera_record_stop", {})
        cc.recording = false
    }

    // ── 底栏新动作（MD3 重构批）──
    /** 屏幕截图（screenshot）：与抓拍（camera_snapshot）是两回事——这个截整屏。 */
    function screenShot() {
        if (cc.selUid === "") return
        cc.lastAction = ""; cc.errCode = ""; cc.canRetry = false
        cc.status = "正在截图 " + cc.deviceName(cc.selUid) + "…"
        cc.backend.sendAction("screenshot", {})
    }
    /** 推文件：把当前设备设为 backend.currentUid（file 面板按它选目标），再开 file 面板。 */
    function pushFile() {
        if (cc.selUid === "") { cc.status = "先选一台设备"; return }
        if (cc.backend.currentUid !== cc.selUid) cc.backend.currentUid = cc.selUid
        if (cc.panelCtx && typeof cc.panelCtx.open === "function") cc.panelCtx.open("file")
        else cc.status = "当前宿主不支持在此打开发文件面板"
    }
    /** 一键收口：停录 + 停摄像头推流。 */
    function stopAll() {
        if (cc.recording) cc.stopRec()
        if (cc.camStreamUid !== "") {
            cc.backend.sendAction("camera_stream_stop", {})
            cc.camStreamUid = ""
            cc.camHasFrame = false
        }
        cc.status = "已停止（录制 / 推流均已收口）"
    }
    /** 关闭面板（宿主上下文注入的 closePanels；老宿主没有时不崩，只没反应）。 */
    function closePanel() {
        if (cc.panelCtx && typeof cc.panelCtx.closePanels === "function") cc.panelCtx.closePanels()
    }

    // ── 摄像头实时画面（2026-10-12 · 四·双画面 · 方案 A：RTC second track）──
    // 按需启停单一收口：期望推流设备 = 摄像头格可见且有选中设备时的 selUid，
    // 否则为空。camSync() 比较期望与实际，只在变化时发 start/stop（隐私红线：不看不采）。
    // 由 camPane.visible 变化 / selUid 变化 / 首次挂载三处触发。
    function camSync() {
        var want = (camPane.visible && cc.selUid !== "") ? cc.selUid : ""
        if (want === cc.camStreamUid) return
        if (cc.camStreamUid !== "") cc.backend.sendAction("camera_stream_stop", {})
        cc.camStreamUid = want
        cc.camHasFrame = false
        if (want !== "") {
            cc.backend.sendAction("camera_stream_start", cc.paramsWithDev())
            cc.status = "正在开摄像头（" + want + "）…"
        }
    }
    onSelUidChanged: { cc.camSync(); cc.screenHasFrame = false }
}
