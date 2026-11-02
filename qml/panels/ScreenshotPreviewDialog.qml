import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// ScreenshotPreviewDialog（2026-10-18 · 用户反馈「截图后没有传回图片且没有预览界面」）
// ───────────────────────────────────────────────────────────────────────────
// 修的问题：老师点「截图」→ 被控端在**教室机本地**存了一张 PNG → 管理端只弹一句
// toast「已截图 → 教室机 xxx：C:\...\shot-123.png」。老师**看不到图**，还得跑到
// 那台机器上翻文件。实测（Main.qml:233）回执里只有 path/bytes/width/height。
//
// 本组件是**接收侧**：读回执里的 `data.thumb`（JPEG 的 base64），显示预览。
//   · 没有 thumb（旧版被控端 / 回执没带图）⇒ 明确提示"该设备未回传预览图"，
//     并保留路径说明，**不假装成功、不显示空白框**。
//   · 有 thumb ⇒ 显示图片 + 尺寸/大小/来源设备 + 「另存为」。
//
// 契约（同 MultiThumbDialog）：Popup 自带 open/close；`property var ctx` 收宿主上下文。
Popup {
    id: shotDlg
    property var ctx
    property var theme: ctx ? ctx.window.th : null

    // 数据：{ uid, path, bytes, width, height, thumb(base64 字符串) }
    property var payload: null

    anchors.centerIn: Overlay.overlay
    modal: true
    padding: 0
    // 预览窗按屏幕比例给，留边距，避免大图顶到屏幕边缘
    width: Math.min(ctx && ctx.window ? ctx.window.width * 0.8 : 900, 1100)
    height: Math.min(ctx && ctx.window ? ctx.window.height * 0.85 : 700, 820)

    background: Rectangle {
        color: theme ? theme.panel : "#1E1E1E"
        radius: theme ? theme.rCard : 12
        border.color: theme ? theme.card : "#3C3C3C"
        border.width: 0
    }

    readonly property bool hasThumb: !!(payload && payload.thumb && payload.thumb.length > 0)
    // data URI：QML Image 直接吃 base64，不需要先落盘
    readonly property string thumbSrc: hasThumb ? ("data:image/jpeg;base64," + payload.thumb) : ""

    function showShot(p) {
        shotDlg.payload = p || null
        shotDlg.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        // ── 标题行 ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: "截图预览"
                color: theme ? theme.fg : "#FAFAFA"
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: shotDlg.payload
                      ? (shotDlg.payload.uid + "　" + (shotDlg.payload.width || "?")
                         + "×" + (shotDlg.payload.height || "?")
                         + "　" + Math.round((shotDlg.payload.bytes || 0) / 1024) + " KB")
                      : ""
                color: theme ? theme.fg3 : "#C8C8C8"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            // 关闭
            Rectangle {
                width: 28; height: 26; radius: theme ? theme.rCard : 8
                color: closeMa.containsMouse ? (theme ? theme.hover2 : "#2A2A2A") : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: theme ? theme.fg : "#FAFAFA"
                    font.pixelSize: 14
                }
                MouseArea {
                    id: closeMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: shotDlg.close()
                }
            }
        }

        // ── 图片区 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: theme ? theme.body : "#141414"
            radius: theme ? theme.rCard : 8
            border.color: theme ? theme.card : "#3C3C3C"
            border.width: 0
            clip: true

            Image {
                id: shotImg
                anchors.fill: parent
                anchors.margins: 8
                visible: shotDlg.hasThumb
                source: shotDlg.thumbSrc
                fillMode: Image.PreserveAspectFit
                cache: false
                asynchronous: true
                // 大图解码期先不裁剪，让 user 看到"在加载"而不是空白
                onStatusChanged: {
                    if (status === Image.Error)
                        ctx && ctx.window && ctx.window.toast("预览图解码失败（回执里的 thumb 不是合法 JPEG）")
                }
            }

            // 未回传预览图：说清原因 + 给路径，别留个空白框让老师猜
            Column {
                anchors.centerIn: parent
                visible: !shotDlg.hasThumb
                spacing: 8
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: shotDlg.payload ? "该设备没有回传预览图" : "没有截图数据"
                    color: theme ? theme.fg : "#FAFAFA"
                    font.pixelSize: 14
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: shotDlg.width - 80
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: shotDlg.payload && shotDlg.payload.path
                          ? ("截图已保存在「设备」上：\n" + shotDlg.payload.path
                             + "\n（这是那台设备的本地路径，本机打不开；需升级被控端才会回传预览图）")
                          : ""
                    color: theme ? theme.fg3 : "#C8C8C8"
                    font.pixelSize: 12
                    lineHeight: 1.3
                }
            }
        }

        // ── 底部：路径 + 另存为 ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: shotDlg.payload && shotDlg.payload.path ? ("设备路径：" + shotDlg.payload.path) : ""
                color: theme ? theme.fg3 : "#C8C8C8"
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }
            // 另存为：把已收到的 base64 落到本机（有 thumb 才有意义）
            Rectangle {
                visible: shotDlg.hasThumb
                width: 76; height: 28; radius: theme ? theme.rCard : 8
                color: saveMa.containsMouse ? (theme ? theme.hover2 : "#2A2A2A") : (theme ? theme.panel : "#1E1E1E")
                border.color: theme ? theme.stroke : "#3C3C3C"
                border.width: 0
                Text {
                    anchors.centerIn: parent
                    text: "另存为…"
                    color: theme ? theme.fg : "#FAFAFA"
                    font.pixelSize: 12
                }
                MouseArea {
                    id: saveMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (ctx && ctx.window) ctx.window.saveScreenshot(shotDlg.payload.thumb)
                    }
                }
            }
        }
    }
}
