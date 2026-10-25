// 系统设置 · 分类「连接」（2026-10-25 从 598 行 SettingsPage 拆出）
//
// 只做"这台机器连到哪"：云端地址 / 连接状态 / 账号票状态。
// 身份与个人偏好不在这里 —— 那是「个人中心」的事（见 pages/account/）。
import QtQuick

import "../../components"

SettingsCard {
    id: card

    title: "连接"
    gap: 14

    property var ctx

    // 一行「键 70px + 值自适应」——Row 里给值设 parent.width 会成环，
    // 所以用 Item 定宽 + 锚点（原实现用 Row 恰好没炸是因为值不换行）。
    Column {
        width: parent.width
        spacing: 8

        Item {
            width: parent.width
            height: t0.implicitHeight
            Text {
                id: t0
                text: "云端"
                color: card.md3 ? card.md3.cOnSurfaceVar : "#49454F"
                font.pixelSize: 13
                width: 70
            }
            Text {
                anchors.left: t0.right
                anchors.right: parent.right
                text: backend.cloudUrl
                elide: Text.ElideRight
                color: card.md3 ? card.md3.cPrimary : "#6750A4"
                font.pixelSize: 13
            }
        }

        Item {
            width: parent.width
            height: t1.implicitHeight
            Text {
                id: t1
                text: "状态"
                color: card.md3 ? card.md3.cOnSurfaceVar : "#49454F"
                font.pixelSize: 13
                width: 70
            }
            Text {
                anchors.left: t1.right
                anchors.right: parent.right
                // 术语换日常词：不出现协议名
                text: backend.connected ? "连接正常" : "连不上云端"
                color: backend.connected
                       ? (card.md3 ? card.md3.cPrimary : "#6750A4")
                       : (card.md3 ? card.md3.cError : "#B3261E")
                font.pixelSize: 13
            }
        }

        Item {
            width: parent.width
            height: Math.max(t2.implicitHeight, t3.implicitHeight)
            Text {
                id: t2
                text: "账号"
                color: card.md3 ? card.md3.cOnSurfaceVar : "#49454F"
                font.pixelSize: 13
                width: 70
            }
            Text {
                id: t3
                anchors.left: t2.right
                anchors.right: parent.right
                wrapMode: Text.Wrap
                text: backend.accountText
                color: card.md3 ? card.md3.cPrimary : "#6750A4"
                font.pixelSize: 13
            }
        }
    }

    // 只在"没登录 / 失败了"这个可点的时候才出现
    Item {
        width: parent.width
        height: visible ? 28 : 0
        visible: !backend.accountBusy && backend.accountText.indexOf("未登录") === 0
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: backend.loginWithSite()
        }
        Text {
            anchors.centerIn: parent
            text: "点这里用网站账号登录"
            color: card.md3 ? card.md3.cPrimary : "#6750A4"
            font.pixelSize: 13
        }
    }
}
