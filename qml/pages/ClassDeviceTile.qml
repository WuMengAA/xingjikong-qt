// 班级工作台 · 设备格子（页面级部件 · #461-#472）
//
// 定位：ClassDeviceGrid 的网格单元，视觉语言照抄 shell/DeviceCard（两层偏移投影 /
// MD3 状态层 / 选中主色薄雾 / 工业青左规 / 等宽版本号），两处观感一致不另起炉灶。
//
// 与 DeviceCard 的三处刻意差异：
//   ① 画面源走班级域 image://classthumb/<uid>?t<rev>（#460 ClassThumbProvider），
//      不是旧墙 image://frames（那是集控页轮询链路，两链不混）；
//   ② 缓存击穿用 thumbRev role（行数据变化 ⇒ source 绑定自动重算），不要手动 tick；
//   ③ 无帧占位「暂无画面」（刚上线未上报，属正常态；故障文案见 UiTerms.states）。
//
// 数据真源：ClassDeviceModel（typed）—— 本件只收标量属性，不碰 backend。
import QtQuick
import "../island"          // SvgIcon

Item {
    id: root

    property var md3: ({})
    property string uid: ""
    property string name: ""
    property bool online: false
    property string version: ""
    property int lastSeenAgoSec: -1     // 秒；-1 = 云端没给
    property bool hasThumb: false
    property int thumbRev: 0
    property bool selected: false

    signal clicked()

    implicitWidth: 144
    implicitHeight: 188

    // hover / press 状态层目标透明度（MD3 state layers，叠 onSurface 不动底色）
    property real _stateOpacity: 0

    // ── 卡片本体（先声明，阴影用 z 压在身后）──
    Rectangle {
        id: cardBg
        anchors.fill: parent
        radius: md3.rL
        color: md3.cContLowest
        border.width: root.selected ? 1.5 : 1
        border.color: root.selected ? md3.cPrimary : md3.cOutlineVar
        z: 1
    }

    // ── Level-1 柔和投影（零依赖，两层偏移矩形近似，不引 QtQuick.Effects）──
    Rectangle {
        anchors.fill: cardBg
        anchors.topMargin: 6
        radius: md3.rL
        color: Qt.rgba(0, 0, 0, 0.05)
        z: 0
    }
    Rectangle {
        anchors.fill: cardBg
        anchors.topMargin: 2
        radius: md3.rL
        color: Qt.rgba(0, 0, 0, 0.04)
        z: 0
    }

    // 选中态主色薄雾（内容之下）
    Rectangle {
        anchors.fill: cardBg
        radius: md3.rL
        color: md3.cPrimary
        opacity: root.selected ? 0.06 : 0
        Behavior on opacity { NumberAnimation { duration: md3.durShort } }
        z: 2
    }

    // MD3 状态层
    Rectangle {
        id: stateLayer
        anchors.fill: cardBg
        radius: md3.rL
        color: md3.cOnSurface
        opacity: root._stateOpacity
        Behavior on opacity { NumberAnimation { duration: md3.durShort } }
        z: 2
    }

    // ── 终末地工业青 · 在线设备「数据在流」左缘细规（与 DeviceCard 同令牌）──
    Rectangle {
        visible: root.online
        width: 3
        radius: 1.5
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.topMargin: 20
        anchors.bottomMargin: 20
        anchors.leftMargin: 5
        color: md3.cAccent
        z: 3
    }

    // 卡片内容：20px 内边距（格子比 DeviceCard 小 4px，留白仍足）
    Item {
        anchors.fill: parent
        anchors.margins: 20
        z: 4

        Column {
            anchors.fill: parent
            spacing: 8

            // 画面区：有缩略图给真图，没有给稳定底色 + 说明（不闪、不装故障）
            Rectangle {
                width: parent.width
                height: 80
                radius: md3.rS
                color: md3.cCont
                clip: true

                Image {
                    id: thumb
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    cache: false
                    visible: root.hasThumb
                    // thumbRev 即缓存击穿参数：model 行数据变化时绑定自动重算
                    source: root.hasThumb
                            ? ("image://classthumb/" + root.uid + "?t" + root.thumbRev)
                            : ""
                    asynchronous: true
                }

                // 无帧占位（离线 / 刚上线还没帧）
                Column {
                    anchors.centerIn: parent
                    visible: !root.hasThumb
                    spacing: 6
                    SvgIcon {
                        name: "monitor"
                        px: 22
                        tint: md3.cOutline
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        text: root.online ? "暂无画面" : "未开机"
                        font.pixelSize: md3.bodyS
                        color: md3.cOutline
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }

                // 在线徽标点（右下角，与卡片状态一致）
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: root.online ? md3.cOk : md3.cOutline
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                }
            }

            Text {
                width: parent.width
                text: root.name !== "" ? root.name : root.uid
                font.pixelSize: md3.body13
                font.weight: Font.Medium
                color: md3.cOnSurface
                elide: Text.ElideRight
            }

            // 状态行：在线给版本号（等宽技术字），离线给「多久没见」
            Text {
                width: parent.width
                text: {
                    if (root.online) return root.version !== "" ? root.version : "在线"
                    if (root.lastSeenAgoSec < 0) return "离线"
                    return root.lastSeenAgoSec < 60 ? "刚断线"
                           : Math.floor(root.lastSeenAgoSec / 60) + " 分前"
                }
                font.pixelSize: md3.bodyS
                font.family: (root.online && root.version !== "") ? "monospace" : ""
                color: md3.cOnSurfaceVar
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        id: tileMa
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onEntered: root._stateOpacity = md3.stHover
        onExited: root._stateOpacity = 0
        onPressed: root._stateOpacity = md3.stPress
        onReleased: root._stateOpacity = (tileMa.containsMouse ? md3.stHover : 0)
        onClicked: root.clicked()
        z: 5
    }
}
