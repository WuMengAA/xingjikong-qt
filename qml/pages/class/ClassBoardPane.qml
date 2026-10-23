// 班级工作台 · 中栏「班级展板」说明卡（#461-#472）
//
// 点单项⑤「展板段先说明卡」：中栏 SegmentedButton 的「展板」段当前**不装假画面**，
// 给一张说明卡讲清楚它是什么、现在到哪一步 —— 真源原则：没做的事就说没做，
// 不用占位图冒充已上线（用户点单「不要塑料感」的延伸：不糊弄）。
//
// 展板定位（蓝图口径）：把本班设备画面与班级动态并排常亮，供教室大屏/一体机
// 公开展示。与工作台的区别：工作台是老师操作视图，展板是旁观视图（无操作）。
// 落地顺序：设备网格（已上线）→ 事件流（已上线）→ 展板常亮模式（规划中）。
//
// 本卡纯展示，零信号零动作 —— 没有可交互的东西就不发信号（宁缺毋滥）。
import QtQuick
import "../../components/md3"     // Md3Card
import "../../island"             // SvgIcon

Item {
    id: pane

    property var md3: ({})

    implicitWidth: 480
    implicitHeight: card.implicitHeight

    Md3Card {
        id: card
        anchors.fill: parent
        variant: "elevated"
        md3: pane.md3

        Column {
            anchors.fill: parent
            spacing: 16

            Row {
                width: parent.width
                spacing: 12

                Rectangle {
                    width: 44
                    height: 44
                    radius: md3.rM
                    color: md3.cSecondaryCont
                    anchors.verticalCenter: parent.verticalCenter

                    SvgIcon {
                        name: "tv"
                        px: 22
                        tint: md3.cOnSecondaryCont
                        anchors.centerIn: parent
                    }
                }

                Column {
                    spacing: 2
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: "班级展板"
                        font.pixelSize: md3.titleM
                        font.weight: Font.Medium
                        color: md3.cOnSurface
                    }
                    Text {
                        text: "教室大屏常亮视图 · 规划中"
                        font.pixelSize: md3.body13
                        color: md3.cOnSurfaceVar
                    }
                }
            }

            Text {
                width: parent.width
                text: "展板把本班设备画面与班级动态并排常亮，适合投在教室一体机大屏上，"
                      + "供全班观看。它与工作台的区别是：工作台由老师操作，展板只展示、不操作。"
                font.pixelSize: md3.body13
                color: md3.cOnSurfaceVar
                wrapMode: Text.WordWrap
                lineHeight: 1.4
            }

            // 规划能力清单（没上线的就明说，不画开关不画按钮）
            Column {
                width: parent.width
                spacing: 10

                Text {
                    text: "规划中的能力"
                    font.pixelSize: md3.bodyS
                    font.weight: Font.Medium
                    color: md3.cOnSurfaceVar
                }

                Repeater {
                    model: [
                        { icon: "layout-grid", label: "设备画面自动轮播", note: "已上线：工作台设备网格" },
                        { icon: "clock",      label: "班级事件滚动展示", note: "已上线：工作台时间线" },
                        { icon: "eye",        label: "大屏常亮与全屏",   note: "规划中，随下个版本" }
                    ]

                    delegate: Row {
                        required property var modelData
                        width: parent.width
                        spacing: 10

                        SvgIcon {
                            name: modelData.icon
                            px: 16
                            tint: md3.cOutline
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: modelData.label
                            font.pixelSize: md3.body13
                            color: md3.cOnSurface
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "· " + modelData.note
                            font.pixelSize: md3.bodyS
                            color: md3.cOutline
                            elide: Text.ElideRight
                            width: parent.width - 120
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
    }
}
