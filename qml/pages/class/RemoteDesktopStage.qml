// 远程桌面舞台（模块一 · 3）—— 纯声明式，零逻辑
//
// 2026-10-28 B0/B1：4:3 与 9:16 的比例、缩放系数**全部由 C++ 算好**
// （classCenter.stageLeftW / stageRightW / stageScale / stageFrameH），本件只绑定、不算 Math。
// 舞台自身尺寸通过 Binding 回写给 C++ 作为输入（声明式，不写 JS）。
//
// 蓝图：左侧画面区 4:3 容器，内部居中渲染 16:9 真实桌面画面（上下留黑边）；右侧操作面板区 9:16。
//
// 真实画面（B1 接线）：进远程桌面时控制器发 remoteEntered → 宿主 backend.setCurrentUid
// （既有「选中即接管」，回执后再拉 VNC），帧到了宿主调 bumpLiveFrame ⇒ liveSource 变
// （tick 由 C++ 递增，QML 不写计数器）。未接通时显示 liveStateText，不假装有画面。
// 指针：只报局部坐标，归一化与夹取在 C++（stagePointer）。
import QtQuick 2.15
import "../../components/theme"       // Md3 令牌
import "../../island"                 // SvgIcon

Item {
    id: stage

    // 把舞台尺寸回写给 C++ 作为比例计算的输入（声明式 Binding，非 JS）
    Binding { target: classCenter; property: "stageViewW"; value: stage.width }
    Binding { target: classCenter; property: "stageViewH"; value: stage.height }

    Row {
        anchors.centerIn: parent
        spacing: 24

        // ── 左：4:3 容器 ──────────────────────────────────────────
        Rectangle {
            width: classCenter.stageLeftW
            height: classCenter.stageBoxH
            radius: 16
            color: "#0F172A"
            border.width: 1
            border.color: Theme.color.textSecondary

            // 容器内 16:9：宽满容器，垂直居中 ⇒ 上下自然留黑边
            Rectangle {
                id: frame
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: classCenter.stageFrameH
                color: "#1E293B"
                clip: true

                // 真实桌面画面（VNC）：源与"有没有画面"都由 C++ 给
                Image {
                    anchors.fill: parent
                    source: classCenter.liveSource
                    visible: classCenter.liveActive
                    fillMode: Image.PreserveAspectFit
                    cache: false
                }

                // 未接通/等第一帧：居中说明（文案由 C++ 给，QML 不写三层判断）
                Column {
                    anchors.centerIn: parent
                    spacing: 10
                    visible: !classCenter.liveActive
                    SvgIcon {
                        name: "monitor"
                        px: 40
                        tint: "#64748B"
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: classCenter.liveStateText
                        color: "#94A3B8"
                        font.pixelSize: 13
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        // 用户向文案（原来的「4:3 容器内居中 16:9 · 上下留黑边」是
                        // 开发规格说明，不该给学生/老师看到，2026-10-28 验收修正）
                        text: "等待设备回传画面"
                        color: "#64748B"
                        font.pixelSize: 12
                    }
                }

                // 左键：移动 / 按下 / 抬起（只报坐标，换算在 C++）
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    onPositionChanged: classCenter.stagePointer("move", mouse.x, mouse.y)
                    onPressed: classCenter.stagePointer("down", mouse.x, mouse.y)
                    onReleased: classCenter.stagePointer("up", mouse.x, mouse.y)
                }
                // 右键：单独一个区域（避免 QML 里写 mouse.button 的三元）
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onPressed: classCenter.stagePointer("down", mouse.x, mouse.y, "right")
                    onReleased: classCenter.stagePointer("up", mouse.x, mouse.y, "right")
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    text: classCenter.deviceName
                    color: "#E2E8F0"
                    font.pixelSize: 12
                }
            }

            // 返回
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 12
                width: 32
                height: 32
                radius: 16
                color: Qt.rgba(255, 255, 255, 0.10)
                SvgIcon {
                    anchors.centerIn: parent
                    name: "arrow-left"
                    px: 16
                    tint: "#E2E8F0"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: classCenter.backToGrid()
                }
            }
        }

        // ── 右：9:16 操作面板 ─────────────────────────────────────
        Rectangle {
            width: classCenter.stageRightW
            height: classCenter.stageBoxH
            radius: 16
            color: Theme.color.surface
            border.width: 1
            border.color: Theme.color.textSecondary

            RemoteOpPanel {
                anchors.fill: parent
                anchors.margins: 16
            }
        }
    }
}
