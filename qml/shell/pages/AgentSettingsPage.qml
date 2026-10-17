import QtQuick
import "../../components"
import "../../components/md3" as M

// 被控端 · 设置页（U5-A3 第一批 · 2026-10-16）
//
// 内容 = QtWidgets 版 `openInfoDialog()`（main.cpp 2172–2387）的真实迁移：
//   信息（版本/设备代号/云端地址/连接状态/数据目录/日志）
//   + 升级状态四态与 OTA 台账（方式/自动/上次/下次）
//   + 开机自启 / 桌面快捷（提权改动，写完由 C++ 复查真实状态 —— 失败时开关回弹）
//   + 功能开关 5 个（通知/语音播报/噪音监测/灵动岛常驻/浅色主题；后两个实时作用于岛）
//   + 动作（立即升级 / 暂停-恢复自动升级 / 打开日志与数据目录）
// 视觉 = MD3（共享 ui 令牌 tok 由宿主经 ctx 传入）。
//
// ⚠️ 令牌 id 用 `tok` 不用 `md3`（子组件属性也叫 md3 ⇒ `md3: md3` 是自引用绑定环）。
Item {
    id: page

    anchors.fill: parent

    property var ctx
    readonly property var tok: (ctx && ctx.theme) ? ctx.theme : null
    // VoiceHub 联动模块（#329）：main.cpp 注入的 voicehub 上下文属性。
    // ⚠️ typeof 守卫不能省 —— 探针/极端环境（模块没注册）里它是未定义标识符，
    //    裸引用直接 ReferenceError（island 侧 bridgeRef 同款纪律）。
    readonly property var vh: (typeof voicehub !== "undefined" && voicehub !== null)
        ? voicehub : null

    // ── 局部组件：信息行（label 左、value 右；同 viewer 设置页卡片的心智）──
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
            width: Math.min(implicitWidth, parent.width - 150)
            horizontalAlignment: Text.AlignRight
            text: rowRoot.value
            font.pixelSize: 13
            color: rowRoot.vColor
            elide: Text.ElideMiddle
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width          // ⚠️ 不设这个 contentItem 宽度塌陷为 0 ⇒ 内容全白（真机截图实证）
        contentHeight: col.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 20
            anchors.topMargin: 16
            spacing: 14

            // ── 卡 1：这台机器是谁、连着谁 ─────────────────────
            M.Md3Card {
                anchors.left: parent.left
                anchors.right: parent.right
                md3: page.tok
                variant: "filled"

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 2

                    Text {
                        text: "这台机器"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                        bottomPadding: 6
                    }
                    InfoRow { label: "当前版本";    value: "v" + agentUi.appVersion }
                    InfoRow {
                        label: "升级状态"
                        value: agentUi.latestText
                        vColor: agentUi.hasNewer
                                ? (page.tok ? page.tok.cWarn : "#D4A017")
                                : (page.tok ? page.tok.cOk : "#4CC38A")
                    }
                    InfoRow { label: "设备代号";    value: agentUi.uid === "" ? "（未注册）" : agentUi.uid }
                    InfoRow { label: "云端地址";    value: agentUi.cloudUrl }
                    InfoRow {
                        label: "连接状态"
                        value: agentUi.connState
                        vColor: agentUi.connState.indexOf("已连接") === 0
                                ? (page.tok ? page.tok.cOk : "#4CC38A")
                                : (page.tok ? page.tok.cWarn : "#D4A017")
                    }
                    InfoRow { label: "数据目录";    value: agentUi.dataDir }
                    InfoRow { label: "日志文件";    value: agentUi.logPath }
                    InfoRow { visible: agentUi.updateNotes !== ""; label: "版本说明"; value: agentUi.updateNotes }
                }
            }

            // ── 卡 2：自动升级台账 + 动作 ──────────────────────
            M.Md3Card {
                anchors.left: parent.left
                anchors.right: parent.right
                md3: page.tok
                variant: "filled"

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 2

                    Text {
                        text: "自动升级"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                        bottomPadding: 6
                    }
                    InfoRow {
                        label: "升级方式"
                        value: agentUi.otaModeText
                        vColor: agentUi.otaModeText.indexOf("强制") === 0
                                ? (page.tok ? page.tok.cError : "#B3261E")
                                : (page.tok ? page.tok.cOnSurface : "#E6E1E5")
                    }
                    InfoRow {
                        label: "自动升级"
                        value: agentUi.otaAutoText
                        vColor: agentUi.otaPausedNow
                                ? (page.tok ? page.tok.cError : "#B3261E")
                                : (page.tok ? page.tok.cOnSurface : "#E6E1E5")
                    }
                    InfoRow { label: "上次自动升级"; value: agentUi.otaLastText }
                    InfoRow { label: "下次自动升级"; value: agentUi.otaNextText }

                    Row {
                        topPadding: 10
                        spacing: 10
                        M.Md3Btn {
                            md3: page.tok
                            text: "立即升级"
                            variant: "filled"
                            enabled: agentUi.canUpgrade
                            onClicked: agentUi.upgradeNow()
                        }
                        M.Md3Btn {
                            md3: page.tok
                            text: agentUi.otaPausedNow ? "恢复自动升级" : "暂停自动升级"
                            variant: "tonal"
                            onClicked: agentUi.toggleOtaPause()
                        }
                        M.Md3Btn {
                            md3: page.tok
                            text: "打开日志目录"
                            variant: "text"
                            onClicked: agentUi.openLogDir()
                        }
                        M.Md3Btn {
                            md3: page.tok
                            text: "打开数据目录"
                            variant: "text"
                            onClicked: agentUi.openDataDir()
                        }
                    }
                }
            }

            // ── 卡 3：开关 ─────────────────────────────────────
            M.Md3Card {
                anchors.left: parent.left
                anchors.right: parent.right
                md3: page.tok
                variant: "filled"

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 0

                    Text {
                        text: "开关"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                        bottomPadding: 6
                    }
                    // 提权类（UAC 失败时 C++ 复查回弹，界面不会假勾上）
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "开机自动启动（登录时拉起）"
                        supporting: "改动需管理员权限（弹 UAC）"
                        checked: agentUi.autostart
                        onToggled: function (v) { agentUi.autostart = v }
                    }
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "在公共桌面放快捷方式"
                        supporting: "所有登录用户可见；改动需管理员权限"
                        checked: agentUi.desktopShortcut
                        onToggled: function (v) { agentUi.desktopShortcut = v }
                    }
                    // 功能开关（勾选即生效；后两个实时作用于灵动岛）
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "通知与弹窗"
                        checked: agentUi.notifyEnabled
                        onToggled: function (v) { agentUi.notifyEnabled = v }
                    }
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "语音播报（通知朗读）"
                        checked: agentUi.voiceEnabled
                        onToggled: function (v) { agentUi.voiceEnabled = v }
                    }
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "噪音监测"
                        checked: agentUi.noiseEnabled
                        onToggled: function (v) { agentUi.noiseEnabled = v }
                    }
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "灵动岛空闲常驻"
                        supporting: "关掉后岛只在与老师交互时出现"
                        checked: agentUi.islandIdle
                        onToggled: function (v) { agentUi.islandIdle = v }
                    }
                    // P0-0（2026-10-17 · 灵动岛第三方推岛治理）：
                    // 插件经本地 WS 调 island_show / notify 时，先问这个开关。
                    // 关掉 = 插件推的提示一律不显示（静默丢弃 + 计数，插件侧收到 dropped）。
                    // ⚠️ 只管**插件**；管理端下发的通知、本机课表/对讲/文件提示不受影响。
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "允许第三方推送灵动岛"
                        supporting: "插件推的通知带 [插件名] 前缀；限 5 条/秒、标题 120 字"
                        checked: agentUi.pluginIslandPush
                        onToggled: function (v) { agentUi.pluginIslandPush = v }
                    }
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "浅色主题"
                        checked: agentUi.lightMode
                        onToggled: function (v) { agentUi.lightMode = v }
                    }
                }
            }

            // ── 卡：VoiceHub 联动（#329 · 2026-10-17）─────────────
            // 只读展示 VoiceHub 服务端的权威广播状态：灵动岛展开态一行
            // 「正在播放 / 进度 / 下一首 / 人数」+ 开播提醒一次。
            // 数据面 = 内置 voicehub 模块（main.cpp 注入的 voicehub 上下文属性；
            // 模块停用只停它的客户端，对象与配置属性仍在，本卡照常可配）。
            // ⚠️ 经 page.vh 访问（typeof 守卫版）：模块没注册时显示「模块未加载」，
            //    不是 ReferenceError。
            M.Md3Card {
                anchors.left: parent.left
                anchors.right: parent.right
                md3: page.tok
                variant: "filled"

                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: 2

                    Text {
                        text: "VoiceHub 联动"
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        color: page.tok ? page.tok.cOnSurface : "#E6E1E5"
                        bottomPadding: 6
                    }
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
                        md3: page.tok
                        placeholder: "服务地址（默认官方站）"
                        text: page.vh ? page.vh.baseUrl : ""
                        onEditingFinished: if (page.vh) page.vh.baseUrl = text
                    }
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
                        text: "开播提醒"
                        supporting: "有新广播时灵动岛弹一次（考试模式静默）"
                        checked: page.vh ? page.vh.autoNotify : false
                        onToggled: function (v) { if (page.vh) page.vh.autoNotify = v }
                    }
                    M.Md3SwitchRow {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        md3: page.tok
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
    }

    // ── 滚动指示条（原生自绘，不用 QtQuick.Controls）────────────────
    // ⚠️ 被控端部署**不含 QtQuick Controls 运行库**（真机实证：`ScrollBar is not a type`
    //    ⇒ 本页 entry 解析失败 ⇒ 面板打开即空白，2026-10-16）。壳与页只用 QtQuick 基础类型，
    //    这条硬约束由共享门禁 scripts/test-agent-qml-deps.mjs 盯着。
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
