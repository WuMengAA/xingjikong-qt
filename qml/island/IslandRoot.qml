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
//
// ── 尺寸模型（⚠️ 这里踩过坑，别改回去）───────────────────────────
//   ❌ 错法：shell 用 anchors.fill: parent + root.implicitHeight 靠内容推导
//           → 循环依赖，implicitHeight 被压到最小，岛被压扁
//   ✅ 正法：w/h 由 root 显式给出（explicitH / explicitW），shell 跟着走
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
    readonly property string _form:       bridgeRef ? bridgeRef.form        : "capsule"
    readonly property string _severity:   bridgeRef ? bridgeRef.severity    : ""
    readonly property string _title:      bridgeRef ? bridgeRef.title       : ""
    readonly property string _desc:       bridgeRef ? bridgeRef.desc        : ""
    readonly property string _icon:       bridgeRef ? bridgeRef.icon        : ""
    readonly property bool   _canOpen:    bridgeRef ? bridgeRef.canOpen     : false
    readonly property var    _states:     (bridgeRef && bridgeRef.states)  ? bridgeRef.states  : []
    readonly property var    _actions:    (bridgeRef && bridgeRef.actions) ? bridgeRef.actions : []

    // ── 尺寸（显式，不靠隐式推导）───────────────────────────
    readonly property int padH: 14
    readonly property int headH: root._expanded ? 46 : 0
    readonly property int listH: (root._expanded && stList.rows > 0) ? stList.implicitHeight + 8 : 0
    readonly property int actH:  (root._actions.length > 0) ? 34 : 0
    readonly property int collapsedH: 56

    implicitWidth: root._idle ? 96
                               : (root._expanded ? 400 : 320)
    implicitHeight: root._idle ? 36
                               : (root._expanded
                                  ? Math.max(140, root.padH + root.headH + root.listH + root.actH + root.padH)
                                  : root.collapsedH)
    // ⚠️ 不给 width/height 赋值：宿主 QQuickWidget 用 SizeRootObjectToView 时，
    //    根 Item 的 w/h 由 view 尺寸驱动 —— 但 QQuickWidget 会先取 root.implicitSize
    //    来 resize view（QQuickWidget::SizeRootObjectToView 语义即"view 跟随 root implicit"）。
    //    显式写 width: implicitWidth 反而被 view 覆盖，且与 Behavior 打架。

    Behavior on width  { NumberAnimation { duration: root.th.durShape; easing.type: Easing.OutBack } }
    Behavior on height { NumberAnimation { duration: root.th.durShape; easing.type: Easing.OutBack } }

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
        // ⚠️ 不要铺满 + alpha（会把图标文字全染掉，第一版截图踩到过）
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: root._severity === "" ? 0 : 3
            visible: root._severity !== ""
            color: root.th.severityColor(root._severity)
        }
    }

    // ════════════════════════════════════════════════════════
    // 空闲态（对齐 island.cpp 644-689）
    // ════════════════════════════════════════════════════════
    Item {
        anchors.fill: parent
        visible: root._idle
        opacity: root._idle ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }

        // 时钟：右半区右对齐 13px Medium
        IslandClock {
            id: idleClock
            theme: root.th
            timePx: 13
            anchors.right: parent.right
            anchors.rightMargin: 10
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

    // ════════════════════════════════════════════════════════
    // 活动态 收起 / 展开（对齐 island.cpp 695-806）
    // ════════════════════════════════════════════════════════
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
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                text: bridgeRef ? bridgeRef.title : ""
                color: root.th.fg
                font.pixelSize: 15
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Text {
                visible: text !== ""
                anchors.left: csTitle.left
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.top: csTitle.bottom
                anchors.topMargin: 1
                text: bridgeRef ? bridgeRef.desc : ""
                color: root.th.sub
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        // ── 展开态 ───────────────────────────────────────
        Item {
            id: expandedCol
            visible: root._expanded
            anchors.fill: parent
            anchors.margins: root.padH

            // 鲸鱼娘（清单 #1）
            WhaleMascot {
                id: whale
                theme: root.th
                px: 40
                anchors.left: parent.left
                anchors.top: parent.top
            }

            // 标题
            Text {
                id: exTitle
                anchors.left: whale.right
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: 2
                text: bridgeRef ? bridgeRef.title : ""
                color: root.th.fg
                font.pixelSize: 17
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            // 描述：跑马灯（对齐 m_marqueeX）
            MarqueeText {
                anchors.left: exTitle.left
                anchors.right: parent.right
                anchors.top: exTitle.bottom
                anchors.topMargin: 2
                height: 18
                text: bridgeRef ? bridgeRef.desc : ""
                px: 13
                theme: root.th
            }

            // 多状态列表（清单 #2）
            StateList {
                id: stList
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: root.headH
                theme: root.th
                items: root._states
                interactive: root._interactive
            }

            // 动作按钮（贴底）
            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 8
                visible: root._actions.length > 0
                height: visible ? 28 : 0

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
        }
    }

    // ── 手势（批次 1/2 只做单击；六手势属欠账，单独立项）─────
    MouseArea {
        anchors.fill: parent
        enabled: root._interactive || !root._idle
        acceptedButtons: Qt.LeftButton
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: {
            if (root.bridgeRef && root.bridgeRef.requestExpand)
                root.bridgeRef.requestExpand(!root._expanded)
        }
    }
}
