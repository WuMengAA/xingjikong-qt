// Step 1 · 设备用途（2026-10-25）
//
// 为什么这一步必须放在「选绑定方式」**之前**：
//   · 用途决定后面绑的是什么 —— 教室端绑班级，广播端绑广播站（站码与班码同一命名空间）；
//   · 用途决定这台机器启动后开哪套外壳（教室面 / 广播台）；
//   · 用户在向导里选了广播端却点「稍后配置」也是真实路径 ⇒ 用途必须当场生效，
//     不能挂在「绑定成功」后面（那是把身份的一部分当成绑定的副产品）。
//
// 交互：两张巨卡整卡可点，选完直接进下一步（不需要再点「下一步」）。
import QtQuick
import "../../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一）
import ".."

Item {
    id: page
    property var wizard: null

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 560)
        spacing: 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "这台设备用来做什么？"
            font.pixelSize: 24
            font.weight: Font.Bold
            color: Theme.color.textPrimary
        }

        Item { width: 1; height: 8 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "用途决定这台设备开哪套界面；以后可在管理端修改。"
            font.pixelSize: 13
            color: Theme.color.textSecondary
        }

        Item { width: 1; height: 32 }

        OobeCardButton {
            width: parent.width
            glyph: "教"
            title: "教室端"
            subtitle: "接在教室大屏上：收通知与黑板卡、上报画面、参与班级管理"
            recommended: true
            onClicked: {
                oobe.chooseRole("classroom")
                page.wizard.gotoStep(2)
            }
        }

        Item { width: 1; height: 16 }

        OobeCardButton {
            width: parent.width
            glyph: "广"
            title: "广播端"
            subtitle: "学校广播室专用：播放排期与校园广播，不参与班级画面"
            onClicked: {
                oobe.chooseRole("broadcast")
                page.wizard.gotoStep(2)
            }
        }

        Item { width: 1; height: 24 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "返回"
            font.pixelSize: 13
            color: Theme.color.textSecondary
            opacity: backArea.containsMouse ? 1.0 : 0.75
            MouseArea {
                id: backArea
                anchors.fill: parent
                anchors.margins: -8
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.wizard.gotoStep(0)
            }
        }
    }
}
