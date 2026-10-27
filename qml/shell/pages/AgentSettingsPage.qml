import QtQuick
import "../../components"
import "../../components/md3" as M

// 被控端 · 设置页（2026-10-28 模块三：按用户蓝图重做）
//
// 蓝图（手写稿）要的是「左侧分类菜单 + 右侧内容区」，七个分类：
//   通用 / 主界面 / 通知与音量 / 自动化 / 更新 / 插件 / 关于
// 本件是那次重做的落地，同时把**托盘右键菜单**里几项搬进来（用户点单：
// 「被控端的有些托盘右键功能就可以移植了」）：
//   配置自检 / 插件市场 / 噪音记录 / 自习记录 / 播放时段 / 打开配置·数据·日志目录
//   搬法是「入口」而不是复制内容 —— 那几项各自已有独立页（AgentSelfCheckPage /
//   AgentPluginsPage / AgentRecordsPage / AgentPlaySchedulePage），
//   设置页用 ctx.goToId(id) 跳过去，不重复实现一遍（两套真源必然走偏）。
//
// ⚠️ 铁律（2026-10-28 用户点名）：**QML 不写逻辑与通信代码**。
//   · 分类切换、左右内容联动、配色、按钮文案全部由 agentUi（C++）给结果值：
//     settingsSections（含 active）、tabGeneral…tabAbout（七个布尔）、
//     connStateColor / latestColor / otaModeColor / otaAutoColor / pauseBtnText / deviceRoleText
//   · 本件无 Math.*（滚动条那两处是几何比例，非业务）、无三元、无 console.log、无通信。
//   · 唯一保留的跨模块债务：VoiceHub 卡的状态文案仍在 QML 里拼（见该卡注释），
//     因为改 voicehub 模块超出本批范围 —— 已记进归档的"未做"清单。
//
// ⚠️ 被控端部署**不含 QtQuick Controls 运行库**（真机实证 ScrollBar is not a type ⇒
//    面板打开即空白）。壳与页只用 QtQuick 基础类型，这条由 test-agent-qml-deps.mjs 盯着。
Item {
    id: page

    anchors.fill: parent

    property var ctx
    readonly property var tok: (ctx && ctx.theme) ? ctx.theme : null
    // VoiceHub 模块（#329）：main.cpp 注入的 voicehub 上下文属性。
    // typeof 守卫不能省 —— 探针/极端环境里它是未定义标识符，裸引用直接 ReferenceError。
    readonly property var vh: (typeof voicehub !== "undefined" && voicehub !== null)
        ? voicehub : null

    // ── 局部组件：信息行（label 左、value 右；value 颜色由调用方给结果值）──
    component InfoRow: Item {
        id: rowRoot
        property string label: ""
        property string value: ""
        property color vColor: page.tok ? page.tok.cOnSurface : "#E6E1E5"
        anchors.left: parent.left
        anchors.right: parent.right
        height: 26
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: rowRoot.label
            font.pixelSize: 13
            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            // 右侧给 label 留 150：不再用 Math.min 夹（视觉等价，且少一处算术）
            width: parent.width - 150
            horizontalAlignment: Text.AlignRight
            text: rowRoot.value
            font.pixelSize: 13
            color: rowRoot.vColor
            elide: Text.ElideMiddle
        }
    }

    // ── 局部组件：分类卡标题（每张卡第一行的小标题）──
    component CardTitle: Text {
        property string text2: ""
        text: text2
        font.pixelSize: 14
        font.weight: Font.Medium
        color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
        bottomPadding: 6
    }

    // ══ 左：分类菜单（蓝图 w-64 ⇒ 本项目取 200，给右侧内容留够宽）══════
    Rectangle {
        id: sideNav
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 200
        color: page.tok ? page.tok.cSurfaceVar : "#F3EDF7"

        // 右缘发丝线（与内容区分界，避免侧栏"贴"在内容上）
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: page.tok ? page.tok.cOutlineVar : "#E7E0EC"
            opacity: 0.6
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 4

            Text {
                text: "设置"
                font.pixelSize: 18
                font.weight: Font.Medium
                color: page.tok ? page.tok.cOnSurface : "#1C1B1F"
                bottomPadding: 8
                leftPadding: 8
            }

            // 分类清单：label/icon/active 全由 C++ 给 —— QML 不做 key 比较
            Repeater {
                model: agentUi.settingsSections
                delegate: Rectangle {
                    id: navRow
                    required property var modelData
                    width: parent.width
                    height: 40
                    radius: 20
                    // 选中态三件（底色/字色/字重）全部取自 modelData —— C++ 算好的结果值，
                    // QML 这里没有 active ? A : B 这种三元配色判断。
                    color: modelData.rowBg
                    Behavior on color { ColorAnimation { duration: 160 } }

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        // 图标位：现在用色块占位（被控端图标名有限，不硬凑）。
                        // 选中时色块跟着变深，与文字同色系。
                        Rectangle {
                            width: 24; height: 24
                            radius: 8
                            color: modelData.fg
                            opacity: 0.16
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            font.pixelSize: 14
                            font.weight: modelData.weight
                            color: modelData.fg
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        // 只报 key，切换合法性由 C++ 判（未知 key 会被忽略并回原值）
                        onClicked: agentUi.settingsTab = navRow.modelData.key
                    }
                }
            }
        }
    }

    // ══ 右：内容区（七个分类，同一时刻只显示一个）════════════════════
    Flickable {
        id: scroller
        anchors.left: sideNav.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        contentWidth: width          // ⚠️ 不设这个 contentItem 宽度塌陷为 0 ⇒ 内容全白（真机实证）
        contentHeight: contentCol.height + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        // 切分类后回到顶部：否则从"关于"的长列表切到"通用"会停在中段，
        // 看起来像点错了（这是 UI 行为，不是业务判断）。
        Connections {
            target: agentUi
            function onSettingsTabChanged() { scroller.contentY = 0 }
        }

        Column {
            id: contentCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 20
            anchors.topMargin: 16
            spacing: 14

            // ─────────────────────────────────────────────────────
            // ① 通用：这台设备是谁、连着谁；启动项；三个目录（托盘移植）
            // ─────────────────────────────────────────────────────
            Column {
                visible: agentUi.tabGeneral
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 14

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 2
                        CardTitle { text2: "这台设备" }
                        InfoRow { label: "当前版本"; value: agentUi.versionText }
                        InfoRow {
                            label: "升级状态"
                            value: agentUi.latestText
                            vColor: agentUi.latestColor
                        }
                        InfoRow { label: "设备代号"; value: agentUi.uidText }
                        InfoRow { label: "用途";     value: agentUi.deviceRoleText }
                        InfoRow { label: "归属码";   value: agentUi.classCodeText }
                        InfoRow { label: "云端地址"; value: agentUi.cloudUrl }
                        InfoRow {
                            label: "连接状态"
                            value: agentUi.connState
                            vColor: agentUi.connStateColor
                        }
                        InfoRow { visible: agentUi.updateNotes !== ""; label: "版本说明"; value: agentUi.updateNotes }
                    }
                }

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 0
                        CardTitle { text2: "启动" }
                        // 提权类（UAC 失败时 C++ 复查回弹，界面不会假勾上）
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "开机自动启动（登录时拉起）"
                            supporting: "改动需管理员权限（弹 UAC）"
                            checked: agentUi.autostart
                            onToggled: function (v) { agentUi.autostart = v }
                        }
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "在公共桌面放快捷方式"
                            supporting: "所有登录用户可见；改动需管理员权限"
                            checked: agentUi.desktopShortcut
                            onToggled: function (v) { agentUi.desktopShortcut = v }
                        }
                    }
                }

                // 托盘右键的三条目录入口（2026-10-28 移植）
                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 10
                        CardTitle { text2: "本地目录" }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "配置文件、运行数据与日志分三个位置存放。托盘右键里原本也有这三项，" +
                                  "现在设置页同样能到 —— 现场排障不用再去摸托盘。"
                            font.pixelSize: 11
                            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                            wrapMode: Text.WordWrap
                        }
                        InfoRow { label: "数据目录"; value: agentUi.dataDir }
                        InfoRow { label: "日志文件"; value: agentUi.logPath }
                        Row {
                            spacing: 10
                            M.Md3Btn {
                                text: "打开配置目录"
                                variant: "tonal"
                                onClicked: agentUi.openConfigDir()
                            }
                            M.Md3Btn {
                                text: "打开数据目录"
                                variant: "tonal"
                                onClicked: agentUi.openDataDir()
                            }
                            M.Md3Btn {
                                text: "打开日志目录"
                                variant: "tonal"
                                onClicked: agentUi.openLogDir()
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────
            // ② 主界面：主题 / 灵动岛形态 / 插件推岛
            // ─────────────────────────────────────────────────────
            Column {
                visible: agentUi.tabInterface
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 14

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 0
                        CardTitle { text2: "外观" }
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "浅色主题"
                            supporting: "即时生效；灵动岛与面板同时切换"
                            checked: agentUi.lightMode
                            onToggled: function (v) { agentUi.lightMode = v }
                        }
                    }
                }

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 0
                        CardTitle { text2: "灵动岛" }
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "空闲时常驻"
                            supporting: "关掉后岛只在与老师交互时出现"
                            checked: agentUi.islandIdle
                            onToggled: function (v) { agentUi.islandIdle = v }
                        }
                        // P0-0（2026-10-17 · 第三方推岛治理）：
                        // 插件经本地 WS 调 island_show / notify 时先问这个开关；
                        // 关掉 = 插件推的提示一律静默丢弃（插件侧收到 dropped）。
                        // ⚠️ 只管**插件**；管理端通知、本机课表/对讲/文件提示不受影响。
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "允许第三方推送灵动岛"
                            supporting: "插件推的通知带 [插件名] 前缀；限 5 条/秒、标题 120 字"
                            checked: agentUi.pluginIslandPush
                            onToggled: function (v) { agentUi.pluginIslandPush = v }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────
            // ③ 通知与音量：弹窗 / 朗读 / 噪音监测 / VoiceHub 联动
            // ─────────────────────────────────────────────────────
            Column {
                visible: agentUi.tabNotify
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 14

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 0
                        CardTitle { text2: "通知" }
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "通知与弹窗"
                            supporting: "关掉后管理端下发的通知不显示（指令仍会执行）"
                            checked: agentUi.notifyEnabled
                            onToggled: function (v) { agentUi.notifyEnabled = v }
                        }
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "语音播报（通知朗读）"
                            supporting: "用本机 TTS 念通知标题与正文"
                            checked: agentUi.voiceEnabled
                            onToggled: function (v) { agentUi.voiceEnabled = v }
                        }
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "噪音监测"
                            supporting: "采集本机麦克风环境音量；记录在「记录」页"
                            checked: agentUi.noiseEnabled
                            onToggled: function (v) { agentUi.noiseEnabled = v }
                        }
                    }
                }

                // VoiceHub 联动（#329）：只读展示服务端的权威广播状态 + 两个偏好开关。
                // ⚠️ 跨模块债务：状态文案仍在 QML 里拼（模块没加载 / 正在播放 / 站内状态三态），
                //    因为把这三态收进 voicehub 模块属于改别的模块，超出本批范围 —— 已记归档。
                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 2
                        CardTitle { text2: "VoiceHub 联动"; bottomPadding: 6 }
                        InfoRow {
                            label: "连接状态"
                            value: {
                                if (!page.vh) return "模块未加载";
                                if (page.vh.active)
                                    return "正在播放：" + (page.vh.title || "（未知曲目）");
                                return page.vh.statusText;
                            }
                        }
                        InfoRow {
                            visible: page.vh && page.vh.active
                            label: "下一首"
                            value: (page.vh && page.vh.nextTitle) || "（无）"
                        }
                        // 服务地址：改完即重连（模块内部 stopClient+startClient）
                        M.Md3TextField {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            placeholder: "服务地址（默认官方站）"
                            text: page.vh ? page.vh.baseUrl : ""
                            onEditingFinished: if (page.vh) page.vh.baseUrl = text
                        }
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "开播提醒"
                            supporting: "有新广播时灵动岛弹一次（考试模式静默）"
                            checked: page.vh ? page.vh.autoNotify : false
                            onToggled: function (v) { if (page.vh) page.vh.autoNotify = v }
                        }
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "控制接口（点歌 / 歌词 / 广播）"
                            supporting: "留桩：接线后生效，当前只读展示不受影响"
                            checked: page.vh ? page.vh.controlEnabled : false
                            onToggled: function (v) { if (page.vh) page.vh.controlEnabled = v }
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 6
                            text: "控制接口需要 VoiceHub Open API 密钥（x-api-key，songs:request 权限）。" +
                                  "密钥存放与审计口径未定之前不接线 —— 开关只是预留。"
                            font.pixelSize: 11
                            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────
            // ④ 自动化：播放时段（托盘移植）+ 定时规则（岛「自」面）
            // ─────────────────────────────────────────────────────
            Column {
                visible: agentUi.tabAutomation
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 14

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 2
                        CardTitle { text2: "播放时段"; bottomPadding: 6 }
                        InfoRow { label: "状态";   value: agentUi.playStateText }
                        InfoRow { label: "时段";   value: agentUi.playWindowsText }
                        M.Md3SwitchRow {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "启用播放时段"
                            supporting: "只在设定时段内响应广播与媒体推送"
                            checked: agentUi.playScheduleEnabled
                            onToggled: function (v) { agentUi.playScheduleEnabled = v }
                        }
                        Row {
                            topPadding: 10
                            spacing: 10
                            M.Md3Btn {
                                text: "打开时段设置"
                                variant: "filled"
                                onClicked: if (page.ctx) page.ctx.goToId("playschedule")
                            }
                            M.Md3Btn {
                                text: "保存当前时段"
                                variant: "tonal"
                                onClicked: agentUi.savePlaySchedule()
                            }
                        }
                    }
                }

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 8
                        CardTitle { text2: "定时规则" }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "「到点自动做什么」的规则在**灵动岛的「自」面**里增删与启停 —— " +
                                  "主入口就在老师手边，不用绕到这个设置页。规则可设为通知、调音量、" +
                                  "静音、锁屏、截图或启动指定程序，全部复用既有指令白名单。"
                            font.pixelSize: 11
                            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────
            // ⑤ 更新：自动升级台账 + 渠道（含临时切换）
            // ─────────────────────────────────────────────────────
            Column {
                visible: agentUi.tabUpdate
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 14

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 2
                        CardTitle { text2: "自动升级" }
                        InfoRow {
                            label: "升级方式"
                            value: agentUi.otaModeText
                            vColor: agentUi.otaModeColor
                        }
                        InfoRow {
                            label: "自动升级"
                            value: agentUi.otaAutoText
                            vColor: agentUi.otaAutoColor
                        }
                        InfoRow { label: "上次自动升级"; value: agentUi.otaLastText }
                        InfoRow { label: "下次自动升级"; value: agentUi.otaNextText }
                        Row {
                            topPadding: 10
                            spacing: 10
                            M.Md3Btn {
                                text: "立即升级"
                                variant: "filled"
                                enabled: agentUi.canUpgrade
                                onClicked: agentUi.upgradeNow()
                            }
                            M.Md3Btn {
                                // 文案随状态变，值由 C++ 给（pauseBtnText）
                                text: agentUi.pauseBtnText
                                variant: "tonal"
                                onClicked: agentUi.toggleOtaPause()
                            }
                            M.Md3Btn {
                                text: "重新检查"
                                variant: "text"
                                onClicked: agentUi.refresh()
                            }
                        }
                    }
                }

                // 更新渠道（临时测试开关 · 2026-10-28 B4）
                // 用户点单原文：「被控端设置界面添加临时的测试更新渠道切换按钮」+「切换逻辑在 C++」。
                // 生效面：g_channel（register 上报 + OTA 查询）+ channel.txt（重启/升级后仍生效）。
                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 8
                        Row {
                            spacing: 8
                            Text {
                                text: "更新渠道"
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                            }
                            // 「临时」角标：让现场一眼看出这不是长期能力（服务端灰度上线后撤）
                            Rectangle {
                                width: 42
                                height: 19
                                radius: 9.5
                                color: "#F59E0B"
                                Text {
                                    anchors.centerIn: parent
                                    text: "临时"
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                    color: "#3B2600"
                                }
                            }
                        }
                        InfoRow { label: "当前渠道"; value: agentUi.updateChannel }
                        Row {
                            spacing: 8
                            Repeater {
                                model: agentUi.channelOptions
                                M.Md3Btn {
                                    text: modelData.label
                                    variant: modelData.variant
                                    onClicked: agentUi.updateChannel = modelData.key
                                }
                            }
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: agentUi.channelNoteText
                            font.pixelSize: 11
                            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────
            // ⑥ 插件：内置模块 + 第三方插件 + 插件市场入口（托盘移植）
            // ─────────────────────────────────────────────────────
            Column {
                visible: agentUi.tabPlugin
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 14

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 8
                        CardTitle { text2: "插件与模块" }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "插件的安装、启停与卸载在「插件」页；托盘右键的「插件市场」也是那一页。" +
                                  "这里放快捷入口，省一次底导航来回。"
                            font.pixelSize: 11
                            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                            wrapMode: Text.WordWrap
                        }
                        Row {
                            topPadding: 4
                            spacing: 10
                            M.Md3Btn {
                                text: "打开插件与模块"
                                variant: "filled"
                                onClicked: if (page.ctx) page.ctx.goToId("plugins")
                            }
                            M.Md3Btn {
                                text: "刷新清单"
                                variant: "tonal"
                                onClicked: agentUi.pluginsRefresh()
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────────────────────
            // ⑦ 关于与诊断：版本信息 + 自检 / 记录入口（托盘移植）
            // ─────────────────────────────────────────────────────
            Column {
                visible: agentUi.tabAbout
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 14

                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 2
                        CardTitle { text2: "关于星集控"; bottomPadding: 6 }
                        InfoRow { label: "被控端版本"; value: agentUi.versionText }
                        InfoRow { label: "设备用途";   value: agentUi.deviceRoleText }
                        InfoRow { label: "归属码";     value: agentUi.classCodeText }
                        InfoRow { label: "云端地址";   value: agentUi.cloudUrl }
                    }
                }

                // 托盘右键的「配置自检…」「噪音记录…」「自习记录…」三条（2026-10-28 移植）
                M.Md3Card {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    variant: "filled"
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        spacing: 2
                        CardTitle { text2: "诊断与记录"; bottomPadding: 6 }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            text: "这三项原先只能从托盘右键进。内容各自在独立页里，这里只放入口 —— " +
                                  "同一份实现在两处重复写，早晚会分叉。"
                            font.pixelSize: 11
                            color: page.tok ? page.tok.cOnSurfaceVar : "#CAC4D0"
                            wrapMode: Text.WordWrap
                            bottomPadding: 6
                        }
                        Row {
                            spacing: 10
                            M.Md3Btn {
                                text: "配置自检"
                                variant: "tonal"
                                onClicked: if (page.ctx) page.ctx.goToId("selfcheck")
                            }
                            // 「记录」页只有教室端有（广播机没有噪音/自习语义 —— 见 AgentPages.js）
                            M.Md3Btn {
                                text: "噪音与自习记录"
                                variant: "tonal"
                                onClicked: if (page.ctx) page.ctx.goToId("records")
                            }
                        }
                    }
                }
            }
        }
    }

    // ── 滚动指示条（原生自绘，不用 QtQuick.Controls）────────────────
    // 位置与长度都是 contentY/contentHeight 的**几何比例**，不是业务判断。
    Rectangle {
        id: scrollInd
        width: 3
        radius: 1.5
        visible: scroller.contentHeight > scroller.height + 1
        x: scroller.width - 6
        y: scroller.height * (scroller.contentY / Math.max(scroller.contentHeight, 1))
        height: Math.max(28, scroller.height * (scroller.height / Math.max(scroller.contentHeight, 1)))
        color: page.tok ? page.tok.cOutline : "#938F99"
        opacity: scroller.moving ? 0.85 : 0.35
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }
}
