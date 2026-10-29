// 班级设备面板（右侧抽屉）· QtShadcn 规范（2026-10-29 拆分 + 迁移）
//
// ⚠️ B0 铁律：**纯声明式、零逻辑**
//   · 面包屑/设备名/缩略图源/设备清单/编辑态全部来自 C++ `classCenter`；
//   · 无 Math.*、无三元、无 JS function、无 console.log、无通信；
//   · hover 与选中态一律用 `State.when` 表达。
//
// ── 2026-10-29 拆分说明 ────────────────────────────────────────────────
// 原单文件 438 行超红线 ⇒ 拆成三件：
//   · 本件（容器）= 底板 + 标题行 + 面包屑行 + 设备 chip + 拼装
//   · ClassDeviceDrawerEditPanel = 编辑区（班级改绑 + 只读说明）
//   · ClassDeviceDrawerThumbCard = 缩略图卡片（进远程桌面）
//
// ── 关于「编辑」的真实边界（不粉饰，原样保留）──────────────────────────
//   云端 knownDevices 只有 classCode / role / name 三个维度，**没有学校**，
//   也**不接受**设备改名 ⇒ 只有「班级改绑」是真能落库的动作
//   （POST /api/class/bind，走既有 ClassAdmin::bindDevice）。
//   学校与设备名按只读展示，并在面板里写明原因 —— 不摆一个点了没反应的下拉。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · **删全部 border**（标题分隔线 / 编辑按钮描边 / 设备 chip 描边）—— 规范铁律；
//   设备 chip 的选中态原本靠 border.color 变绿，改为**实底主色/成功色区分**
// · `Md3Btn` → `AppButton`；`Md3.*` → `Theme.*`；字号走 12/13/14/16/20
// · 左侧 32 圆角的"溢出裁切"技法是纯 QML 几何，保留（不是边框也不是绝对定位滥用）
import QtQuick
import QtQuick.Layouts
import "../../components"           // AppButton
import "../../components/theme"    // Theme 令牌

Item {
    id: drawer

    // 关闭与进入远程桌面：一律调 C++，QML 只转发意图、不持有状态
    signal closeDrawer()
    signal enterRemote(string dev)

    // 底板：只有**左侧**两角圆、右侧贴屏幕边保持直角。
    // QML 的 Rectangle 四角共用同一 radius ⇒ 用「矩形向右溢出 + 父级 clip」实现。
    Item {
        anchors.fill: parent
        clip: true
        Rectangle {
            anchors.fill: parent
            anchors.rightMargin: -40      // 右侧溢出 40 > 圆角 32 ⇒ 裁出来就是干净直角
            radius: Theme.radius.xl + 12  // 32
            color: Theme.color.surface
        }
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
                anchors.leftMargin: Theme.spacing.lg
                anchors.verticalCenter: parent.verticalCenter
                text: "班级设备面板"
                color: Theme.color.textPrimary
                font.pixelSize: 20
                font.bold: true
            }
            AppButton {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing.md
                anchors.verticalCenter: parent.verticalCenter
                text: "✕"
                variant: "ghost"
                size: "sm"
                onClicked: drawer.closeDrawer()
            }
            // 原为 1px 分隔线；规范要求分区只靠底色差 ⇒ 移除
        }

        Flickable {
            width: parent.width
            height: parent.height - 64
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: body
                width: parent.width
                spacing: Theme.spacing.lg
                anchors.margins: Theme.spacing.lg

                // ── 第二行：面包屑 + 设备名 + 编辑 ──────────────
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.sm

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.xs
                        // 面包屑由 C++ 拼（学校 > 届 > 班级），QML 不做字符串拼接
                        Text {
                            Layout.fillWidth: true
                            text: classCenter.breadcrumbText
                            color: Theme.color.textSecondary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: classCenter.deviceName
                            color: Theme.color.textPrimary
                            font.pixelSize: 16
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    AppButton {
                        id: editBtn
                        text: "编辑"
                        variant: "secondary"
                        size: "sm"
                        onClicked: classCenter.requestOp("class_edit")

                        // 果冻曲线 + hover/press 用 State 表达（B0：不写三元）
                        scale: 1.0
                        Behavior on scale {
                            NumberAnimation { duration: Theme.motion.normal; easing.type: Easing.OutBack }
                        }
                        states: [
                            State {
                                name: "pressed"
                                when: editBtn.pressed
                                PropertyChanges { target: editBtn; scale: Theme.motion.pressScale }
                            },
                            State {
                                name: "hovered"
                                when: editBtn.hovered && !editBtn.pressed
                                PropertyChanges { target: editBtn; scale: Theme.motion.hoverScale }
                            }
                        ]
                    }
                }

                // ── 编辑区（内容与开合态都在 C++）────────────────────
                // 展开/收起补过渡：外层 Item 动 height + opacity 并 clip 溢出，
                // 不写三元 —— 开合由 State.when 判。
                Item {
                    id: editWrap
                    Layout.fillWidth: true
                    implicitHeight: 0
                    height: implicitHeight
                    clip: true
                    opacity: 0
                    Behavior on height {
                        NumberAnimation { duration: Theme.motion.slow; easing.type: Easing.OutCubic }
                    }
                    Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
                    states: State {
                        name: "open"
                        when: classCenter.editing
                        PropertyChanges {
                            target: editWrap
                            implicitHeight: editPanel.contentHeight + Theme.spacing.md + Theme.spacing.sm
                            opacity: 1
                        }
                    }

                    ClassDeviceDrawerEditPanel {
                        id: editPanel
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                    }
                }

                // ── 设备切换（一个班多台时；选中态由 C++ 给）────────
                Text {
                    Layout.fillWidth: true
                    visible: classCenter.devices.length > 1
                    text: "本班设备"
                    color: Theme.color.textSecondary
                    font.pixelSize: 12
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.sm
                    visible: classCenter.devices.length > 1

                    Repeater {
                        model: classCenter.devices
                        delegate: AppButton {
                            id: chip
                            required property var modelData
                            text: modelData.name
                            // 选中态改用**实底**表达（规范禁 border）：默认 secondary，
                            // 选中由 State.when 切 primary —— B0 铁律禁三元，故不用条件表达式。
                            variant: "secondary"
                            size: "sm"
                            states: State {
                                name: "picked"
                                when: chip.modelData.selected
                                PropertyChanges { target: chip; variant: "primary" }
                            }
                            onClicked: classCenter.selectDevice(modelData.uid)
                        }
                    }
                }

                // ── 第三行：缩略图卡片 ──────────────────────────
                ClassDeviceDrawerThumbCard {
                    Layout.fillWidth: true
                    onActivated: drawer.enterRemote("")
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: "—— 功能完善中 ——"
                    color: Theme.color.textSecondary
                    font.pixelSize: 12
                }
            }
        }
    }
}
