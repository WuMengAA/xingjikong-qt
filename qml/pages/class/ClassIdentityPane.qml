// 班级工作台 · 左栏「身份与订阅态」（#461-#472，2026-10-24 清新极简改版）
//
// 回答老师的三个问题：这是哪个班、看着呢吗、连得上吗。
//   · 班名/班码 —— classService.className / classCode；
//   · 订阅态 —— FreshPill 三态（已订阅/连接中/未订阅），诚实不伪装；
//   · 计数 —— 在线数走云端快照 onlineCount；
//   · 故障 —— lastError 非空时内联（frDanger 软底 + 人话）。
// 数据真源：backend.classService（#460）。本面板只读 + 一个退订信号。
import QtQuick
import "../../components/fresh"     // FreshCard / FreshPill
import "../../island"              // SvgIcon

Item {
    id: pane

    property var md3: ({})
    property var cs: null        // ClassService（backend.classService）
    property bool pending: false  // 已发订阅请求、还没回执（宿主过渡态）

    signal unsubscribeRequested()

    implicitWidth: 260
    implicitHeight: contentCol.implicitHeight + 32

    readonly property bool subbed: cs && cs.subscribed === true
    readonly property string codeText: (cs && cs.classCode) ? cs.classCode : ""
    readonly property string nameText: (cs && cs.className) ? cs.className : ""
    // ⚠️ 命名纪律：属性名不得 on + 大写开头（会撞 QML 信号处理器语法，整页崩）
    readonly property int onlineN: (cs && cs.onlineCount) ? cs.onlineCount : 0
    readonly property int deviceN: (cs && cs.deviceCount) ? cs.deviceCount : 0
    readonly property string errText: (cs && cs.lastError) ? cs.lastError : ""

    FreshCard {
        anchors.fill: parent
        md3: pane.md3

        Column {
            id: contentCol
            width: parent.width        // 自然高度（anchors.fill 会与 implicitHeight 成环）
            spacing: 14

            // ── 班名 + 订阅态 pill ──
            Row {
                width: parent.width
                spacing: 8

                Text {
                    text: pane.nameText !== "" ? pane.nameText : "未选择班级"
                    font.pixelSize: md3.titleM
                    font.weight: Font.Medium
                    color: md3.frInk || "#111827"
                    elide: Text.ElideRight
                    width: parent.width - 80
                    anchors.verticalCenter: parent.verticalCenter
                }
                FreshPill {
                    anchors.verticalCenter: parent.verticalCenter
                    md3: pane.md3
                    text: pane.subbed ? "已订阅" : (pane.pending ? "连接中" : "未订阅")
                    tone: pane.subbed ? "ok" : (pane.pending ? "wait" : "off")
                }
            }

            // ── 班码（等宽，电话核对用）──
            Text {
                width: parent.width
                visible: pane.codeText !== ""
                text: "班级代码  " + pane.codeText
                font.pixelSize: md3.bodyS
                font.family: "monospace"
                color: md3.frInk2 || "#6B7280"
                elide: Text.ElideRight
            }

            // ── 计数：在线 / 总数 ──
            Row {
                width: parent.width
                spacing: 8

                Text {
                    text: pane.onlineN
                    font.pixelSize: md3.headlineS
                    font.weight: Font.Medium
                    color: md3.frGreen || "#22C55E"
                }
                Text {
                    text: "台在线 · 共 " + pane.deviceN + " 台"
                    font.pixelSize: md3.body13
                    color: md3.frInk2 || "#6B7280"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 故障内联（订阅被拒 / 断线说明）──
            Rectangle {
                width: parent.width
                visible: pane.errText !== ""
                height: errRow.implicitHeight + 20
                radius: md3.frRS || 12
                color: md3.frDangerCont || "#FEE2E2"

                Row {
                    id: errRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    SvgIcon {
                        name: "triangle-alert"
                        px: 16
                        tint: md3.frDanger || "#EF4444"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: pane.errText
                        font.pixelSize: md3.body13
                        color: md3.frOnDangerCont || "#7F1D1D"
                        wrapMode: Text.WordWrap
                        width: parent.width - 24
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            // ── 退订（仅已订阅可点；未订阅不画）──
            FreshBtn {
                md3: pane.md3
                text: "取消订阅"
                variant: "ghost"
                enabled: pane.subbed
                visible: pane.subbed
                onClicked: pane.unsubscribeRequested()
            }
        }
    }
}
