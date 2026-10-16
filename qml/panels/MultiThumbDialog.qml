import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../schedule"

// MultiThumbDialog（U3 从 Main.qml 抽出 · 多班缩略图墙）
// ───────────────────────────────────────────────────────────────────────────
// 契约（见 Stelarith-shared-qml/ui/UiContract.js）：
//   · Popup 自带 open()/close() ⇒ entry 契约天然满足
//   · `property var ctx` 收宿主注入的上下文：ctx.window（原 `root.`）、ctx.open/ctx.closePanels
//   · `property var theme` 给本文件自己的绘制用（token 对象）
//
// ⚠️ 内容与抽出前逐字相同，只做了四类机械替换（语义不变）：
//      `root.X` → `ctx.window.X`
//      裸 `th.foo`（属性读取）→ `theme.foo`
//      裸 `th`（作为取值，如 `Btn { theme: th }`）→ `ctx.window.th`
//        ⚠️ 不能用本地 `theme`：那会变成 `theme: theme` 自引用绑定
//      跨面板 `controlDlg.close()` → `ctx.closePanels()`
Popup {
    id: multiThumbDlg
    property var ctx
    property var theme: ctx ? ctx.window.th : null

    anchors.centerIn: Overlay.overlay
    width: 640
    height: 520
    modal: true
    padding: 0
    background: Rectangle {
        color: theme.panel
        radius: theme.rCard
        border.color: theme.card
        border.width: 1
    }
    onOpened: multiThumb.watch()
    onClosed: backend.subscribeThumbnails([])

    Column {
        id: thumbCol
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Row {
            width: parent.width
            spacing: 8
            Text { text: "多班监控（缩略图墙）"; color: theme.fg3; font.pixelSize: 11 }
            Rectangle {
                width: 64; height: 26; radius: theme.rCtrl
                color: theme.inv
                Text { anchors.centerIn: parent; text: "刷新"; color: theme.win; font.pixelSize: 12 }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        multiThumb.watch()
                        ctx.window.toast("正在订阅多班缩略图…")
                    }
                }
            }
        }

        // 网格：最多 9 台（在线优先），每格一个设备的缩略图
        GridLayout {
            id: multiThumb
            width: parent.width
            columns: 3
            columnSpacing: 6
            rowSpacing: 6

            property var watched: []   // 已订阅的设备 uid

            function watch() {
                // 收集在线的设备 uid（最多 9 台），保持设备表顺序
                var uids = []
                var devs = backend.devices || []
                for (var i = 0; i < devs.length && uids.length < 9; ++i) {
                    if (devs[i] && devs[i].online && devs[i].uid
                            && uids.indexOf(devs[i].uid) < 0)
                        uids.push(devs[i].uid)
                }
                watched = uids
                backend.subscribeThumbnails(uids)
            }

            Repeater {
                model: multiThumb.watched
                delegate: Rectangle {
                    id: cell
                    width: (multiThumb.width - 12) / 3
                    height: 96
                    radius: theme.rCtrl
                    color: theme.canvas
                    border.color: theme.stroke
                    border.width: 1
                    clip: true

                    // 缩略图：帧更新时 tick++ 换缓存键
                    Image {
                        id: thumbImg
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        // ⚠️ 2026-10-10：tick 是**本 Image** 的属性，原来却写 `cell.tick++`
                        //   （cell 是那一格的 Rectangle，没这属性）⇒ 每来一帧都刷
                        //   `Cannot assign to non-existent property "tick"`，缓存键永不刷新、
                        //   缩略图看着"不更新"。改用 Image 自己的 id 引用。
                        property int tick: 0
                        source: "image://frames/" + modelData + "?t" + tick
                        Connections {
                            target: backend
                            function onThumbnailChanged(uid) { if (uid === modelData || uid === "") thumbImg.tick++ }
                        }
                    }

                    // uid 标签（底部小条，别盖住画面太多）
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 18
                        color: Qt.rgba(0, 0, 0, 0.55)
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: "#FAFAFA"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                            width: parent.width - 8
                        }
                    }

                    // 点击 → 切到这台看大画面（控制页）
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            backend.setCurrentUid(modelData)
                            ctx.window.page = 1   // 集控页（带大画面）
                            multiThumbDlg.close()
    ctx.closePanels()
                            ctx.window.toast("已切换查看 " + modelData)
                        }
                    }

                    // 空态：还没收到这台帧
                    Text {
                        anchors.centerIn: parent
                        visible: thumbImg.tick === 0
                        text: "等待画面…"
                        color: theme.fg4
                        font.pixelSize: 10
                    }
                }
            }
        }

        Text {
            text: "最多 9 台在线；点格切换大画面"
            color: theme.fg3; font.pixelSize: 10; wrapMode: Text.Wrap
        }
    }
}