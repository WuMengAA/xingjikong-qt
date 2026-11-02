// 账户与登录（统一卡 · 2026-10-25 抽出合并）
//
// 一处定义、三处复用，保证「身份」与「登录」的口径只有一份：
//   ① 个人中心（身份标明）
//   ② 首启引导第二步（oobe）
//   ③ 设置-账户与登录
//
// 注册在主站跨域完成（本系统不运营自建注册服务，openRegisterPage 打开 siteUrl/register）；
// 登录走星璃账号 OAuth 授权（OAuthLogin → 站点 authorizeViewer），本机被授权为「主站的项目子服务」，
// 不需要用户填密钥 / token，本机只留会话凭据，下次自动接入。
import QtQuick
import QtQuick.Layouts
import "./theme"   // Theme 设计令牌单例（pragma Singleton）

SettingsCard {
    id: card

    title: backend.loggedIn ? "账户与身份" : "登录星璃账号"
    subtitle: backend.loggedIn
              ? card.roleDesc(backend.role)
              : "本系统不运营自建注册服务，账号需到主站跨域注册；注册后回此登录，本机即被授权为「主站的项目子服务」，自动接入、无需填密钥。"

    property var ctx

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

    // ── 未登录：引导 + 登录 / 注册 ──
    Column {
        width: parent.width
        spacing: 10
        visible: !backend.loggedIn

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: backend.accountBusy
                  ? "正在等浏览器里授权…（授权完会自动接上）"
                  : "两步接入：① 点「注册新账号」去主站创建账号（跨域）；② 点「登录」在此完成 OAuth 授权，本机即被登记为「主站的项目子服务」，自动拿到接入票。"
            color: Theme.color.textSecondary
            font.pixelSize: 13
        }
        RowLayout {
            width: parent.width
            spacing: 10
            Btn {
                Layout.preferredWidth: 120
                theme: ({ })
                text: "登录"
                strong: true
                enabled: !backend.accountBusy
                onClicked: backend.loginWithSite()
            }
            Btn {
                Layout.preferredWidth: 120
                theme: ({ })
                text: "注册新账号"
                enabled: !backend.accountBusy
                onClicked: backend.openRegisterPage()
            }
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            visible: backend.registerUrl !== ""
            text: "注册页将打开：" + backend.registerUrl
            color: Theme.color.textSecondary
            font.pixelSize: 11
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            visible: backend.accountText !== ""
            text: backend.accountText
            color: Theme.color.textSecondary
            font.pixelSize: 12
        }
    }

    // ── 已登录：身份 + 切身份 + 退出 ──
    Column {
        width: parent.width
        spacing: 10
        visible: backend.loggedIn

        Row {
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: backend.accountName === "" ? "星璃账号" : backend.accountName
                color: Theme.color.textPrimary
                font.pixelSize: 16
                font.weight: Font.Medium
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                height: 22
                width: badge.implicitWidth + 16
                radius: 11
                color: Theme.color.primary
                Text {
                    id: badge
                    anchors.centerIn: parent
                    text: card.roleText(backend.role)
                    color: Theme.color.primaryForeground
                    font.pixelSize: 12
                }
            }
            // 未认证用户：邮箱未验证时的身份标识（与角色徽标并列，一眼可见）
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: !backend.verified
                height: 22
                width: unverifiedBadge.implicitWidth + 16
                radius: 11
                color: Theme.color.destructive
                Text {
                    id: unverifiedBadge
                    anchors.centerIn: parent
                    text: "未认证用户"
                    color: Theme.color.destructiveForeground
                    font.pixelSize: 12
                }
            }
        }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            visible: backend.loggedIn && !backend.verified
            text: "邮箱尚未验证。建议到主站（站点「账号」页）完成邮箱验证，以解除未认证限制。"
            color: Theme.color.textSecondary
            font.pixelSize: 12
        }

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            visible: backend.roleLocked
            text: "身份由服务器下发，本机不能切换。"
            color: Theme.color.textSecondary
            font.pixelSize: 12
        }

        // 服务端锁定身份时不给"换身份"按钮 —— 点了必然被拒，那是假按钮
        RowLayout {
            width: parent.width
            spacing: 10
            visible: !backend.roleLocked
            Text {
                text: "这一台的身份"
                color: Theme.color.textSecondary
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Btn {
                Layout.preferredWidth: 96
                Layout.preferredHeight: 26
                theme: ({ })
                text: "换成管理员"
                disabled: backend.role === "admin"
                onClicked: backend.setRole("admin")
            }
            Btn {
                Layout.preferredWidth: 96
                Layout.preferredHeight: 26
                theme: ({ })
                text: "换成教师"
                disabled: backend.role === "teacher"
                onClicked: backend.setRole("teacher")
            }
        }

        Btn {
            width: 168
            theme: ({ })
            text: "退出登录"
            tip: "清掉本机凭据；下次登录要重新授权"
            onClicked: backend.forgetAccount()
        }
    }
}
