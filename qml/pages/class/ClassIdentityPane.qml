// 班级工作台 · 左栏「身份与订阅态」（#461-#472）
//
// 回答老师的三个问题：这是哪个班、看着呢吗、连得上吗。
//   · 班名/班码 —— classService.className / classCode（云端 class-subscribed
//     回包口径；班码等宽字，方便电话里口述核对）；
//   · 订阅态 —— 三态诚实呈现：已订阅 / 订阅中（pending，本地过渡态）/
//     未订阅（含被拒后）。不把"没订"画成"已订"（班级工作台的信任底线）；
//   · 计数 —— 在线数走云端快照 onlineCount（与设备网格模型 onlineCount()
//     对账用，两者瞬时可能差一两台：事件在途，属正常，不藏）；
//   · 故障 —— lastError 非空时内联展示（ErrorContainer 色 + 人话），
//     订阅被拒是显性状态，不是静默失败。
//
// 数据真源：backend.classService（#460 typed 服务）。本面板只读 + 发一个
// 退订信号，不自己存任何班级状态（避免第二份真源）。
import QtQuick
import "../../components/md3"     // Md3Card / Md3Btn
import "../../island"             // SvgIcon

Item {
    id: pane

    property var md3: ({})
    property var cs: null       // ClassService（backend.classService）
    property bool pending: false // 已发订阅请求、还没回执（宿主过渡态）

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

    Md3Card {
        anchors.fill: parent
        variant: "elevated"
        md3: pane.md3

        Column {
            id: contentCol
            width: parent.width        // 自然高度（anchors.fill 会与 implicitHeight 成环）
            spacing: 12

            // ── 班名 + 订阅态点 ──
            Row {
                width: parent.width
                spacing: 8

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: pane.subbed ? md3.cOk
                                       : (pane.pending ? md3.cWarn : md3.cOutline)
                }
                Text {
                    text: pane.nameText !== "" ? pane.nameText : "未选择班级"
                    font.pixelSize: md3.titleM
                    font.weight: Font.Medium
                    color: md3.cOnSurface
                    elide: Text.ElideRight
                    width: parent.width - 16
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 班码（等宽，电话核对用）──
            Text {
                width: parent.width
                visible: pane.codeText !== ""
                text: "班级代码  " + pane.codeText
                font.pixelSize: md3.bodyS
                font.family: "monospace"
                color: md3.cOnSurfaceVar
                elide: Text.ElideRight
            }

            // ── 订阅态文字（三态，诚实）──
            Text {
                width: parent.width
                text: pane.subbed ? "已订阅班级动态"
                      : pane.pending ? "正在连接班级…"
                      : "未订阅：选择左侧班级开始"
                font.pixelSize: md3.body13
                color: pane.subbed ? md3.cOnSurfaceVar
                                   : (pane.pending ? md3.cWarn : md3.cOutline)
            }

            // ── 计数：在线 / 总数 ──
            Row {
                width: parent.width
                spacing: 8

                Text {
                    text: pane.onlineN
                    font.pixelSize: md3.headlineS
                    font.weight: Font.Medium
                    color: md3.cPrimary
                }
                Text {
                    text: "台在线 · 共 " + pane.deviceN + " 台"
                    font.pixelSize: md3.body13
                    color: md3.cOnSurfaceVar
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 故障内联（订阅被拒 / 断线说明）──
            Rectangle {
                width: parent.width
                visible: pane.errText !== ""
                height: errRow.implicitHeight + 20
                radius: md3.rS
                color: md3.cErrorCont

                Row {
                    id: errRow
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    SvgIcon {
                        name: "triangle-alert"
                        px: 16
                        tint: md3.cError
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: pane.errText
                        font.pixelSize: md3.body13
                        color: md3.cOnErrorCont
                        wrapMode: Text.WordWrap
                        width: parent.width - 24
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            // ── 退订（仅已订阅时可点；未订阅点了是空操作，不画）──
            Md3Btn {
                md3: pane.md3
                text: "取消订阅"
                variant: "text"
                enabled: pane.subbed
                visible: pane.subbed
                onClicked: pane.unsubscribeRequested()
            }
        }
    }
}
