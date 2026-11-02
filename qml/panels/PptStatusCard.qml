// PPT 面板 · 状态卡（2026-10-27 · I 批）
// 从 PptDialog.qml 拆出（体积门禁：QML 组件 ≤ 200 行；拆件也符合"新组件独立文件"）。
// 只负责"把被控端回执里的状态画出来"，不做任何指令发送 —— 状态与动作分离，
// 免得以后加状态字段要动发送逻辑。
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: card

    property var theme: null
    property string appLabel: ""     // "PowerPoint" / "WPS 演示" / ""（未连上）
    property bool presenting: false
    property int curPage: 0
    property int totalPages: 0
    property string hint: ""

    radius: theme ? theme.rCard : 12
    color: theme ? theme.cream : "#2B2930"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 10

        // 应用徽标：**永远显示"实际控的是哪个"**（Office / WPS COM 互斥时最关键）
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: badge.implicitWidth + 18
            implicitHeight: 24
            radius: 12
            color: card.appLabel === "" ? "transparent"
                                        : Qt.rgba(card.theme ? card.theme.ok.r : 0.2,
                                                  card.theme ? card.theme.ok.g : 0.7,
                                                  card.theme ? card.theme.ok.b : 0.45, 0.16)
            border.width: 0
            border.color: card.appLabel === "" ? (card.theme ? card.theme.stroke : "#49454F")
                                               : (card.theme ? card.theme.ok : "#4CC38A")
            Text {
                id: badge
                anchors.centerIn: parent
                text: card.appLabel === "" ? "未连上" : card.appLabel
                color: card.appLabel === "" ? (card.theme ? card.theme.fg3 : "#CAC4D0")
                                            : (card.theme ? card.theme.ok : "#4CC38A")
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                text: {
                    if (!card.presenting) return "未在放映"
                    if (card.totalPages > 0)
                        return "第 " + card.curPage + " / " + card.totalPages + " 页"
                    return "第 " + card.curPage + " 页"
                }
                color: card.theme ? card.theme.fg : "#E6E1E5"
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: card.hint
                color: card.theme ? card.theme.fg3 : "#CAC4D0"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
    }
}
