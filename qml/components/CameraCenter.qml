// 星集控 · 监控中心（2026-10-19 · MD3 重构批 / 2026-10-29 拆分）
//
// 本次按 MD3 规范重构（用户点单），彻底消除旧版"拼凑感"。规格对照：
//   ① 净化边框：卡片/区域一律无 border 无阴影，用**背景色阶**区分层级；
//      仅保留 1px 分隔线（cOutlineVar）作小节分隔。
//   ② 统一圆角：按钮/卡片/分段/徽标一律 rM（12px）。
//   ③ Tab：StTabBar 标准下划线 Tab（主色 3px 指示器）。
//   ④ 精简文字：黑屏占位只剩「连接中...」/「暂无画面」并居中；长说明移到「诊断」页与 Tooltip。
//   ⑤ 分层：顶栏只有标题 + 右侧无边框图标按钮（测试/抓拍/关闭）。
//
// 2026-10-29 拆分：状态与逻辑下沉到 CameraController.qml；四档 Tab 页面抽成独立子组件
// （CameraCenterLive / Record / Study / Diag），状态行与底栏保留在本文件。视觉块全部通过
// `cc` 引用控制器；本文件只做编排，不含业务函数（行为零差异）。
//
// 数据源：backend.devices → backend.sendAction() → backend.recordings → backend.subscribeThumbnails()
// 绑定存储：QtCore Settings（cameraBind/<uid> = 设备名，与 CameraBindDialog 同 key）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "../island"   // SvgIcon 唯一真源在共享岛目录（构建期拷到 qml/island/）
import "md3"         // MD3 组件库（构建期由 Stelarith-shared-qml/ui/md3/ 拷入 components/md3/）
import "./theme"     // Theme 设计令牌单例（IconBtn 收口走 Theme，弃 Md3 th 令牌）

Item {
    // ⚠️ 见 CameraController：这些属性由宿主（CameraCenterDialog）注入，转发给内部控制器。
    required property var backend
    property var theme: ({})
    property var panelCtx: null

    // 控制器：持有全部状态属性 + 逻辑函数；视觉块统一通过 `cc` 引用它。
    CameraController {
        id: ctrl
        backend: backend
        theme: theme
        panelCtx: panelCtx
    }

    // ── 无边框图标按钮（IconButton）：顶栏/刷新用，状态层 8%/10%，无底无边 ──
    component IconBtn: Rectangle {
        id: iconBtn
        property string icon: ""
        property string tip: ""
        signal clicked()
        width: 40
        height: 40
        radius: Theme.radius.lg
        color: "transparent"
        opacity: iconBtn.enabled ? 1 : 0.38
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.color.primary
            opacity: ma.pressed ? 0.10 : (ma.containsMouse ? 0.08 : 0)
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        SvgIcon {
            anchors.centerIn: parent
            name: iconBtn.icon
            px: 20
            tint: Theme.color.textPrimary
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            enabled: iconBtn.enabled
            cursorShape: iconBtn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (iconBtn.enabled) iconBtn.clicked()
        }
        ToolTip {
            visible: ma.containsMouse && iconBtn.tip !== ""
            delay: 450
            text: iconBtn.tip
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: ctrl.tSpM

        // ① 顶栏：标题吃剩余宽度，右侧无边框图标按钮（测试/抓拍/关闭）
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Text {
                id: headTitle
                anchors.left: parent.left
                anchors.right: headBtns.left
                anchors.rightMargin: ctrl.tSpS
                anchors.verticalCenter: parent.verticalCenter
                text: "监控中心" + (ctrl.selUid !== "" ? " · " + ctrl.deviceName(ctrl.selUid) : "")
                color: ctrl.tFg
                font.pixelSize: ctrl.tTitleM
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Row {
                id: headBtns
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: ctrl.tSpXS
                IconBtn {
                    icon: "circle-check"
                    tip: "摄像头自检：发一条测试指令，看能不能出帧"
                    enabled: ctrl.selUid !== ""
                    onClicked: ctrl.testCam()
                }
                IconBtn {
                    icon: "video"
                    tip: "抓拍一帧（camera_snapshot）"
                    enabled: ctrl.selUid !== ""
                    onClicked: ctrl.snapshot()
                }
                IconBtn {
                    icon: "x"
                    tip: "关闭监控中心"
                    onClicked: ctrl.closePanel()
                }
            }
        }

        // ② Tab：标准下划线 Tab（实时 / 录制回放 / 自习 / 诊断）
        StTabBar {
            id: tabs
            theme: ctrl.theme
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            model: ["实时", "录制回放", "自习", "诊断"]
            currentIndex: 0
        }

        // ③ 页面容器：四档互斥显示
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            CameraCenterLive   { id: liveInst;   anchors.fill: parent; cc: ctrl; visible: tabs.currentIndex === 0 }
            CameraCenterRecord { anchors.fill: parent; cc: ctrl; visible: tabs.currentIndex === 1 }
            CameraCenterStudy  { anchors.fill: parent; cc: ctrl; visible: tabs.currentIndex === 2 }
            CameraCenterDiag   { anchors.fill: parent; cc: ctrl; visible: tabs.currentIndex === 3 }
        }

        // ④ 状态行（人话结论；可恢复的错误旁边给「重试」）
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            Text {
                id: statusLine
                anchors.left: parent.left
                anchors.right: retryTxt.visible ? retryTxt.left : parent.right
                anchors.rightMargin: retryTxt.visible ? ctrl.tSpS : 0
                anchors.verticalCenter: parent.verticalCenter
                text: ctrl.status
                color: ctrl.tFg4
                font.pixelSize: ctrl.tLabelS
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                id: retryTxt
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: ctrl.canRetry
                text: "重试"
                color: ctrl.tAcc
                font.pixelSize: ctrl.tLabelS
                font.weight: Font.Medium
                font.underline: retryMa.containsMouse
                MouseArea {
                    id: retryMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: ctrl.retryLast()
                }
            }
        }

        // ⑤ 底栏：左＝截图/推文件，右＝停止/返回（MD3 按钮，统一 12 圆角）
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: ctrl.tSpS
                Md3Btn {
                    radius: ctrl.tRM
                    variant: "tonal"
                    text: "截图"
                    enabled: ctrl.selUid !== ""
                    tip: "对被控端屏幕截图（screenshot）"
                    onClicked: ctrl.screenShot()
                }
                Md3Btn {
                    radius: ctrl.tRM
                    variant: "filled"
                    text: "文件分发"
                    enabled: ctrl.selUid !== ""
                    tip: "将本机文件分发到该设备"
                    onClicked: ctrl.pushFile()
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: ctrl.tSpS
                Md3Btn {
                    radius: ctrl.tRM
                    variant: "outlined"
                    text: "停止"
                    enabled: ctrl.recording || ctrl.camStreamUid !== ""
                    tip: "一键收口：停录 + 停摄像头推流"
                    onClicked: ctrl.stopAll()
                }
                Md3Btn {
                    radius: ctrl.tRM
                    variant: "text"
                    text: "返回"
                    tip: "关闭监控中心"
                    onClicked: ctrl.closePanel()
                }
            }
        }
    }
}
