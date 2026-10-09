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
    /** 宿主层：Main.qml 传进来的 acctLayer（z:9990，罩在所有页面之上）。
     *  它存在只为让调用点的赋值合法 —— 当前 Popup 仍走 Overlay.overlay 那条
     *  （见下面 anchorGlobal / ax / ay 的旧逻辑）。接手的人把 popup.parent 换到
     *  这一层、并把 modal 遮罩一并挂进来，遮罩就不会再压住菜单。
     *  ⚠️ 少了这一行声明，Main.qml 里的 `hostLayer: acctLayer` 会直接让整窗
     *     QML 加载失败（Cannot assign to non-existent property），程序连起来都起不来。
     */
    property var hostLayer: null

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
                    color: backend.loggedIn ? (acct.theme.win || "#0A0A0A") : (acct.theme.fg4 || "#707070")
                    font.pixelSize: 12
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
                font.pixelSize: 13
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
                    font.pixelSize: 12
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
        // ⚠️⚠️ 父层**必须显式给**（这就是「用户面板不在屏幕内」的真根因，2026-10-08）：
        //
        //   不给的话 Popup 会落到"声明处"的坐标空间 —— 这个组件声明在**顶栏那一格里**
        //   （150x28 的小 Item），于是 x/y 被当成相对那一格的偏移。
        //   探针实测：菜单 x=926 被解释成"胶囊右边 926px" ⇒ 绝对位置 ≈ 窗口外 1900px，
        //   **一个像素都没画出来**（但 menu.visible/opened 全是 true，日志也一片正常）。
        //
        //   为什么不是自动挂到 Overlay.overlay 上：本工程里 `Overlay.overlay` 在弹窗打开
        //   之前读是 **null**（探针：overlayNull=true、parent=AccountMenu_QMLTYPE_63），
        //   挂在顶栏那一格上，坐标空间就全错了。前两次修（换算法、加边界夹取）都栽在
        //   "坐标系到底是谁的"这件事上，没治到父层。
        //
        //   hostLayer = Main.qml 里的 acctLayer：铺满窗口、z=9990。挂在它上面，
        //   x/y 天然就是**窗口坐标**（下面的 place() 也按窗口算），菜单既在窗口里、
        //   又压在所有页面之上。
        parent: hostLayer
        x: 0
        y: 0
        width: 236
        height: 200                 // 打开前 place() 会按内容重算
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
                font.pixelSize: 14
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
                color: acct.theme.fg3 || "#8A8A8A"
                font.pixelSize: 12
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
                    color: acct.theme.fg3 || "#8A8A8A"
                    font.pixelSize: 12
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

    /**
     * 每次打开**现算**落点，绝不依赖绑定缓存。
     * 细节见 menu 那段注释（这是「账户面板飞到屏幕外」的修复本体）。
     */
    function place() {
        // 参考层：铺满窗口的那一层（hostLayer）。它拿不到时退到自己的父（同类空间）。
        var ref = (hostLayer && hostLayer.width > 0) ? hostLayer : acct.parent
        if (!ref) return
        var p = acct.mapToItem(ref, 0, 0)          // 胶囊在这层里的坐标 = 窗口坐标
        var wantH = Math.min(menu.implicitHeight, Math.max(120, ref.height - 16))
        // 右对齐到胶囊右缘，再四边夹住（窗口很窄时也不会贴出右边界）
        var mx = p.x + acct.width - menu.width
        mx = Math.max(8, Math.min(mx, Math.max(8, ref.width - menu.width - 8)))
        // 默认贴胶囊下缘；下面放不下就翻到上面；上下都放不下就贴顶
        // ⚠️ 最后那次 Math.max(8, …) 必须**同时夹上下**：只夹上不夹下的话，
        //    菜单比窗口还高时 y 会算成负数（老版就是这么飞出去的）。
        var my = p.y + acct.height + 6
        if (my + wantH > ref.height - 8) my = p.y - wantH - 6
        my = Math.max(8, Math.min(my, Math.max(8, ref.height - wantH - 8)))
        menu.height = wantH
        menu.x = mx
        menu.y = my
        console.log("[acct] 菜单落点 (" + mx + "," + my + ") " + menu.width + "x" + menu.height
                    + "（参考层 " + ref.width + "x" + ref.height + "，胶囊 " + p.x + "," + p.y + "）")
    }

    function toggle() {
        if (menu.visible) menu.close()
        else { place(); menu.open() }
    }
}
