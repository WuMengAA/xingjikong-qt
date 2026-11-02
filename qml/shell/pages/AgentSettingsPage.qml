// 被控端 · 设置页（2026-10-28 模块三：按用户蓝图重做；同日拆分九个 Sec）
//
// 蓝图要的是「左侧分类菜单 + 右侧内容区」，七个分类：
//   通用 / 主界面 / 通知与音量 / 自动化 / 更新 / 插件 / 关于
// 同时把**托盘右键**里几项搬进来（用户点单「有些托盘右键功能可以移植了」）：
//   配置自检 / 插件市场 / 噪音记录 / 自习记录 / 播放时段 / 打开配置·数据·日志目录
// 搬法是「入口」而不是复制内容 —— 那几项各自已有独立页，这里用 ctx.goToId(id) 跳过去。
//
// 2026-10-28 拆分（台账 [P0] 欠账）：内容按分类搬进 settings/Sec*.qml（九个），
// 本文件只留**骨架**：分类菜单 + 滚动容器 + 九个可见性绑定 + 重置确认 + 滚动条。
// 动机：拆前 1016 行，按蓝图再补「主界面/通知/自动化/更新-插件细化」会到 1500+。
// 「通用」拆三件（单件超 200 行红线），三段在主文件里按原顺序排列，视觉顺序不变。
//
// 2026-10-30 用户点单「全部换新」：
//   ① 删 `import "../../ash"` 与 `AshTheme { id: ash }` —— 令牌改由共享库 `Theme` 单例提供，
//      明暗在 AgentShell 统一绑（`Theme.dark = !agentUi.lightMode`，与 ash 的
//      `dark: agentUi.darkTheme` 等价：C++ 里 `darkTheme()` 就是 `!m_lightMode`）。
//   ② 九个 Sec* 不再接收 `ash:` 注入（各件已自带 Theme 依赖）。
//   ③ 重置确认由 AshDialog 重写为 AppDialog（新件是"内容容器 + 受控 opened"，
//      不再吃 `actions[]` 数组与 onActionClicked(index, role) 那套约定）。
//   ⑤ 顺带修正 `contentCol` 只写了 `anchors.topMargin` 却没写 `anchors.top`
//      ⇒ topMargin 一直无效、内容贴顶。补 `anchors.top: parent.top` 让 16 的上边距真正生效。
//
// ⚠️ 铁律（2026-10-28 用户点名）：**QML 不写逻辑与通信代码**。
//   · 分类切换、左右内容联动、配色、按钮文案全部由 agentUi（C++）给结果值：
//     settingsSections（含 active）、tabGeneral…tabAbout（七个布尔）、
//     connStateColor / latestColor / otaModeColor / otaAutoColor / pauseBtnText / deviceRoleText
//   · 本件无业务计算：可见性绑 C++ 布尔、配色绑 C++ 结果色；
//     滚动条那两处是几何比例（非业务）。
//
// ⚠️ 被控端部署**不含 QtQuick Controls 运行库**（真机实证 ScrollBar is not a type ⇒
//    面板打开即空白）。壳与页只用 QtQuick 基础类型，这条由 test-agent-qml-deps.mjs 盯着。
import QtQuick
import QtQuick.Layouts

import "settings"   // 九个分类内容件（2026-10-28 拆分）
import "../../components"
import "../../components/theme"
// 分类行真图标：shared-qml/island/SvgIcon.qml，构建期拷到 qml/island/ ⇒ 相对路径两级。
// 图标名由 C++ settingsSections 的 icon 字段给，着色用同一项的 fg（选中/未选中已由 C++ 算好）。
import "../../island"

Item {
    id: page

    anchors.fill: parent

    property var ctx

    // VoiceHub 模块（#329）：main.cpp 注入的 voicehub 上下文属性。
    // typeof 守卫不能省 —— 探针/极端环境里它是未定义标识符，裸引用直接 ReferenceError。
    readonly property var vh: (typeof voicehub !== "undefined" && voicehub !== null)
        ? voicehub : null

    // ══ 左：分类菜单（2026-10-28 抽成独立件 settings/SettingsNav.qml）══
    SettingsNav {
        id: sideNav
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
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
            anchors.top: parent.top
            anchors.topMargin: 16
            spacing: 14
            // 通用 · 设备与启动
            SecGeneral {
                visible: agentUi.tabGeneral
                ctx: page.ctx
                vh: page.vh
                width: parent.width
            }
            // 通用 · 服务与时间
            SecGeneralService {
                visible: agentUi.tabGeneral
                ctx: page.ctx
                vh: page.vh
                width: parent.width
            }
            // 通用 · 存储·高级·目录
            SecGeneralLocal {
                visible: agentUi.tabGeneral
                ctx: page.ctx
                vh: page.vh
                width: parent.width
                // 重置确认框留在主文件（它是页面级浮层，不属于某一类）
                onResetRequested: resetDialog.opened = true
            }
            // 主界面
            SecAppearance {
                visible: agentUi.tabInterface
                ctx: page.ctx
                vh: page.vh
                width: parent.width
            }
            // 通知与音量
            SecNotify {
                visible: agentUi.tabNotify
                ctx: page.ctx
                vh: page.vh
                width: parent.width
            }
            // 自动化
            SecAutomation {
                visible: agentUi.tabAutomation
                ctx: page.ctx
                vh: page.vh
                width: parent.width
            }
            // 更新
            SecUpdate {
                visible: agentUi.tabUpdate
                ctx: page.ctx
                vh: page.vh
                width: parent.width
            }
            // 插件
            SecPlugin {
                visible: agentUi.tabPlugin
                ctx: page.ctx
                vh: page.vh
                width: parent.width
            }
            // 关于与诊断
            SecAbout {
                visible: agentUi.tabAbout
                ctx: page.ctx
                vh: page.vh
                width: parent.width
            }
        }
    }

    // ── 重置确认（高级选项）────────────────────────────────────────
    // 二次确认是硬要求：这一步会删掉云端地址与令牌，点错就得现场重填地址。
    // 2026-10-30：AshDialog → AppDialog。新件只提供遮罩/卡片/动效与 `opened` 受控开关，
    // 标题、正文、按钮都由调用方在内部 ColumnLayout 里写（不再吃 actions[] 数组约定）。
    AppDialog {
        id: resetDialog
        onClosed: resetDialog.opened = false

        Text {
            Layout.fillWidth: true
            text: "重置全部设置？"
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: Theme.color.textPrimary
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: agentUi.resetScopeText
            font.pixelSize: 14
            color: Theme.color.textSecondary
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing.sm
            spacing: Theme.spacing.sm

            Item { Layout.fillWidth: true }   // 撑开，把两个动作推到右侧

            AppButton {
                text: "取消"
                variant: "secondary"
                onClicked: resetDialog.opened = false
            }
            AppButton {
                text: "确认重置"
                variant: "destructive"
                onClicked: {
                    resetDialog.opened = false
                    agentUi.resetAllSettings()
                }
            }
        }
    }

    // ── 滚动指示条（原生自绘，不用 QtQuick.Controls）────────────────
    // 位置与长度都是 contentY/contentHeight 的**几何比例**，不是业务判断。
    // ⚠️ 2026-10-30 自查修：`x` 是**相对父坐标**（本件与 scroller 同级，父＝页面根），
    //    原来写 `x: scroller.width - 6` 只算了 Flickable 的宽度、漏了它自身的 x 偏移
    //    （=侧栏宽 200）⇒ 指示条落在内容区中间，截图上是一条"凭空竖线"
    //    （x=775 逻辑像素处，实测 #48484D）。改为 `scroller.x + scroller.width`。
    //    y 无此问题：scroller 上下都锚到父顶底，y 恒 0，局部即全局。
    Rectangle {
        id: scrollInd
        width: 3
        radius: 1.5
        visible: scroller.contentHeight > scroller.height + 1
        x: scroller.x + scroller.width - 9
        y: scroller.height * (scroller.contentY / Math.max(scroller.contentHeight, 1))
        height: Math.max(28, scroller.height * (scroller.height / Math.max(scroller.contentHeight, 1)))
        color: Theme.color.textSecondary
        opacity: scroller.moving ? 0.85 : 0.35
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }
}
