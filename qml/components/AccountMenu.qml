// 星集控 · 顶栏「账户 / 身份」入口（2026-10-06 重写）
//
// 顶栏原来有个「控制 ┃ 被控」的分段控件 —— 那是开发期的自检开关：
// 管理端和被控端是**两台不同的程序**（被控端是 stelarith-agent-portable / 星集控被控端），
// 在这台机器上切个开关就能"假装自己是被控端"，纯粹是为了省一次切换程序的麻烦。
// 它留在正式界面里只有副作用：用户第一反应是"这两个到底什么关系/点了会怎样"，
// 而点下去什么也不会发生（另一台机器没在跑）。
//
// 现在换成真实产品的样子：
//   未登录 → 一颗「登录」按钮（点一下走星璃账号授权，回拨自动接上）
//   已登录 → 头像（账号首字）+ 账号名 + 角色徽标，点开是**身份菜单**：
//              切身份（管理员 / 教师）、看这个身份能干什么、退出登录、去设置。
// 「被控端是什么 / 上哪儿下」这类问题挪到设置-关于里讲清楚（那儿才放得下说明）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
// ⚠️ 本文件**就在** components/ 里，所以不能写 import "components" ——
// 那会去 components/components/ 找目录，找不到就整页 "Type AccountMenu unavailable"。
// Btn.qml 是同目录的兄弟文件，QML 会自动认，不用 import。

Item {
    id: acct

    property var theme: ({ })
    property bool open: menu.visible
    /** 主窗口把"跳到设置页"塞进来（这里够不着 root，别硬写死页号）。 */
    property var goSettings: null

    // 胶囊本体：一个 Rectangle 装「头像 + 名字 + 角色」
    Rectangle {
        id: chip
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 28
        width: acctRow.implicitWidth + 18
        radius: 14
        color: chipHover.containsMouse ? (acct.theme.cream || "#141414") : "transparent"
        border.color: acct.theme.stroke || "#242424"
        border.width: 1

        RowLayout {
            id: acctRow
            anchors.fill: parent
            anchors.margins: 4
            spacing: 8

            // 头像：账号首字。没账号就一枚空心圆（别画一个假头像）
            Rectangle {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                radius: 10
                color: backend.loggedIn ? (acct.theme.inv || "#F0F0F0") : "transparent"
                border.color: acct.theme.stroke || "#242424"
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: backend.loggedIn && backend.accountName !== ""
                              ? backend.accountName.charAt(0).toUpperCase() : "?"
                    color: backend.loggedIn ? (acct.theme.win || "#0A0A0A") : (acct.theme.fg4 || "#3A3A3A")
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
            }

            Text {
                id: chipName
                Layout.alignment: Qt.AlignVCenter
                text: backend.accountBusy ? "登录中…"
                    : backend.loggedIn ? (backend.accountName === "" ? "已登录" : backend.accountName)
                    : "登录"
                color: backend.loggedIn ? (acct.theme.fg || "#FAFAFA") : (acct.theme.op || "#C8C8C8")
                font.pixelSize: 12
                font.weight: backend.loggedIn ? Font.Normal : Font.Medium
            }

            // 角色徽标：没登录时不显示（"未登录"不是一个角色）
            Rectangle {
                id: badge
                Layout.alignment: Qt.AlignVCenter
                visible: backend.loggedIn
                height: 18
                width: badgeText.implicitWidth + 12
                radius: 9
                color: (acct.theme.cream || "#141414")
                border.color: acct.theme.stroke || "#242424"
                border.width: 1
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    text: backend.role === "admin" ? "管理员" : "教师"
                    color: acct.theme.op || "#C8C8C8"
                    font.pixelSize: 10
                }
            }
        }

        MouseArea {
            id: chipHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: acct.toggle()
        }
    }

    // 下拉菜单（浮在顶栏下面，右对齐）
    Popup {
        id: menu
        // ⚠️ 2026-10-06 二次修（第一次只夹了边界，没换坐标系，照旧飞出屏幕）：
        // Popup 打开时会被 reparent 到 Overlay.overlay 上，它的 x/y 是**窗口级**坐标；
        // 而 acct.x / acct.y 是胶囊**相对父容器**的坐标。两个坐标系一混，
        // 胶囊嵌在顶栏 Row 里（acct.x 常常只有几十），算出来的位置就飘到窗口左上角甚至屏幕外。
        // 另外旧式 `overlayH - height - 8` 在菜单比窗口还高时是**负数**，y 直接变负 → 出屏。
        // 现在：先用 mapToGlobal 把胶囊位置换算进 overlay 坐标系，再四边夹住、下放不下就翻上去。
        readonly property point anchorGlobal: acct.mapToGlobal(0, 0)
        readonly property point overlayGlobal: Overlay.overlay ? Overlay.overlay.mapToGlobal(0, 0) : Qt.point(0, 0)
        readonly property real ax: anchorGlobal.x - overlayGlobal.x
        readonly property real ay: anchorGlobal.y - overlayGlobal.y
        readonly property real avW: Overlay.overlay ? Overlay.overlay.width : 1280
        readonly property real avH: Overlay.overlay ? Overlay.overlay.height : 720
        // 菜单比可用高度还高时截到可用高度（配合下面的 clip，绝不许溢出到屏幕外）
        readonly property real wantH: Math.min(implicitHeight, Math.max(120, avH - 16))
        x: Math.max(8, Math.min(ax + acct.width - width, Math.max(8, avW - width - 8)))
        y: {
            var below = ay + acct.height + 6
            if (below + wantH <= avH - 8) return below          // 下面放得下就贴着胶囊下缘
            var above = ay - wantH - 6                          // 放不下就翻到胶囊上方
            return Math.max(8, above)                           // 上下都放不下就贴顶，绝不许负数
        }
        height: wantH
        width: 236
        modal: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: acct.theme.panel || "#101010"
            radius: acct.theme.rCard || 12
            border.color: acct.theme.stroke || "#242424"
            border.width: 1
        }

        contentItem: Column {
            spacing: 0
            padding: 8
            width: 236
            clip: true

            // ── 账号信息 ──
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 10
                text: backend.accountBusy ? "正在用星璃账号登录…"
                    : backend.loggedIn
                          ? (backend.accountName === "" ? "星璃账号" : backend.accountName)
                          : "还没登录星璃账号"
                color: acct.theme.fg || "#FAFAFA"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 10
                text: backend.loggedIn
                      ? (backend.role === "admin" ? "管理员身份：可以看画面，也能操作教室机"
                                                  : "教师身份：可以看画面、发通知，操作类动作要管理员")
                      : "登录后自动接入教室机，不用填任何密钥"
                wrapMode: Text.Wrap
                color: acct.theme.fg3 || "#5A5A5A"
                font.pixelSize: 11
            }

            // ⚠️ 分隔线就是分隔线：空行用 Item 撑，别指望 padding ——
            // 第一版把 topPadding 写在 Rectangle 上，QML 直接报 "Cannot assign to non-existent property"，
            // 整个弹窗连加载都过不去。竖着排的间距一律用下面这几行的 spacing / Item。
            Rectangle { width: parent.width; height: 1; color: acct.theme.sepline }
            Item { width: 1; height: 8 }

            // ── 没登录：登录 / 注册 ──
            Column {
                visible: !backend.loggedIn && !backend.accountBusy
                width: parent.width
                spacing: 8
                Item { width: 1; height: 10 }

                Btn {
                    width: parent.width
                    theme: acct.theme
                    text: backend.accountBusy ? "登录中…" : "登录"
                    strong: true
                    enabled: !backend.accountBusy
                    onClicked: { backend.loginWithSite(); menu.close() }
                }
                Btn {
                    width: parent.width
                    theme: acct.theme
                    text: "注册新账号"
                    onClicked: { backend.openRegisterPage(); menu.close() }
                }
            }

            // ── 已登录：切身份 + 退出 ──
            Column {
                visible: backend.loggedIn
                width: parent.width
                spacing: 8

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    text: "这一台上的身份"
                    color: acct.theme.fg3 || "#5A5A5A"
                    font.pixelSize: 11
                }

                // ⚠️ RowLayout / Column 都是定位器，**没有** padding 那组属性
                // （只有 spacing）。竖向间距一律用 spacing，别再往 topPadding 上写。
                RowLayout {
                    width: parent.width
                    spacing: 8

                    Repeater {
                        model: [ { k: "管理员", v: "admin" }, { k: "教师", v: "teacher" } ]
                        delegate: Btn {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            theme: acct.theme
                            text: modelData.k
                            strong: backend.role === modelData.v
                            tip: modelData.v === "admin" ? "能操作教室机（电源/远控/终端/广播）"
                                                         : "能看画面、发通知；操作类要管理员"
                            onClicked: {
                                backend.setRole(modelData.v)
                                menu.close()
                            }
                        }
                    }
                }

                Btn {
                    width: parent.width
                    Layout.topMargin: 8
                    theme: acct.theme
                    text: "退出登录（清掉本机凭据）"
                    onClicked: { backend.forgetAccount(); menu.close() }
                }
            }

            Rectangle { width: parent.width; height: 1; color: acct.theme.sepline }
            Item { width: 1; height: 8 }

            RowLayout {
                width: parent.width
                Btn {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    theme: acct.theme
                    text: "去设置"
                    onClicked: { if (acct.goSettings) acct.goSettings(); menu.close() }
                }
            }
        }
    }

    function toggle() {
        if (menu.visible) menu.close()
        else menu.open()
    }
}
