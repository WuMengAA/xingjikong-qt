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

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

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
        }
    }
}
