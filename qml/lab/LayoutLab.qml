// 布局演示宿主 · LayoutLab（2026-10-14）
//
// 用户点单「看布局——不同界面的模板和简易演示 demo」：本页把五个主要界面
// 的 MD3 布局模板装进一个可切换的演示器，供逐个评价。评价通过后，
// 各 layout/*.qml 即成为对应真实界面的迁移底稿。
//
// 屏幕清单（设计尺寸）：
//   DesktopConsole 1280×800  管理端桌面集控台（导航轨+顶栏+统计+设备网格）
//   OobeConfig      560×640  被控端 OOBE 配置窗（需求 #2：只露接入码）
//   ExamMode       1280×800  被控端考试模式全屏（#88）
//   LocalSettings   560×680  被控端本机设置窗（需求 #3：本地开关组）
//   NotifyFull      960×600  被控端通知/广播全屏（需求 #4 之前的全屏态）
// 注：只 import 组件目录拿 Md3 令牌；屏幕由 Loader 按路径加载，各自文件管自己的 import。
import QtQuick
import "components"
import "components/theme"   // Theme 设计令牌单例（pragma Singleton，2026-10-25；dark 由主外壳统一写）

Item {
    id: root
    // 宿主只传 dark 布尔；各屏内部自建 Md3 实例（见各屏注释——晚赋值换实例会出事故）。
    // 本文件左侧切换轨用自己的实例（同模式）。
    property bool dark: false
    property int screen: 0
    readonly property var screens: [
        { name: "桌面集控台", short: "桌面", icon: "🖥", w: 1280, h: 800, src: "DesktopConsole.qml" },
        { name: "OOBE 配置",  short: "配置", icon: "🐋", w: 560,  h: 640, src: "OobeConfig.qml" },
        { name: "考试模式",   short: "考试", icon: "📝", w: 1280, h: 800, src: "ExamMode.qml" },
        { name: "本机设置",   short: "设置", icon: "🎚", w: 560,  h: 680, src: "LocalSettings.qml" },
        { name: "通知全屏",   short: "通知", icon: "📢", w: 960,  h: 600, src: "NotifyFull.qml" }
    ]

    Rectangle { anchors.fill: parent; color: Theme.color.background }

    // 左侧切换轨（56 宽，图标 + 短名）
    Rectangle {
        width: 56; height: parent.height
        color: Theme.color.surfaceVariant
        Column {
            anchors.top: parent.top; anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 4
            Repeater {
                model: root.screens
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: 48; height: 56
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: Theme.radius.md
                        color: root.screen === index ? Theme.color.primary : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
                    }
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.icon; font.pixelSize: 16 }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.short
                            font.pixelSize: 9
                            color: root.screen === index ? Theme.color.primaryForeground : Theme.color.textSecondary
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: root.screen = index }
                }
            }
        }
        // 底部主题指示
        Text {
            anchors.bottom: parent.bottom; anchors.bottomMargin: 12
            anchors.horizontalCenter: parent.horizontalCenter
            text: Theme.dark ? "深" : "浅"
            font.pixelSize: 10
            color: Theme.color.textSecondary
        }
    }

    // 舞台：当前屏幕按设计尺寸居中
    Item {
        anchors.left: parent.left; anchors.leftMargin: 56
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        Loader {
            id: loader
            anchors.centerIn: parent
            source: "layout/" + root.screens[root.screen].src
            onLoaded: {
                if (loader.item) {
                    loader.item.width = root.screens[root.screen].w
                    loader.item.height = root.screens[root.screen].h
                    loader.item.dark = root.dark
                }
            }
        }
        // 主题切换同步（探针/交互改 root.dark 时，已加载屏幕跟着换）
        Connections {
            target: root
            function onDarkChanged() { if (loader.item) loader.item.dark = root.dark }
        }
        // 屏幕信息标（右上角，不参与评价）
        Text {
            anchors.top: parent.top; anchors.right: parent.right
            anchors.margins: 8
            text: root.screens[root.screen].name + "  " + root.screens[root.screen].w + "×" + root.screens[root.screen].h
            font.pixelSize: 10
            color: root.dark ? "#938F99" : "#79747E"
        }
    }
}
