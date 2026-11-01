import QtQuick 2.15
import "."

// 灵动岛 · 「设」面（2026-10-27 · C 批）
//
// ── 为什么这一面必须在岛上（用户 07:36 裁决）────────────────────────
//   「这些功能大多是加入到被控端，而且大部分和灵动岛有关」。
//   系统音量 / 通知音量是老师上课当场要调的，管理端不在手边，岛才是他的手。
//
// ── 数据从哪来 ──────────────────────────────────────────────────
//   全部来自 IslandBridge 的属性（systemVolume / notifyVolume），
//   按钮走 bridge.settingsAction(cmd)。本面**不存权威状态**：真值在 C++ 域 / QSettings，
//   QML 只做显示 + 拖动时即时跟手 + 转发。
//   2026-11-01 用户裁决：通知黑名单不是设计需求（通知按「提供者」建模，见 ClassIsland
//   通知模型，将另行重做）⇒ 黑名单 UI 移除；C++ 侧 notify/blacklist 过滤仍保留（在
//   提供者模型落地前，它仍是入站通知的真实过滤器，不在本批删除）。
//
// ── 为什么滑块只推不拉 ──────────────────────────────────────────
//   滑块 value 不绑回 bridge 属性：系统音量 TTS 前后会被抬到 75%/恢复到 50%，若把
//   滑块绑成桥属性，下一次 changed 就会把滑块拽回真实值，和老师手指打架。所以只在一
//   次性打开时从桥取初值，之后以本地拖动为准；下次打开再重新取当前设备真实值。
//
// ── 布局（重要）──────────────────────────────────────────────────
//   行用「锚」布局（label 锚左 / value 锚右 / 滑块锚中间剩余空间），**不**用 Row 子项
//   写宽度——Row 自身无显式宽度时，子项写 `parent.width - N` 会陷入循环绑定，结果滑
//   块宽度算错、贴边被 IslandSurface 圆角裁剪（实测两次）。锚布局无此坑。
Item {
    id: face

    property var th: null
    property var bridge: null

    // 一次性初值（打开面时从桥拉一次；之后本地跟手，不再回写）
    // 2026-10-30：守卫补「属性未就绪」分支 —— bridge 为真值但 systemVolume/notifyVolume
    // 仍是 undefined 时，原式会把 undefined 赋给 int（日志实锤 Unable to assign [undefined] to int）。
    property int sysVol:    (face.bridge && face.bridge.systemVolume !== undefined) ? face.bridge.systemVolume : -1
    property int notifyVol: (face.bridge && face.bridge.notifyVolume !== undefined) ? face.bridge.notifyVolume : 75

    function act(cmd) {
        if (face.bridge && face.bridge.settingsAction) face.bridge.settingsAction(cmd)
    }

    Column {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 10

        // ── 系统音量 ───────────────────────────────────────────
        Item {
            width: parent.width
            height: 28
            Text {
                id: sysLbl
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "系统音量"
                color: face.th ? face.th.fg : "#FFFFFF"
                font.pixelSize: 12
                width: 56
            }
            Text {
                id: sysVal
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: (face.sysVol < 0 ? "—" : face.sysVol + "%")
                color: face.th ? face.th.sub : "#9A9A9A"
                font.pixelSize: 12
                width: 40
                horizontalAlignment: Text.AlignRight
            }
            IslandVolSlider {
                anchors.left: sysLbl.right
                anchors.leftMargin: 8
                anchors.right: sysVal.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                height: 28
                th: face.th
                Component.onCompleted: value = (face.sysVol < 0 ? 0 : face.sysVol)
                onValueChanged: face.act("sysvol:" + value)
            }
        }

        // ── 通知音量 ───────────────────────────────────────────
        Item {
            width: parent.width
            height: 28
            Text {
                id: nfyLbl
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "通知音量"
                color: face.th ? face.th.fg : "#FFFFFF"
                font.pixelSize: 12
                width: 56
            }
            Text {
                id: nfyVal
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: (face.notifyVol < 0 ? "—" : face.notifyVol + "%")
                color: face.th ? face.th.sub : "#9A9A9A"
                font.pixelSize: 12
                width: 40
                horizontalAlignment: Text.AlignRight
            }
            IslandVolSlider {
                anchors.left: nfyLbl.right
                anchors.leftMargin: 8
                anchors.right: nfyVal.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                height: 28
                th: face.th
                Component.onCompleted: value = (face.notifyVol < 0 ? 75 : face.notifyVol)
                onValueChanged: face.act("notifyvol:" + value)
            }
        }

        // 2026-11-01 用户裁决：通知黑名单不是设计需求。真正的概念是「通知提供者」——
        //   课程/天气/自动化等提醒**来源**（生产者），见 ClassIsland 的通知模型。
        //   黑名单 UI 已移除；提供者视图将按 ClassIsland 模型重做（不是简单的来源名黑白名单）。
    }
}
