// 个人中心 · 账户卡（2026-10-25 从 AccountCenterPage 抽出，拆件过 200 行红线）
//
// 三态：未登录（登录/注册两个出口）→ 登录中（一句进度）→ 已登录（账号 + 身份徽标
// + 换身份 + 退出登录）。身份文案三档，第三档为广播室预留。
import QtQuick
import QtQuick.Layouts

import "../../components"

SettingsCard {
    id: card

    title: "账户"
    subtitle: backend.loggedIn ? card.roleDesc(backend.role) : ""

    property var ctx

    /** 身份文案：管理员 / 教师（含班主任）/ 广播室。 */
    function roleText(r) {
        if (r === "admin") return "管理员"
        if (r === "broadcast") return "广播室"
        return "教师"
    }
    function roleDesc(r) {
        if (r === "admin") return "可管理班级、设备与系统配置；危险动作需要二次确认。"
        if (r === "broadcast") return "广播室身份：只能使用广播与排期相关功能。"
        return "可查看本班设备、发布通知与作业、管理班级事务；系统配置由管理员负责。"
    }

    // ── 未登录 ──
    Column {
        width: parent.width
        spacing: 10
        visible: !backend.loggedIn

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: backend.accountBusy
                  ? "正在等浏览器里授权…（授权完会自动接上）"
                  : "还没登录。点登录会用星璃账号授权，之后这台设备自动拿到接入票，不用填密钥。"
            color: card.md3 ? card.md3.cOnSurfaceVar : "#49454F"
            font.pixelSize: 13
        }
        RowLayout {
            width: parent.width
            spacing: 10
            Btn {
                Layout.preferredWidth: 120
                theme: card.md3
                text: "登录"
                strong: true
                enabled: !backend.accountBusy
                onClicked: backend.loginWithSite()
            }
            Btn {
                Layout.preferredWidth: 120
                theme: card.md3
                text: "注册新账号"
                enabled: !backend.accountBusy
                onClicked: backend.openRegisterPage()
            }
        }
    }

    // ── 已登录 ──
    Column {
        width: parent.width
        spacing: 10
        visible: backend.loggedIn

        Row {
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: backend.accountName === "" ? "星璃账号" : backend.accountName
                color: card.md3 ? card.md3.cOnSurface : "#1C1B1F"
                font.pixelSize: 16
                font.weight: Font.Medium
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: 22
                width: badge.implicitWidth + 16
                radius: 11
                color: card.md3 ? card.md3.cSecondaryCont : "#E8DEF8"
                Text {
                    id: badge
                    anchors.centerIn: parent
                    text: card.roleText(backend.role)
                    color: card.md3 ? card.md3.cOnSecondaryCont : "#1D192B"
                    font.pixelSize: 12
                }
            }
        }

        // 服务端锁定身份时不给"换身份"按钮 —— 点了必然被拒，那是假按钮
        RowLayout {
            width: parent.width
            spacing: 10
            visible: !backend.roleLocked
            Text {
                text: "这一台的身份"
                color: card.md3 ? card.md3.cOnSurfaceVar : "#49454F"
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Btn {
                Layout.preferredWidth: 96
                Layout.preferredHeight: 26
                theme: card.md3
                text: "换成管理员"
                disabled: backend.role === "admin"
                onClicked: backend.setRole("admin")
            }
            Btn {
                Layout.preferredWidth: 96
                Layout.preferredHeight: 26
                theme: card.md3
                text: "换成教师"
                disabled: backend.role === "teacher"
                onClicked: backend.setRole("teacher")
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            visible: backend.roleLocked
            text: "身份由服务器下发，本机不能切换。"
            color: card.md3 ? card.md3.cOnSurfaceVar : "#49454F"
            font.pixelSize: 12
        }

        Btn {
            width: 168
            theme: card.md3
            text: "退出登录"
            tip: "清掉本机凭据；下次登录要重新授权"
            onClicked: backend.forgetAccount()
        }
    }
}
