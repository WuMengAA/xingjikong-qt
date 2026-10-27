// 班级设备面板（右侧抽屉 Drawer）—— 纯声明式，零逻辑
//
// 2026-10-27 需求 · 2026-10-28 B1 接真数据。用户蓝图定稿：
//   · 第一行：固定标题字样「班级设备面板」（右侧关闭）。
//   · 第二行：学校 > 班级 > 设备名；最右侧 [编辑]。
//       点编辑：学校、班级、设备名（限 20 字）。
//   · 第三行（入口）：带**缩略图**的卡片（约 160×90），点击进入「远程桌面」。
//
// ⚠️ 本件不含任何逻辑（B0 铁律）：
//   · 面包屑/设备名/缩略图源/设备清单/编辑态全部来自 C++ `classCenter`；
//   · 无 Math.*、无三元（hover 与选中态用 State 表达）、无 JS function、无 console.log、无通信。
//
// ⚠️ 关于「编辑」的真实边界（不粉饰）：
//   云端 knownDevices 只有 classCode / role / name 三个维度，**没有学校**，也**不接受**设备改名
//   ⇒ 只有「班级改绑」是真能落库的动作（POST /api/class/bind，走既有 ClassAdmin::bindDevice）。
//   学校与设备名按只读展示，并在面板里写明原因 —— 不摆一个点了没反应的下拉。
import QtQuick 2.15
import "../../components/theme"       // Md3 令牌
import "../../components/md3" as M    // Md3Btn（班级改绑用的胶囊按钮）
import "../../island"                 // SvgIcon

Item {
    id: drawer

    // 关闭与进入远程桌面：一律调 C++，QML 只转发意图、不持有状态
    signal closeDrawer()
    signal enterRemote(string dev)

    // 滑入动效由宿主的 Behavior on x 承担（原型 400ms cubic-bezier(.2,.8,.2,1) ⇒ OutCubic）

    Rectangle {
        anchors.fill: parent
        color: Md3.cSurface
        radius: 0
    }

    // 左侧圆角（原型 rounded-l-[32px]）
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 32
        color: Md3.cSurface
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ── 第一行：标题 + 关闭 ──────────────────────────────────
        Item {
            width: parent.width
            height: 64
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 24
                anchors.verticalCenter: parent.verticalCenter
                text: "班级设备面板"
                color: Md3.cOnSurface
                font.pixelSize: 20
                font.bold: true
            }
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                width: 36
                height: 36
                radius: 18
                color: "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    color: Md3.cOnSurfaceVar
                    font.pixelSize: 18
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: drawer.closeDrawer()
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Md3.cOutlineVar
                opacity: 0.6
            }
        }

        Flickable {
            width: parent.width
            height: parent.height - 64
            contentHeight: body.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: body
                width: parent.width
                spacing: 20
                topPadding: 24
                leftPadding: 24
                rightPadding: 24
                bottomPadding: 24

                // ── 第二行：面包屑 + 设备名 + 编辑 ──────────────
                Item {
                    width: body.width - 48
                    height: 56
                    Column {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                        // 面包屑由 C++ 拼（学校 > 届 > 班级），QML 不做字符串拼接
                        Text {
                            text: classCenter.breadcrumbText
                            color: Md3.cOnSurfaceVar
                            font.pixelSize: 12
                        }
                        Text {
                            text: classCenter.deviceName
                            color: Md3.cOnSurface
                            font.pixelSize: 16
                            font.bold: true
                        }
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 64
                        height: 32
                        radius: 16
                        color: Md3.cSurface
                        border.width: 1
                        border.color: Md3.cOutlineVar
                        Text {
                            anchors.centerIn: parent
                            text: "编辑"
                            color: Md3.cOnSurface
                            font.pixelSize: 13
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: classCenter.requestOp("class_edit")
                        }
                    }
                }

                // ── 编辑区（visible / 内容全部来自 C++）────────────
                Rectangle {
                    visible: classCenter.editing
                    width: body.width - 48
                    height: editCol.height + 24
                    radius: 16
                    color: Md3.cSurface
                    border.width: 1
                    border.color: Md3.cOutlineVar

                    Column {
                        id: editCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 10

                        Text {
                            width: editCol.width
                            text: "所属班级（点一下即改绑，云端立即生效）"
                            color: Md3.cOutline
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        // 班级选项与选中态样式都由 C++ 给（variant），QML 不写三元
                        Flow {
                            width: editCol.width
                            spacing: 8
                            Repeater {
                                model: classCenter.classOptions
                                M.Md3Btn {
                                    text: modelData.name
                                    variant: modelData.variant
                                    onClicked: classCenter.rebindSelectedTo(modelData.code)
                                }
                            }
                        }
                        Rectangle {
                            width: editCol.width
                            height: 1
                            color: Md3.cOutlineVar
                            opacity: 0.6
                        }
                        Text {
                            width: editCol.width
                            text: "学校：本校（云端目前只有班级维度，没有学校表）"
                            color: Md3.cOnSurfaceVar
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            width: editCol.width
                            text: "设备名：" + classCenter.deviceName +
                                  "（设备名由被控端自报，云端不开改名接口；改名在设备端做）"
                            color: Md3.cOnSurfaceVar
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // ── 设备切换（一个班多台时；选中态由 C++ 给）────────
                Text {
                    visible: classCenter.devices.length > 1
                    text: "本班设备"
                    color: Md3.cOutline
                    font.pixelSize: 12
                }
                Flow {
                    width: body.width - 48
                    spacing: 8
                    visible: classCenter.devices.length > 1
                    Repeater {
                        model: classCenter.devices
                        Rectangle {
                            id: chip
                            width: chipLabel.implicitWidth + 36
                            height: 30
                            radius: 15
                            color: Md3.cSurface
                            border.width: 1
                            border.color: Md3.cOutline

                            // 选中态：State 表达（QML 不写三元）
                            states: State {
                                name: "picked"
                                when: modelData.selected
                                PropertyChanges { target: chip; color: "#DCFCE7"; border.color: "#22C55E" }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8
                                height: 8
                                radius: 4
                                color: modelData.dot
                                opacity: modelData.dotOpacity
                            }
                            Text {
                                id: chipLabel
                                anchors.left: parent.left
                                anchors.leftMargin: 26
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name
                                color: Md3.cOnSurface
                                font.pixelSize: 12
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: classCenter.selectDevice(modelData.uid)
                            }
                        }
                    }
                }

                // ── 第三行：缩略图卡片（160×90，点击进入远程桌面）──
                Item {
                    width: body.width - 48
                    height: 90

                    Rectangle {
                        id: thumbCard
                        width: 160
                        height: 90
                        radius: 12
                        color: "#0F172A"
                        border.width: 1
                        border.color: Md3.cOutline
                        clip: true

                        // 真图：image://classthumb/<uid>?t<rev>（源由 C++ 拼；无图时为空串）
                        Image {
                            anchors.fill: parent
                            source: classCenter.thumbSource
                            visible: classCenter.thumbAvailable
                            fillMode: Image.PreserveAspectCrop
                            cache: false
                        }
                        // 空态：说清"还没有画面"，不是黑框
                        Column {
                            anchors.centerIn: parent
                            spacing: 4
                            visible: !classCenter.thumbAvailable
                            SvgIcon {
                                name: "monitor"
                                px: 24
                                tint: "#64748B"
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "暂无画面"
                                color: "#94A3B8"
                                font.pixelSize: 11
                            }
                        }

                        scale: 1.0
                        Behavior on scale {
                            NumberAnimation { duration: 400; easing.type: Easing.OutBack }
                        }
                        states: [
                            State {
                                name: "pressed"
                                when: thumbMa.pressed
                                PropertyChanges { target: thumbCard; scale: 0.96 }
                            },
                            State {
                                name: "hovered"
                                when: thumbMa.containsMouse && !thumbMa.pressed
                                PropertyChanges { target: thumbCard; scale: 1.03; border.color: "#22C55E" }
                            }
                        ]
                        MouseArea {
                            id: thumbMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: drawer.enterRemote("")
                        }
                    }

                    Column {
                        anchors.left: thumbCard.right
                        anchors.leftMargin: 16
                        anchors.verticalCenter: thumbCard.verticalCenter
                        spacing: 4
                        Text {
                            text: "进入远程桌面"
                            color: Md3.cOnSurface
                            font.pixelSize: 14
                            font.bold: true
                        }
                        Text {
                            text: "缩略图 160×90 · 16:9"
                            color: Md3.cOutline
                            font.pixelSize: 12
                        }
                        Text {
                            text: "只读画面；键鼠注入在设备集控页"
                            color: Md3.cOutline
                            font.pixelSize: 11
                        }
                    }
                }

                Text {
                    width: body.width - 48
                    horizontalAlignment: Text.AlignHCenter
                    text: "—— 功能完善中 ——"
                    color: Md3.cOutline
                    font.pixelSize: 12
                }
            }
        }
    }
}
