import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"     // Btn / ToggleRow（本端自有通用组件）
// 2026-10-18：MD3 布局组件（Md3SwitchRow / Md3ListItem / Md3NavBar…）。
// 真源在 Stelarith-shared-qml/ui/md3/，构建时由 CMake copy_directory 落到
// qml/Stelarith/components/md3/ —— 故源码树下 qmllint 解析不到（磁盘加载模式），
// 以「构建 + 运行」验证为准，与岛同套路。
import "../components/md3"

// 设置页（整屏页 · U5「两端同 shell」A-2 第一页）
//
// 从 `Main.qml` 的 `StackLayout` 第三个 `Item` 抽出（原 1547–2089，543 行）。
// 抽取规矩照 U2/U3 抽出面板那一套：
//
// 1. **只搬不改** —— 几何 / 文案 / 交互逐字保留（探针 + 人眼回归的判据）。
// 2. **隐式作用域搬不走**，依赖一律显式化：
//      `th`                  → 本页 `readonly property`（宿主经 ctx 传令牌）
//                              ⇒ body 里 81 处**裸 `th` 零改动**（同名属性接管）
//      `root.isPhone`        → `ctx.window.isPhone`
//      `openOnly(x)`         → `ctx.open(x)`（U3 起 openOnly 已由 `UiPanelHost` 承担）
//      `schedEditor.show()`  → `ctx.window.openScheduleEditor()`
//                              （`schedEditor` 是窗口的**子对象 id**，外部够不到 ⇒ 窗口包一层函数）
// 3. `backend` 是 C++ **上下文属性** —— 独立 QML 文件里同样可访问，**无需传参**
//    ⇒ 那 40 处引用零改动。
//
// ctx 形状 = `shared-qml/ui/UiPageHost.qml` 的 `_ctx()`（**同一份契约**）：
//   window / theme / registry / host / scope / open / close / closePanels
Item {
    id: page

    property var ctx

    // 设计令牌：宿主经 ctx 传入。
    // ⚠️ 刻意**不写 fallback 实例**：`Md3` 只存在于**产物树**的 `components/`
    //    （共享 ui 由构建期 `copy_directory` 拷过去），源树里没有 —— 写下来会让
    //    "直接吃源树"的探针加载失败。宿主保证给，这里只做空保护。
    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

    // 原注保留（说明"账户/提醒"两张卡为什么留白）：
    //   它们没有数据源 —— 规则要求"不假装能用"，所以卡片位置留着、内容留白，
    //   而不是写一句「尚未接入（无数据源）」的开发说明（规则 1）。
    Rectangle { anchors.fill: parent; color: th.body }

    // ⚠️ 2026-10-08：这里**必须可滚动**。设置页内容本来就超过一屏（六张卡），
    //    1280×720 的教室机上更紧。以前 GridLayout 直接 anchors.fill —— 内容一超
    //    就把最后几行挤成零高，「关于」整张卡（含「检查更新」）在屏幕上根本点不到
    //    （有截图实证）。改成 Flickable + 内容高度：卡片按内容撑开，超出的滚动看。
    //    注：下面各卡的 Layout.fillHeight 在这个「按内容定高」的网格里是惰性的，
    //    同一行的等高等由 GridLayout 本身保证，留着只是万一以后又改回填充式。
    Flickable {
        id: settingsFlick
        anchors.fill: parent
        anchors.margins: 18
        contentWidth: width
        contentHeight: settingsGrid.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            width: 6
        }

        GridLayout {
            id: settingsGrid
            width: parent.width
            columns: 2
            columnSpacing: 14
            rowSpacing: 14

            // ── 账户（真：OAuth 一户通，2026-10-06 打磨）──
            // 以前这张卡是空的：一行"还没有账户 / 登录后显示"，既不能登录也不能注册，
            // 用户看到的是"这功能没做完"而不是"我该点哪儿"。现在按状态给出口：
            //   没登录 → 登录 / 注册 两颗按钮（没登录就是没凭据，连云端也连不上，要说清）
            //   登录中 → 一句进度
            //   已登录 → 账号 + 角色 + 换身份
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: colSettings1.implicitHeight + 32
                Layout.fillHeight: true
                // 2026-10-18 MD3：卡片从最低档 panel 升 elevated（与概览页同语言）——cContLow 底 + outlineVar 细边
                radius: 12
                color: th.cContLow
                border.color: th.cOutlineVar
                border.width: 1
                Column {
                    id: colSettings1
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 14
                    // 2026-10-18 MD3：分节头 labelLarge（14px Medium + onSurfaceVar + 0.1 字距）
                Text { text: "账户"; color: th.cOnSurfaceVar; font.pixelSize: 14; font.weight: Font.Medium; font.letterSpacing: 0.1 }

                    // 没登录：两颗按钮 + 一句话说清"登录之后会发生什么"
                    Column {
                        visible: !backend.loggedIn
                        width: parent.width
                        spacing: 10
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: backend.accountBusy
                                  ? "正在等浏览器里授权…（授权完会自动接上教室机）"
                                  : "还没登录。点登录会用星璃账号授权，" +
                                    "之后这台机器自动拿到接入票，不用填密钥。"
                            color: th.op; font.pixelSize: 13
                        }
                        RowLayout {
                            width: parent.width
                            spacing: 10
                            Btn {
                                Layout.preferredWidth: 120
                                theme: th
                                text: "登录"
                                strong: true
                                enabled: !backend.accountBusy
                                onClicked: backend.loginWithSite()
                            }
                            Btn {
                                Layout.preferredWidth: 120
                                theme: th
                                text: "注册新账号"
                                enabled: !backend.accountBusy
                                onClicked: backend.openRegisterPage()
                            }
                        }
                    }

                    // 已登录：账号 + 角色
                    Column {
                        visible: backend.loggedIn
                        width: parent.width
                        spacing: 10
                        RowLayout {
                            width: parent.width
                            Text {
                                text: backend.accountName === "" ? "星璃账号" : backend.accountName
                                color: th.fg; font.pixelSize: 15
                                font.weight: Font.Medium
                            }
                            Rectangle {
                                height: 20
                                width: roleTxt.implicitWidth + 14
                                radius: 10
                                color: th.cream
                                border.color: th.stroke; border.width: 1
                                Text {
                                    id: roleTxt
                                    anchors.centerIn: parent
                                    anchors.leftMargin: 7
                                    anchors.rightMargin: 7
                                    text: backend.role === "admin" ? "管理员" : "教师"
                                    color: th.op; font.pixelSize: 12
                                }
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: backend.role === "admin"
                                  ? "管理员：可看画面、操作教室机。灰着的按钮是权限之外的操作。"
                                  : "教师：可看画面、发通知、推文件。操作类动作需管理员。"
                            color: th.fg3; font.pixelSize: 12
                        }
                        RowLayout {
                            width: parent.width
                            spacing: 10
                            Text { text: "这一台的身份"; color: th.fg3; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                            Btn {
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 26
                                theme: th
                                text: "换成管理员"
                                strong: backend.role !== "admin"
                                enabled: backend.role !== "admin"
                                tip: "切到管理员才能用操作类按钮"
                                onClicked: backend.setRole("admin")
                            }
                            Btn {
                                Layout.preferredWidth: 96
                                Layout.preferredHeight: 26
                                theme: th
                                text: "换成教师"
                                strong: backend.role === "admin"
                                enabled: backend.role === "admin"
                                tip: "切教师后操作类按钮置灰"
                                onClicked: backend.setRole("teacher")
                            }
                        }
                    }
                }
            }

            // ── 连接（真数据：运行时读，不写硬可能存在编造的值）──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: colSettings2.implicitHeight + 32
                Layout.fillHeight: true
                // 2026-10-18 MD3：卡片从最低档 panel 升 elevated（与概览页同语言）——cContLow 底 + outlineVar 细边
                radius: 12
                color: th.cContLow
                border.color: th.cOutlineVar
                border.width: 1
                Column {
                    id: colSettings2
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 14
                    // 2026-10-18 MD3：分节头 labelLarge（14px Medium + onSurfaceVar + 0.1 字距）
                Text { text: "连接"; color: th.cOnSurfaceVar; font.pixelSize: 14; font.weight: Font.Medium; font.letterSpacing: 0.1 }
                    // 稿 .kv：键 70px 辅助色，值 12px 操作色
                    Column {
                        width: parent.width
                        spacing: 6
                        Row {
                            Text { text: "云端"; color: th.fg3; font.pixelSize: 13; width: 70 }
                            Text { text: backend.cloudUrl; color: th.op; font.pixelSize: 13 }
                        }
                        Row {
                            Text { text: "状态"; color: th.fg3; font.pixelSize: 13; width: 70 }
                            Text {
                                // 术语换日常词：不说"已连接/未连接"以外的协议词（规则 2）
                                text: backend.connected ? "连接正常" : "连不上云端"
                                color: th.op; font.pixelSize: 13
                            }
                        }
                        // 网站账号（2026-10-06，OAuth 一户通）：账号过期/没登录时要能看见原因，
                        // 并且能当场点一下重新登录 —— 不能让用户去翻日志才知道"票过期了"。
                        Row {
                            Text { text: "账号"; color: th.fg3; font.pixelSize: 13; width: 70 }
                            Text {
                                id: acctText
                                text: backend.accountText
                                color: backend.accountBusy ? th.fg3 : th.op
                                font.pixelSize: 13
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                        }
                        // 只在"没登录 / 失败了"这个可点的时候才出现，已登录时界面不该多一个按钮
                        MouseArea {
                            visible: !backend.accountBusy && backend.accountText.indexOf("未登录") === 0
                            height: visible ? 26 : 0
                            width: parent.width
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.loginWithSite()
                            Text {
                                anchors.centerIn: parent
                                text: "点这里用网站账号登录"; color: th.op; font.pixelSize: 13
                            }
                        }
                    }
                }
            }

            // ── 提醒（2026-10-06：以前是画着玩的假开关，现在是真的）──
            // 两个开关都在后端接了真事件：操作完成（机器真做了才提示）、
            // 与云端断开。状态落在 QSettings，重开程序还在。
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: colSettings3.implicitHeight + 32
                Layout.fillHeight: true
                // 2026-10-18 MD3：卡片从最低档 panel 升 elevated（与概览页同语言）——cContLow 底 + outlineVar 细边
                radius: 12
                color: th.cContLow
                border.color: th.cOutlineVar
                border.width: 1
                Column {
                    id: colSettings3
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 14
                    // 2026-10-18 MD3：分节头 labelLarge（14px Medium + onSurfaceVar + 0.1 字距）
                Text { text: "提醒"; color: th.cOnSurfaceVar; font.pixelSize: 14; font.weight: Font.Medium; font.letterSpacing: 0.1 }
                    Column {
                        width: parent.width
                        spacing: 4
                        // 2026-10-18 MD3 布局：开关行从手写 ToggleRow 换成 Md3SwitchRow
                        // （56/72 高列表行 + 52×32 轨道开关 + supporting 说明），
                        // 与设置页其它分节的 MD3 语言一致——这是"列表行"的 MD3 标准形态。
                        Md3SwitchRow {
                            width: parent.width
                            md3: th
                            text: "操作完成时提示"
                            supporting: "机器上真的做完了才说一句（云端收下不算）"
                            checked: backend.notifyOnDone
                            onToggled: function (on) { backend.notifyOnDone = on }
                        }
                        Md3SwitchRow {
                            width: parent.width
                            md3: th
                            text: "设备离线时提醒"
                            supporting: "与云端断开时弹一条（后台也能看见）"
                            checked: backend.notifyOnOffline
                            onToggled: function (on) { backend.notifyOnOffline = on }
                        }
                    }
                }
            }

            // ── 启动（2026-10-09 · 用户清单第②条：开机自启 / 桌面快捷方式）──
            // 管理端是「老师自己的机器」⇒ 走**当前用户**范围、**免提权**：
            //   自启写 HKCU\...\Run；快捷方式建在当前用户桌面。
            // 状态直接读后端属性（backend.autoStart / desktopShortcut = 现读注册表/桌面），
            // 不缓存到 QML，避免"界面说开着、注册表其实没有"的假象。
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: colSettingsBoot.implicitHeight + 32
                Layout.fillHeight: true
                // 2026-10-18 MD3：卡片从最低档 panel 升 elevated（与概览页同语言）——cContLow 底 + outlineVar 细边
                radius: 12
                color: th.cContLow
                border.color: th.cOutlineVar
                border.width: 1
                visible: !ctx.window.isPhone   // 开机自启/桌面快捷方式 = Windows 专属，手机隐藏
                Column {
                    id: colSettingsBoot
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 14
                    // 2026-10-18 MD3：分节头 labelLarge（14px Medium + onSurfaceVar + 0.1 字距）
                Text { text: "启动"; color: th.cOnSurfaceVar; font.pixelSize: 14; font.weight: Font.Medium; font.letterSpacing: 0.1 }
                    Column {
                        width: parent.width
                        spacing: 4
                        // 2026-10-18 MD3 布局：同提醒区，启动项开关行也换成 Md3SwitchRow
                        Md3SwitchRow {
                            width: parent.width
                            md3: th
                            text: "开机自动启动"
                            supporting: "登录 Windows 后自动打开（免管理员）"
                            checked: backend.autoStart
                            onToggled: function (on) { backend.setAutoStart(on) }
                        }
                        Md3SwitchRow {
                            width: parent.width
                            md3: th
                            text: "桌面快捷方式"
                            supporting: "在桌面放一枚「星集控管理端」图标"
                            checked: backend.desktopShortcut
                            onToggled: function (on) { backend.setDesktopShortcut(on) }
                        }
                    }
                }
            }

            // ── 外观（真：深浅切换在这版已经可用）──
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: colSettings4.implicitHeight + 32
                Layout.fillHeight: true
                // 2026-10-18 MD3：卡片从最低档 panel 升 elevated（与概览页同语言）——cContLow 底 + outlineVar 细边
                radius: 12
                color: th.cContLow
                border.color: th.cOutlineVar
                border.width: 1
                Column {
                    id: colSettings4
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 14
                    Text { text: "外观"; color: th.fg3; font.pixelSize: 12 }
                    // E4：三档（浅色 / 深色 / 跟随系统），选中的那块边框走反白
                    RowLayout {
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: [
                                { k: "浅色", m: 0 },
                                { k: "深色", m: 1 },
                                { k: "跟随系统", m: 2 }
                            ]
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                radius: th.rCtrl
                                color: "transparent"
                                border.color: (backend.themeMode === modelData.m) ? th.inv : th.stroke
                                border.width: 1
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.k
                                    color: (backend.themeMode === modelData.m) ? th.fg : th.fg3
                                    font.pixelSize: 13
                                    font.weight: (backend.themeMode === modelData.m) ? Font.Medium : Font.Normal
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.themeMode = modelData.m
                                }
                            }
                        }
                    }
                }
            }

            // ── 课表编辑器（2026-10-06 并入）──
            // 风格与「外观」等卡片一致：黑白、自绘按钮（不用 QtQuick.Controls 默认 Button，
            // 那个默认蓝会把黑白界面破掉）。
            Rectangle {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.preferredHeight: colSettings5.implicitHeight + 32
                Layout.fillHeight: true
                // 2026-10-18 MD3：卡片从最低档 panel 升 elevated（与概览页同语言）——cContLow 底 + outlineVar 细边
                radius: 12
                color: th.cContLow
                border.color: th.cOutlineVar
                border.width: 1
                Column {
                    id: colSettings5
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 10
                    Text { text: "课表编辑器"; color: th.fg3; font.pixelSize: 12 }
                    Text {
                        text: "排课、时间轴、科目与多周轮换（兼容 ClassIsland）"
                        color: th.op; font.pixelSize: 13
                        wrapMode: Text.Wrap
                        width: parent.width
                    }
                    // 自绘按钮：反白风格与稿稿一致
                    Rectangle {
                        width: 132
                        height: 32
                        radius: th.rCtrl
                        color: hovered ? th.hover : th.cream
                        border.color: th.stroke
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: "打开课表编辑器"
                            color: th.fg
                            font.pixelSize: 13
                        }
                        MouseArea {
                            id: hovered
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ctx.window.openScheduleEditor()
                        }
                    }
                }
            }

            // ── 关于（通栏）──
            // 版本以前写「—」（等于没做），现在读编译期注入的 backend.version。
            // 「被控端是什么」以前在顶栏那个死开关上没地方讲 —— 挪到这儿，
            // 一句话说清两台程序的关系 + 上哪儿拿被控端。
            Rectangle {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                Layout.preferredHeight: colSettings6.implicitHeight + 32
                Layout.fillHeight: true
                // 2026-10-18 MD3：卡片从最低档 panel 升 elevated（与概览页同语言）——cContLow 底 + outlineVar 细边
                radius: 12
                color: th.cContLow
                border.color: th.cOutlineVar
                border.width: 1
                Column {
                    id: colSettings6
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 6
                    // 2026-10-18 MD3：分节头 labelLarge（14px Medium + onSurfaceVar + 0.1 字距）
                Text { text: "关于"; color: th.cOnSurfaceVar; font.pixelSize: 14; font.weight: Font.Medium; font.letterSpacing: 0.1 }
                    Text { text: "星集控 · 管理端"; color: th.fg; font.pixelSize: 13 }
                    Row {
                        spacing: 0
                        Text { text: "版本"; color: th.fg3; font.pixelSize: 12; width: 70 }
                        Text { text: backend.version; color: th.op; font.pixelSize: 12 }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "被控端装在教室机上（星集控被控端），" +
                              "登录同一账号后就会出现在这里。" +
                              "要看某台机器，在控制页左边选。"
                        color: th.fg3; font.pixelSize: 12
                    }
                    // 更新日志 / 帮助：把官网已有的「版本历史」与「帮助」能力搬进管理端（需求 #7）。
                    // 之前管理端缺这两块、也没有替代，老师想看更新了什么只能去翻网页。
                    Row {
                        spacing: 8
                        Rectangle {
                            width: 96; height: 30; radius: th.rCtrl
                            color: clHover.containsMouse ? th.hover : th.panel
                            border.color: th.stroke; border.width: 1
                            Text { anchors.centerIn: parent; text: "更新日志"; color: th.fg; font.pixelSize: 13 }
                            MouseArea { id: clHover; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ctx.open("changelog") }
                        }
                        Rectangle {
                            width: 80; height: 30; radius: th.rCtrl
                            color: hpHover.containsMouse ? th.hover : th.panel
                            border.color: th.stroke; border.width: 1
                            Text { anchors.centerIn: parent; text: "帮助"; color: th.fg; font.pixelSize: 13 }
                            MouseArea { id: hpHover; anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: ctx.open("help") }
                        }
                        // 诊断与反馈：出错时唯一能把信息交出去的地方（GUI 子系统后没控制台）
                        Rectangle {
                            width: 110; height: 30; radius: th.rCtrl
                            color: dgHover.containsMouse ? th.hover : th.panel
                            border.color: th.stroke; border.width: 1
                            Text { anchors.centerIn: parent; text: "诊断与反馈"; color: th.fg; font.pixelSize: 13 }
                            MouseArea {
                                id: dgHover
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { diagDlg.refreshLog(); diagDlg.open() }
                            }
                        }
                    }
                    // ── 更新（2026-10-08）──
                    // 状态机在 C++ 的 Updater 里，界面只做三件事：把 updater.message 原样显示、
                    // 按状态决定按钮文案与灰否、把更新说明摊开。「有没有新版」是云端算的
                    // （客户端只把本地版本号发过去），界面不自己比版本。
                    // ⚠️ 行数都封顶：这张卡在 GridLayout 里，涨太高会把上面的卡挤扁。
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        visible: updater.message !== ""
                        text: updater.message
                        color: updater.state === "failed" ? th.fg : th.fg3
                        font.pixelSize: 12
                        font.bold: updater.state === "failed"
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        visible: updater.state === "available" && updater.notes !== ""
                        text: updater.sizeBytes > 0
                              ? ("更新说明：" + updater.notes +
                                 "（" + (updater.sizeBytes / 1048576).toFixed(1) + " MB）")
                              : ("更新说明：" + updater.notes)
                        color: th.fg3; font.pixelSize: 12
                    }
                    RowLayout {
                        width: parent.width
                        spacing: 10
                        Btn {
                            Layout.preferredWidth: 118
                            Layout.preferredHeight: 26
                            theme: th
                            text: updater.busy ? "处理中…" : "检查更新"
                            tip: "检查云端是否有新版本"
                            disabled: updater.busy
                            onClicked: updater.checkForUpdate()
                        }
                        // 只在"确实有新版本"时出现 —— 常驻一颗不能点的按钮只会让人犯嘀咕。
                        Btn {
                            Layout.preferredWidth: 118
                            Layout.preferredHeight: 26
                            theme: th
                            visible: updater.state === "available"
                            strong: true
                            text: updater.mandatory ? "必须更新" : "立即更新"
                            tip: "下载、校验、解压、自动重启，全程无需手动操作"
                            disabled: updater.busy
                            onClicked: updater.installUpdate()
                        }
                        Btn {
                            Layout.preferredWidth: 118
                            Layout.preferredHeight: 26
                            theme: th
                            text: "去官网下载页"
                            tip: "打开 www.245959623.xyz/download"
                            onClicked: backend.openExternal("https://www.245959623.xyz/download")
                        }
                    }
                    Text {
                        text: "日志 stelarith-viewer-qt/viewer.log"
                        color: th.fg3; font.pixelSize: 12
                    }
                }
            }
        }
    }
}
