import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// MD3 布局组件（Md3Card / Md3Btn…）：真源在 Stelarith-shared-qml/ui/md3/，
// 构建期 copy_directory 到 qml/Stelarith/components/md3/ —— 源树下 qmllint
// 解析不到（磁盘加载模式），以「构建 + 运行」验证为准，与集控页同套路。
import "../components/md3"
import "../island"         // SvgIcon

// 班级详情页（整屏页 · page 5）· V1.0 P1
//
// 蓝图对应：「班级独立详情页（核心控制中枢）」——
//   层级 1：班级概览 + 批量操作（顶部班名/人数/课表 + 设备网格 + 一键锁屏等）
//   层级 2：单台设备（右侧面板：VNC 实时画面 + 截图/锁定桌面/远程重启）
//
// ── 数据真源（不自己另起一份） ───────────────────────────────────────
// 班级：backend.currentClass（看板进入时已由 backend.setCurrentClass 写入）。
// 设备：backend.devices 按 currentClass 过滤 —— 刻意**不用** ctx.window.filteredDevices：
//       后者还叠加了「全部/在线/离线」筛选（集控页左栏那三枚 chip 的状态），
//       老师在那边筛过"离线"再进详情页会只剩离线设备，看着像"这个班没人"。
//       详情页要的是「这个班全部设备」，自己按班级过滤才对得上语义。
// 画面：backend.frameSource + image://vnc（VNC 链路）/ image://frames（轮询链路），
//       与集控页中栏、监控页同一套取帧方式，tick 递增逼 Image 重取。
//
// ── 刻意不做的事（避免两份维护） ─────────────────────────────────────
// 1. **远控鼠标/键盘输入**：集控页中栏那套 fitRect / normOf / vkOf 是踩过三次坑
//    才稳定的（黑边归一化、INT_MIN、修饰键），复制一份就是两份要修的 bug。
//    本页只显示画面 + 下发动作；要操作那台设备，用右下「在集控页打开」。
// 2. **课表**：蓝图写了「课表跳转」，但现有课表入口是主界面弹窗（ScheduleDialog），
//    本页拿不到它的句柄也不该猜 → 放占位按钮 + 说明，接线等单独点单。
// 3. **摄像头画面与回放**：被控端当前**没有摄像头采集通道**（recordings 是屏幕录像），
//    面板右侧给占位并写明，不做假画面。
//
// ctx 形状与集控页同一份契约（Main.qml 的 _pageCtx）。
Item {
    id: page

    property var ctx

    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

    // 当前展开的设备（页内状态；选中态真源仍是 ctx.window.currentUid ——
    // 面板开哪台由本页记住，画面订阅交给宿主的 pickDevice 去切）。
    property string openUid: ""

    Rectangle { anchors.fill: parent; color: th.body }

    // 本班设备：按班级码过滤（见文件头为什么不用 filteredDevices）
    function classDevices() {
        var devs = (backend && backend.devices) ? backend.devices : []
        var cc = backend.currentClass || ""
        var out = []
        for (var i = 0; i < devs.length; ++i) {
            var d = devs[i]
            if (!d) continue
            if ((d.classCode || "") === cc) out.push(d)
        }
        return out
    }

    function className() {
        var cs = (backend && backend.classes) ? backend.classes : []
        var cc = backend.currentClass || ""
        for (var i = 0; i < cs.length; ++i) if (cs[i].code === cc) return cs[i].name || cs[i].code
        return cc === "" ? "全部班级" : cc
    }

    // 点一台：走宿主的 pickDevice（无修饰键 = 单选 + 把画面切过去），
    // 与集控页左栏点设备是同一条路，订阅/退订逻辑只有一份。
    function openDevice(uid) {
        page.openUid = uid
        ctx.window.pickDevice(uid, 0, 0)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // ── 顶栏：返回 / 班名 / 人数 / 课表 / 班级批量 ──────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Md3Btn {
                md3: th
                text: "返回看板"
                variant: "text"
                onClicked: ctx.window.goPage(4)
            }

            Text {
                text: page.className()
                color: th.fg
                font.pixelSize: 16
                font.weight: Font.Medium
            }
            Text {
                text: {
                    var ds = page.classDevices()
                    var on = 0
                    for (var i = 0; i < ds.length; ++i) if (ds[i].online !== false) on++
                    return ds.length + " 台 · " + on + " 台在线"
                }
                color: th.fg3
                font.pixelSize: 12
            }

            Item { Layout.fillWidth: true }

            // 课表入口：占位。真接线要够到主界面的 ScheduleDialog，
            // 本页不该去猜它的 id（猜了就是第二个"页面互相够"的耦合点）。
            Md3Btn {
                md3: th
                text: "课表"
                variant: "outlined"
                enabled: false
                tip: "课表入口待接入"
            }

            // 班级级批量：选全班设备后下发（与看板批量同一条执行入口）
            Md3Btn {
                md3: th
                text: "锁定全班桌面"
                variant: "tonal"
                onClicked: {
                    var ds = page.classDevices()
                    var uids = []
                    for (var i = 0; i < ds.length; ++i) if (ds[i].online !== false) uids.push(ds[i].uid)
                    if (uids.length === 0) return
                    ctx.window.picked = uids
                    ctx.window.runActionById("pick-lock")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ── 主区：设备网格 ─────────────────────────────────
            GridView {
                id: devGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: 132
                cellHeight: 84
                model: page.classDevices()

                delegate: Item {
                    width: devGrid.cellWidth
                    height: devGrid.cellHeight

                    Md3Card {
                        anchors.fill: parent
                        anchors.margins: 6
                        md3: th
                        variant: (modelData.uid === page.openUid) ? "elevated" : "filled"

                        Column {
                            anchors.fill: parent
                            spacing: 4
                            Row {
                                spacing: 6
                                Rectangle {
                                    width: 6; height: 6; radius: 3
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: (modelData.online !== false) ? th.ok : th.err
                                }
                                Text {
                                    width: 96
                                    text: (modelData.name && modelData.name !== "")
                                          ? modelData.name : modelData.uid
                                    color: th.cOnSurface
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                }
                            }
                            Text {
                                width: parent.width
                                // 在线给版本号，离线给"多久没见"——都是云端真字段，
                                // 没有就不画（见集控页同一处注释：不编数据）
                                text: (modelData.online !== false)
                                      ? ((modelData.version && modelData.version !== "") ? modelData.version : "在线")
                                      : ((modelData.lastSeenAgoSec !== undefined && modelData.lastSeenAgoSec >= 0)
                                         ? (modelData.lastSeenAgoSec < 60 ? "刚断线"
                                            : Math.floor(modelData.lastSeenAgoSec / 60) + " 分前")
                                         : "离线")
                                color: th.cOnSurfaceVar
                                font.pixelSize: 12
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.openDevice(modelData.uid)
                    }
                }
            }

            // ── 右侧：单机面板（点设备滑出） ─────────────────────
            // 桌面端全项目只有手机端用 Drawer（DeviceDetailDrawer）；
            // 桌面是整屏 StackLayout，抽屉会盖住整页 —— 这里用页内面板 + 滑入动画，
            // 蓝图写的「右侧滑出抽屉，或页面内原地展开一个面板」取后一种。
            Rectangle {
                Layout.preferredWidth: 420
                Layout.fillHeight: true
                radius: th.rL
                color: th.cContLow
                border.color: th.cOutlineVar
                border.width: 1
                visible: page.openUid !== ""

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: {
                                var ds = page.classDevices()
                                for (var i = 0; i < ds.length; ++i)
                                    if (ds[i].uid === page.openUid)
                                        return (ds[i].name && ds[i].name !== "") ? ds[i].name : ds[i].uid
                                return page.openUid
                            }
                            color: th.cOnSurface
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }
                        Item { Layout.fillWidth: true }
                        Md3Btn {
                            md3: th
                            text: "关闭"
                            variant: "text"
                            onClicked: page.openUid = ""
                        }
                    }

                    // 画面：VNC 链路走 image://vnc，轮询链路走 image://frames，
                    // 与集控页中栏完全一致（tick 递增逼 Image 重取，source 不变 Qt 不重读）。
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 260
                        radius: th.rM
                        color: th.panel
                        border.color: th.canvas
                        border.width: 1
                        clip: true

                        Image {
                            id: shotImg
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            visible: backend.frameSource !== "vnc"
                            property int tick: 0
                            source: ctx.window.frameReady ? ("image://frames/f?" + tick) : ""
                            Connections {
                                target: backend
                                function onFrameChanged() { shotImg.tick++ }
                            }
                        }
                        Image {
                            id: vncImg
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            visible: backend.frameSource === "vnc"
                            property int tick: 0
                            source: backend.frameSource === "vnc" ? ("image://vnc/live?" + tick) : ""
                        }
                        Connections {
                            target: backend
                            function onVncFrameReady(img) { vncImg.tick++ }
                        }

                        Column {
                            anchors.centerIn: parent
                            visible: backend.frameSource !== "vnc" && !ctx.window.frameReady
                            spacing: 6
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: ctx.window.linkBusy ? "正在接通" : "等画面"
                                color: th.op
                                font.pixelSize: 13
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: ctx.window.linkBusy ? "连上就有画面" : "这台设备还没发来第一帧"
                                color: th.fg3
                                font.pixelSize: 12
                            }
                        }

                        // 链路徽标：与集控页同一语义（实时 = 反白强调）
                        Text {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 8
                            text: backend.frameSource === "vnc" ? "实时" : "轮询"
                            color: backend.frameSource === "vnc" ? th.win : th.fg3
                            font.pixelSize: 12
                        }
                    }

                    // 操作栏：截图 / 锁定桌面 / 远程重启
                    // 三条都是 ActionDefs.js 里的声明（screenshot / lock / reboot），
                    // 走 runActionById ⇒ 权限门控、能力门控、危险动作二次确认全部复用。
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: ["screenshot", "lock", "reboot"]
                            delegate: Md3Btn {
                                md3: th
                                Layout.fillWidth: true
                                text: modelData === "screenshot" ? "屏幕截图"
                                    : modelData === "lock" ? "锁定桌面" : "远程重启"
                                variant: (modelData === "lock" || modelData === "reboot") ? "error" : "tonal"
                                radius: 12
                                onClicked: ctx.window.runActionById(modelData)
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "鼠标键盘操作请到「集控」页：那里才有一套稳定的坐标换算，复制过来就是两份要修的 bug。"
                        color: th.fg4
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    Md3Btn {
                        md3: th
                        text: "在集控页打开"
                        variant: "outlined"
                        onClicked: ctx.window.goPage(1)
                    }

                    // 摄像头画面与回放：占位（被控端暂无摄像头采集通道）
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        radius: th.rM
                        color: "transparent"
                        border.color: th.cOutlineVar
                        border.width: 1
                        Column {
                            anchors.centerIn: parent
                            spacing: 4
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "摄像头画面"
                                color: th.cOutline
                                font.pixelSize: 13
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "待接入：被控端暂无摄像头采集通道"
                                color: th.cOutline
                                font.pixelSize: 12
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        // 空态：这个班还没有设备
        Column {
            visible: page.classDevices().length === 0
            Layout.fillWidth: true
            topPadding: 40
            spacing: 6
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "这个班还没有设备"
                color: th.fg
                font.pixelSize: 14
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "设备绑定班级之后会出现在这里"
                color: th.fg3
                font.pixelSize: 12
            }
        }
    }
}
