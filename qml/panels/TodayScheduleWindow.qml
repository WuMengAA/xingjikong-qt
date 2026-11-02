// 今日课表 · 教室大屏窗口（2026-11-02 · 取代 QWidget 手绘版）
//
// ── 替代了什么 ──────────────────────────────────────────────────────────
// 原 `src/schedule_view.cpp` 是 `class ScheduleViewWindow : public QWidget`，
// `paintEvent()` 里 QPainter 手绘整屏：色值硬编码、字号靠 `qMax(22, h/28)` 拍。
// 三个后果都不是审美问题：① 整屏 CPU 光栅化掉帧；② 绕开设计令牌；③ 违反
// 2026-11-01 裁决「qtshadcn 是唯一图形库，不许 QWidget CPU 渲染」。
//
// ── 令牌只用 QtShadcnTheme，不 import 共享 Theme 单例 ─────────────────────
// 两个令牌源：`QtShadcnTheme`（随模块发布，ThemeManager.mode 驱动，qtshadcn
// 组件内部读的就是它）与共享 `Theme` 单例（构建期才拷进 qml/components/theme/）。
// 选前者是为一致性 —— delegate 里的 ShadcnBadge/ShadcnProgress 只认 QtShadcnTheme，
// 外壳若用另一套，"当前节反白块"与"当前节进度条"会一屏两种底色。
//
// ── 独立 ApplicationWindow 而非弹窗 + 接线 ──────────────────────────────
// 要投教室投影/大屏，得能 showFullScreen()、能拖到第二显示器；弹窗做不到。
// 先例 `qml/schedule/ScheduleEditor.qml` 同形态。接线：托盘 → C++
// `backend.requestTodaySchedule()` → 信号 todayScheduleRequested() →
// Main.qml 的 Connections 调 `schedWindow.openWindow()`。不让 C++ 直接 new
// 窗口：那会回到 QWidget/QWindow 混管、"两套渲染"的老路。
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtShadcn

ApplicationWindow {
    id: win
    /** 数据源：C++ 的 TodayScheduleModel（强类型，§10.7）。 */
    property var model: null

    QtShadcnTheme { id: theme }

    width: 1100
    height: 760
    minimumWidth: 640
    minimumHeight: 420
    title: qsTr("星集控 · 今日课表")
    color: theme.background
    visible: false

    // 大屏缩放：以 720p 为 1.0 基准，字号随之派生（理由见 TodayScheduleRow.qml）。
    readonly property real scale: Math.max(0.75, height / 720.0)

    Shortcut {
        sequence: "Escape"
        enabled: win.visibility === Window.FullScreen
        onActivated: win.showNormal()
    }
    Shortcut {
        sequence: "F11"
        onActivated: win.visibility === Window.FullScreen ? win.showNormal() : win.showFullScreen()
    }

    // ── 顶部：日期 + 一句话状态；右侧实时时钟 ──
    Item {
        id: head
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.round(96 * win.scale)

        Column {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(24 * win.scale)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(2 * win.scale)

            Text {
                text: win.model ? win.model.dateText : ""
                color: theme.foreground
                font.pixelSize: Math.round(26 * win.scale)
                font.weight: Font.Bold
            }
            Text {
                text: win.model ? win.model.statusText : ""
                color: theme.mutedForeground
                font.pixelSize: Math.round(15 * win.scale)
            }
        }

        // 右侧实时时钟（秒跳时整串不左右抖）
        Text {
            anchors.right: parent.right
            anchors.rightMargin: Math.round(24 * win.scale)
            anchors.verticalCenter: parent.verticalCenter
            text: win.model ? win.model.nowText : ""
            color: theme.foreground
            font.pixelSize: Math.round(34 * win.scale)
            font.weight: Font.Light
            font.preferShaping: false
            renderType: Text.NativeRendering
        }
    }

    // ── 主体：课程列表 ──
    ListView {
        id: list
        anchors.top: head.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: foot.top
        anchors.leftMargin: Math.round(24 * win.scale)
        anchors.rightMargin: Math.round(24 * win.scale)
        clip: true
        spacing: Math.round(8 * win.scale)
        model: win.model

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: TodayScheduleRow {
            width: list.width
            scale: win.scale
            // ⓘ required 的是上下文对象 `model` 本身（合法）；对 role 逐个
            //   required 才会因与组件属性撞名而加载期报错。
            required property var model
            slotName: model.slotName
            subject: model.subject
            timeText: model.timeText
            isNow: model.isNow
            isPast: model.isPast
            progress: model.progress
        }

        // 进入时滚到当前节（教室大屏没人去拖滚动条，必须自己送到眼前）。
        Connections {
            target: win.model
            function onCurrentChanged() { win._scrollToCurrent() }
        }
        Component.onCompleted: win._scrollToCurrent()

        // 空态：说清「为什么空」，别只画一片黑
        Column {
            anchors.centerIn: parent
            visible: list.count === 0
            spacing: Math.round(10 * win.scale)
            width: parent.width
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("今天没有排课")
                color: theme.foreground
                font.pixelSize: Math.round(22 * win.scale)
                font.weight: Font.Medium
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, Math.round(560 * win.scale))
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("课表档案里没有今天的课程安排。请用「课表编辑器」导入或新建档案。")
                color: theme.mutedForeground
                font.pixelSize: Math.round(14 * win.scale)
            }
        }
    }

    // ── 底部：全屏开关 + 快捷键提示（全屏时 Esc 可退）──
    Item {
        id: foot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.round(56 * win.scale)
        ShadcnButton {
            anchors.left: parent.left
            anchors.leftMargin: Math.round(24 * win.scale)
            anchors.verticalCenter: parent.verticalCenter
            text: win.visibility === Window.FullScreen ? qsTr("退出全屏") : qsTr("全屏显示")
            variant: ShadcnButton.Variant.Ghost
            size: ShadcnButton.Size.Small
            onClicked: win.visibility === Window.FullScreen ? win.showNormal() : win.showFullScreen()
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: Math.round(24 * win.scale)
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("F11 全屏 · Esc 退出")
            color: theme.mutedForeground
            font.pixelSize: Math.round(12 * win.scale)
            opacity: 0.7
        }
    }

    /** 把当前节滚到视线内；不在上课则回到顶部（早自习前要看到第一节）。 */
    // 把当前节滚到视线内；不在上课则回顶部（早自习前要看到第一节）。
    function _scrollToCurrent() {
        if (!model) return
        const idx = model.currentIndex
        list.positionViewAtIndex(idx >= 0 ? idx : 0, ListView.Beginning)
    }

    // 托盘入口的打开手势（统一在这里做，Main.qml 只负责转发）。
    function openWindow() {
        if (visibility === Window.FullScreen) showNormal()
        show()
        raise()
        requestActivate()
        _scrollToCurrent()
    }
}
