import QtQuick 2.15
import "."

// 被控端灵动岛 · QML 根组件
//
// 替代 src/island.cpp（935 行纯 QPainter 手绘）的视觉层。
// 系统层（无边框窗口/置顶/穿透/几何动画/生命周期）仍由 C++ IslandOverlay 持有。
//
// ── 接口契约（批次 3 由 IslandBridge 提供，签名不得改）──────────────
//   bridge.title / desc / icon / form / severity / light / idle / expanded
//   bridge.states     多状态列表 [{k,t,d,p}]
//   bridge.actions    [{label, role}]
//   bridge.fireAction(label)   QML → C++ 回调
//   bridge.requestExpand(bool) 手势 → C++
//   bridge.history     [{title,desc,icon,ts,timeStr}]  历史通知列表
//   bridge.dismissHistory(index)              清除一条历史
//   bridge.clearHistory()                     清空全部历史
//
    // ── 尺寸模型 ───────────────────────────────────────────
    //   root 用 implicitWidth / implicitHeight 显式给出目标尺寸。
    //   shell 用 anchors.fill: parent 跟随 root 实际大小。
    //   宿主 QQuickWidget (SizeRootObjectToView) 先取 root.implicitSize
    //   来 resize view，然后 root 实际 w/h 由 view 尺寸驱动。
    //   ⚠️ 不给 width/height 显式赋值：会被 view 覆盖，且与 Behavior 打架。
    //   ⚠️ 之前试过让 shell 用 anchors.fill + root.implicitHeight 靠内容推导，
    //      循环依赖把 implicitHeight 压到最小、岛被压扁——所以改成显式三段式。
Item {
    id: root

    // ⚠️⚠️ 属性名**不能**叫 bridge！
    //    QML 的名字解析顺序里，**对象自身的属性**优先于上下文属性（context property）。
    //    写 `property var bridge: typeof bridge !== "undefined" ? bridge : null`
    //    时，右边那个 `bridge` 解析到的是**它自己**（恒 undefined）⇒ 永远 null。
    //    症状极具欺骗性：QML errors=0、加载成功、几何正确，但所有数据都是空/默认值。
    //    （2026-10-11 实测踩到。同族坑：挂子组件时写 `backend: backend` 也是这个。）
    //    正解：起个不冲突的名字。C++ 侧 contextProperty 仍叫 "bridge"。
    readonly property var bridgeRef: (typeof bridge !== "undefined" && bridge !== null) ? bridge : null

    // 标题字号自适应度量器（fitTitlePx 用）。不显示、不参与布局。
    TextMetrics {
        id: tm
        font.family: "Microsoft YaHei UI, PingFang SC, sans-serif"
        font.weight: Font.Bold
    }

    // ── 主题 ────────────────────────────────────────────────
    // ⚠️ 不用 pragma Singleton（需深浅并存），用普通组件 + 两实例
    IslandTheme { id: thLight; light: true }
    IslandTheme { id: thDark;  light: false }
    readonly property var th: root._light ? thLight : thDark

    // ── 从桥取状态 ──────────────────────────────────────────
    // ⚠️⚠️ 绝不用 bridge[k] 动态索引取属性！
    //    QQml 的绑定依赖是**静态**分析的：bridge["title"] 这种写法编译器看不见
    //    "root 依赖 bridge.title"，于是 bridge.changed() 发出来时这些 readonly property
    //    **不会重新求值** —— 它们在组件创建那一刻算一次就锁死。
    //    （2026-10-11 实测踩到：QML 加载成功、errors=0、几何也变，但 title/desc/states
    //      全空，屏幕上只有个空胶囊。原因就是这里。）
    //    正解：每个属性写一条**点访问**绑定；桥把全部 property 的 NOTIFY 都指向同一个
    //    changed() 信号，所以一条信号能让下面全部重读。
    readonly property bool   _idle:       bridgeRef ? bridgeRef.idle        : true
    readonly property bool   _expanded:   bridgeRef ? bridgeRef.expanded    : false
    readonly property bool   _light:      bridgeRef ? bridgeRef.light       : false
    readonly property bool   _interactive:bridgeRef ? bridgeRef.interactive : false
    readonly property string _severity:   bridgeRef ? bridgeRef.severity    : ""
    readonly property string _title:      bridgeRef ? bridgeRef.title       : ""
    readonly property var    _states:     (bridgeRef && bridgeRef.states)  ? bridgeRef.states  : []
    readonly property var    _actions:    (bridgeRef && bridgeRef.actions) ? bridgeRef.actions : []
    readonly property var    _history:    (bridgeRef && bridgeRef.history) ? bridgeRef.history : []

    // ── 文件传输进度（#198，桥 fileTransfer* 属性）──────────
    // ⚠️ 同样是点访问绑定（不是 bridge["x"]），靠 bridge.changed() 一把全 NOTIFY 重读。
    readonly property bool   _ftActive:   bridgeRef ? bridgeRef.fileTransferActive : false
    // ⚠️ QML 没有 qint64 类型！用 real（double）承接 C++ 的 qint64 字节数属性。
    //    之前写 qint64 在 Qt 6.8 下被宽松放过，Qt 6.12 严格化后直接拒载整个 IslandRoot
    //    ⇒ 岛变透明空白（"灵动岛消失"）。2026-10-12 修。
    readonly property real  _ftBytes:    bridgeRef ? bridgeRef.fileTransferBytes  : 0
    readonly property real  _ftTotal:    bridgeRef ? bridgeRef.fileTransferTotal  : 0
    readonly property real   _ftPct:      (root._ftTotal > 0) ? Math.min(1, root._ftBytes / root._ftTotal) : 0

    // ── 尺寸（显式，不靠隐式推导）───────────────────────────
    readonly property int padH: 14
    // 展开态"标题 + 描述"占的头部高度（自适应字号后标题可达 64px，2 行 128px）。
    // 给列表/历史面板定位当 top 锚点，避免压到标题。
    readonly property int headH: root._expanded
        // ⚠️ 传输中 exDesc 被隐藏（改由进度条呈现），不能再把它的高度算进来，
        //    否则头部多算一块空白、把下面的历史区顶出去。
        ? Math.max(46, 10 + exTitle.height + 6 + (root._ftActive ? 0 : exDesc.height))
        : 0
    readonly property int listH: (root._expanded && stList.rows > 0) ? stList.implicitHeight + 8 : 0
    // 历史面板高度：展开态 + 有历史 + 无活跃按钮时才给空间。
    // 2026-10-12 修「展开后太矮、历史显示不全」：每条 44→48px、封顶 200→300px，
    // 头部按 histHeader 实际高度 32px 算（原来是拍脑袋的 36，与面板实高不一致）。
    readonly property int histH: (root._expanded && root._history.length > 0 && !root._showActionsNow)
        ? Math.min(300, root._history.length * 48 + 32)
        : 0
    readonly property int actH:  (root._actions.length > 0) ? 34 : 0
    // 文件传输进度条占的额外高度（展开态 + 传输中才给）
    readonly property int ftH: (root._expanded && root._ftActive) ? 46 : 0
    readonly property int collapsedH: root.th.shapeCollapsed

    // 有动作按钮时：胶囊也走"加高"逻辑（Centered/Fullscreen 本来就高）
    readonly property bool _showActionsNow: root._actions.length > 0 && !root._idle

    // ── 展开态标题自适应字号（15 → 64px，按字数收敛）──────────
    // "自适应大小"：短标题用超大字（大数字/关键提示），长标题降级到可读。
    // 宽按当前形态（idle 不看这里；expanded=400，collapsed=320）给 2 行预算。
    // 纯计算 + TextMetrics 度量，不依赖任何隐式布局。
    function fitTitlePx(t) {
        var n = t ? t.length : 0
        var fs = (n >= 33) ? root.th.fsTitleXs
              : (n >= 28) ? root.th.fsTitleSm
              : (n >= 17) ? root.th.fsTitleMd
              : (n >= 9)  ? root.th.fsTitleLg
              : (n >= 5)  ? root.th.fsTitleXl
              :             root.th.fsTitleXxl
        if (n === 0) return fs
        var w = (root._expanded ? 400 : 320) - root.padH * 2 - 48
        // 从 fs 往下探，找到"单行或两行能进宽"的最大字号
        for (; fs >= root.th.fsTitleXs; fs -= 2) {
            tm.font.pixelSize = fs
            tm.text = t
            if (tm.width <= w) return fs   // 单行能进
            // 两行预算 ≈ 2 * 平均行宽；粗略用 n*0.6*fs 估算字符宽
            var twoLine = (n * 0.55 * fs) / 2
            if (twoLine <= w) return fs
        }
        return root.th.fsTitleXs
    }

    // #198：体积文案（与 C++ humanBytes 同 1024 进制，只到 GB）
    function fxHumanBytes(n) {
        if (n < 1024) return String(n) + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KB"
        if (n < 1024 * 1024 * 1024) return (n / 1024 / 1024).toFixed(1) + " MB"
        return (n / 1024 / 1024 / 1024).toFixed(2) + " GB"
    }
    readonly property int exTitlePx: root.fitTitlePx((bridgeRef ? bridgeRef.title : ""))

    implicitWidth: root._idle ? root.th.shapeIdle * 3    // 4:3 比例 (44*3=132)
                               : (root._expanded ? 400 : 320)
    implicitHeight: root._idle ? root.th.shapeIdle
                               : (root._expanded
                                  ? Math.max(140, root.padH + root.headH + root.listH + root.histH + root.ftH + root.actH + root.padH)
                                  : (root._showActionsNow
                                     ? Math.max(root.collapsedH + root.actH + 10, 90)
                                     : root.collapsedH))
    // ⚠️ 不给 width/height 赋值：宿主 QQuickWidget 用 SizeRootObjectToView 时，
    //    根 Item 的 w/h 由 view 尺寸驱动 —— 但 QQuickWidget 会先取 root.implicitSize
    //    来 resize view（QQuickWidget::SizeRootObjectToView 语义即"view 跟随 root implicit"）。
    //    显式写 width: implicitWidth 反而被 view 覆盖，且与 Behavior 打架。

    Behavior on width  { NumberAnimation { duration: root.th.durShape; easing.type: Easing.OutBack } }
    Behavior on height { NumberAnimation { duration: root.th.durShape; easing.type: Easing.OutBack } }

    // ══════════════════════════════════════════════════════════
    // 手势 MouseArea（⚠️ 必须在最底层，否则吃掉按钮点击）
    // ══════════════════════════════════════════════════════════
    // ⚠️⚠️ 2026-10-11 修 bug「确认按钮点不到」：
    //    原来 MouseArea 声明在**最后**（z 层最高），ActionButton 的 MouseArea 在它下面
    //    ⇒ 点击被根 MouseArea 吞掉，ActionButton 永远收不到 onClicked。
    //    修法：把 MouseArea 移到**最前面**（z 最低），让上面的按钮先收到事件。
    //    Rectangle 不吃鼠标事件，所以空白区的点击自然穿透到这里。
    MouseArea {
        id: gestureArea
        anchors.fill: parent
        z: -1   // 确保在所有内容之下
        enabled: root._interactive || !root._idle
        acceptedButtons: Qt.LeftButton
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            // 有动作按钮时不做展开/收起（避免点按钮周围误触展开）
            if (root._showActionsNow) return
            if (root.bridgeRef && root.bridgeRef.requestExpand)
                root.bridgeRef.requestExpand(!root._expanded)
        }
    }

    // ══════════════════════════════════════════════════════════
    // 鲸鱼娘（2026-10-11 · dwh 移植）—— 右下角，所有形态都在
    // ══════════════════════════════════════════════════════════
    WhaleMascot {
        id: whale
        theme: root.th
        // 空闲态鲸鱼正常大小（28px），底层扩大了能放下
        px: root._idle ? 28 : (root._expanded ? 36 : 28)
        bridgeRef: root.bridgeRef
        showWhale: true
        anchors.right: parent.right
        anchors.rightMargin: root._idle ? 6 : 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root._idle ? 2 : 4
        opacity: root._idle ? 0.7 : 1.0
        z: 100

        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on px       { NumberAnimation { duration: root.th.durShape; easing.type: Easing.OutBack } }
    }

    // ── 底 ──────────────────────────────────────────────────
    Rectangle {
        id: shell
        anchors.fill: parent
        radius: root._idle ? height / 2 : (root._expanded ? 18 : height / 2)
        color: root._idle ? root.th.idleBg : root.th.bg
        border.width: root._idle ? 0 : 1
        border.color: root.th.light ? "#E2E2E2" : "#333333"

        Behavior on radius { NumberAnimation { duration: root.th.durShape } }
        Behavior on color  { ColorAnimation  { duration: 200 } }

        // 严重度：只做顶部细条。
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: root._severity === "" ? 0 : 3
            visible: root._severity !== ""
            color: root.th.severityColor(root._severity)
        }
    }

    // ══════════════════════════════════════════════════════════
    // 空闲态（对齐 island.cpp 644-689）
    // ══════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: root._idle
        opacity: root._idle ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }

            IslandClock {
            id: idleClock
            theme: root.th
            timePx: root.th.fsBody
            // 岛宽 130，鲸鱼占右 28px+6margin=34px，时钟右留 38px
            anchors.right: parent.right
            anchors.rightMargin: 38
            anchors.verticalCenter: parent.verticalCenter
        }

        // 左半区：横杠 + 下三角
        Item {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width / 2
            height: parent.height

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -3
                width: 24; height: 4; radius: 2
                color: root.th.idleFg
            }

            Canvas {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 5
                width: 10; height: 6
                onPaint: {
                    var c = getContext("2d")
                    c.reset(); c.beginPath()
                    c.moveTo(0, 0); c.lineTo(width, 0); c.lineTo(width / 2, height)
                    c.closePath()
                    c.fillStyle = root.th.idleFg
                    c.fill()
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    // 活动态 收起 / 展开
    // ══════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: !root._idle
        opacity: root._idle ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 160 } }

        // ── 收起态 ───────────────────────────────────────
        Item {
            id: collapsedRow
            visible: !root._expanded
            anchors.fill: parent

            SvgIcon {
                id: csIcon
                name: bridgeRef ? bridgeRef.icon : ""
                tint: root.th.fg
                px: 22
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: csTitle
                anchors.left: csIcon.right
                anchors.leftMargin: 12
                anchors.right: parent.right
                anchors.rightMargin: 42
                anchors.verticalCenter: parent.verticalCenter
                text: bridgeRef ? bridgeRef.title : ""
                color: root.th.fg
                font.pixelSize: root.th.fsTitle
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Text {
                visible: text !== ""
                anchors.left: csTitle.left
                anchors.right: parent.right
                anchors.rightMargin: 42
                anchors.top: csTitle.bottom
                anchors.topMargin: 1
                text: bridgeRef ? bridgeRef.desc : ""
                color: root.th.sub
                font.pixelSize: root.th.fsNote
                elide: Text.ElideRight
            }
        }

        // ── 展开态 ───────────────────────────────────────
        Item {
            id: expandedCol
            visible: root._expanded
            anchors.fill: parent
            anchors.margins: root.padH

            Text {
                id: exTitle
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: 48
                anchors.top: parent.top
                anchors.topMargin: 2
                text: bridgeRef ? bridgeRef.title : ""
                color: root.th.fg
                font.pixelSize: root.exTitlePx
                font.weight: Font.Bold
                minimumPixelSize: root.th.fsTitleXs
                fontSizeMode: Text.Fit
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            // 描述：Markdown 分行（**粗** / # 标题 / - 列表 / \n）
            // 用 RichText 直接吃桥预渲染的 descHtml；多行、超宽自动回退省略。
            Text {
                id: exDesc
                anchors.left: exTitle.left
                anchors.right: exTitle.right
                anchors.top: exTitle.bottom
                anchors.topMargin: 6
                visible: !root._ftActive   // #198：传输中隐藏 stale 的 "0%" 描述，改由进度条呈现
                text: bridgeRef ? bridgeRef.descHtml : ""
                textFormat: Text.RichText
                color: root.th.sub
                // 2026-10-12 用户反馈「字数太少太小」：正文 13→15px，
                // 行数上限 5→10（原来长通知第 6 行起就被 … 吃掉）。
                font.pixelSize: root.th.fsBody + 2
                font.family: "Microsoft YaHei UI, PingFang SC, sans-serif"
                wrapMode: Text.WordWrap
                maximumLineCount: 10
                elide: Text.ElideRight
                lineHeight: 1.32
            }

            StateList {
                id: stList
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: exDesc.bottom
                anchors.topMargin: 8
                theme: root.th
                items: root._states
            }

            // ── #198 文件接收进度条（展开态，传输中可见）──────
            // 标题已是"正在接收 <name>"（C++ 设的），这里只补实时进度。
            Item {
                id: ftRow
                visible: root._ftActive
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: stList.bottom
                anchors.topMargin: root._ftActive ? 12 : 0
                height: visible ? 40 : 0

                // 进度槽
                Rectangle {
                    id: ftTrack
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 8
                    radius: 4
                    color: root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.14)
                    clip: true

                    // 已收部分（宽度随 _ftPct 实时变化）
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: Math.max(0, parent.width * root._ftPct)
                        radius: 4
                        color: root.th.accent
                        Behavior on width { NumberAnimation { duration: 120 } }
                    }
                }

                // 左：实时百分比 + 已收/总体积
                Text {
                    anchors.left: parent.left
                    anchors.top: ftTrack.bottom
                    anchors.topMargin: 5
                    text: (root._ftTotal > 0
                           ? Math.round(root._ftPct * 100) + "% · "
                             + root.fxHumanBytes(root._ftBytes) + " / " + root.fxHumanBytes(root._ftTotal)
                           : root.fxHumanBytes(root._ftBytes) + " · 大小未知")
                    color: root.th.sub
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                // 右：状态标
                Text {
                    anchors.right: parent.right
                    anchors.top: ftTrack.bottom
                    anchors.topMargin: 5
                    text: "接收中"
                    color: root.th.accent
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    // 动作按钮（独立于展开/收起态，活动态显示）
    // ══════════════════════════════════════════════════════════
    // 空闲态不显示（_showActionsNow = _actions.length > 0 && !_idle）。
    // C++ 侧有 actions 时会自动把形态升到 Centered/Capsule（够高放按钮）。
    // 2026-10-11 修 bug「确认按钮点不到」：
    //   原来 ActionButton 在 expandedCol 里（只有展开态可见），
    //   且被根 MouseArea 吞掉点击。现在：
    //   1) 移到根级（独立于展开/收走态）
    //   2) z: 50（高于底壳和内容，低于鲸鱼 z:100）
    //   3) ActionButton 自带 MouseArea，自己吃事件
    Row {
        id: actionRow
        anchors.left: parent.left
        anchors.leftMargin: root.padH
        anchors.right: parent.right
        anchors.rightMargin: root.padH + 40   // 给鲸鱼让位
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        spacing: 8
        visible: root._showActionsNow
        height: visible ? 32 : 0
        z: 50    // 高于内容、低于鲸鱼

        Repeater {
            model: root._actions
            delegate: ActionButton {
                theme: root.th
                label: modelData && modelData.label ? modelData.label : String(modelData)
                role: modelData && modelData.role ? modelData.role
                                                  : (index === 0 ? "primary" : "secondary")
                onClicked: {
                    if (root.bridgeRef && root.bridgeRef.fireAction)
                        root.bridgeRef.fireAction(label)
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════
    // 历史通知面板（展开态下滑出，独立容器 + 视觉隔离）
    // ══════════════════════════════════════════════════════════
    // 2026-10-11 修观感问题：
    //   用户反馈"历史通知没有专门隔离"——原来只是裸 ListView 浮在同一背景上，
    //   跟当前通知没有任何分界。现在做**独立容器**：
    //   · 独立卡片底色（比 shell 背景略深/浅，有区分度）
    //   · 标题头"历史通知" + 条数 + 清空按钮（按钮形态，不是裸文字）
    //   · 分割线把标题区与列表区隔开
    //   · 每条历史有独立行底色（hover 加深）
    //   · 面板从标题/描述区**下方**开始，不覆盖标题
    Item {
        id: historyPanel
        visible: root._expanded && root._history.length > 0 && !root._showActionsNow
        anchors.left: parent.left
        anchors.leftMargin: root.padH
        anchors.right: parent.right
        anchors.rightMargin: root.padH
        anchors.top: parent.top
        // 2026-10-12 修「历史显示不全」：原写法 headH + 36，但 implicitHeight 预算里
        // 顶部只算了 padH ⇒ 那 36 属于"预算外占位"，把历史面板整体往下顶、越顶越矮。
        // 现在与预算同一套公式：从内容区（标题+描述+状态列表+进度条）之下开始。
        anchors.topMargin: root.padH + root.headH + root.listH + root.ftH
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.padH
        z: 40

        // 独立容器：比 shell 略深/浅一档，有圆角
        Rectangle {
            id: histContainer
            anchors.fill: parent
            radius: 10
            clip: true
            color: root.th.light ? Qt.rgba(0, 0, 0, 0.05) : Qt.rgba(1, 1, 1, 0.06)
            border.width: 1
            border.color: root.th.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.10)

            // ── 标题头 ──────────────────────────────
            Item {
                id: histHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 32

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "历史通知"
                    color: root.th.fg
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 68
                    anchors.verticalCenter: parent.verticalCenter
                    text: "(" + root._history.length + ")"
                    color: root.th.sub
                    font.pixelSize: 11
                }

                // 清空按钮（按钮形态）
                Rectangle {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: clearTxt.implicitWidth + 16
                    height: 20
                    radius: height / 2
                    color: clearMa.pressed
                        ? (root.th.light ? Qt.rgba(0, 0, 0, 0.10) : Qt.rgba(1, 1, 1, 0.15))
                        : (clearMa.containsMouse
                           ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                           : Qt.rgba(0, 0, 0, 0))

                    Text {
                        id: clearTxt
                        anchors.centerIn: parent
                        text: "清空"
                        color: root.th.sub
                        font.pixelSize: 10
                    }

                    MouseArea {
                        id: clearMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.bridgeRef && root.bridgeRef.clearHistory)
                                root.bridgeRef.clearHistory()
                        }
                    }
                }

                // 分割线
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: root.th.light ? Qt.rgba(0, 0, 0, 0.08) : Qt.rgba(1, 1, 1, 0.10)
                }
            }

            // ── 列表区 ──────────────────────────────
            ListView {
                id: historyList
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: histHeader.bottom
                anchors.bottom: parent.bottom
                anchors.margins: 2
                clip: true
                model: root._history
                spacing: 2

                delegate: Item {
                    width: historyList.width
                    // 2026-10-12：行高 40→48（配描述 2 行 + 字号放大），与 histH 的 48/条一致
                    height: 48

                    // 每条独立行底色
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: 6
                        color: maHov.containsMouse
                            ? (root.th.light ? Qt.rgba(0, 0, 0, 0.06) : Qt.rgba(1, 1, 1, 0.10))
                            : (root.th.light ? Qt.rgba(0, 0, 0, 0.02) : Qt.rgba(1, 1, 1, 0.04))
                        Behavior on color { ColorAnimation { duration: 80 } }
                    }

                    SvgIcon {
                        id: hIcon
                        name: modelData.icon || ""
                        tint: root.th.sub
                        px: 15
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        id: hTitle
                        anchors.left: hIcon.right
                        anchors.leftMargin: 6
                        anchors.right: hTime.left
                        anchors.rightMargin: 6
                        anchors.top: parent.top
                        anchors.topMargin: 5
                        text: modelData.title || ""
                        color: root.th.fg
                        // 2026-10-12「字太小」：标题 12→13
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    Text {
                        anchors.left: hTitle.left
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.top: hTitle.bottom
                        text: modelData.desc || ""
                        color: root.th.sub
                        // 2026-10-12「字数太少」：描述 10→12，并允许 2 行（原来一行就 … 截掉）
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    Text {
                        id: hTime
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.top: parent.top
                        anchors.topMargin: 5
                        text: modelData.timeStr || ""
                        color: root.th.sub
                        // 2026-10-12：时间 9→11
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: maHov
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.bridgeRef && root.bridgeRef.showHistoryItem)
                                root.bridgeRef.showHistoryItem(index)
                        }
                    }
                }
            }
        }
    }
}
