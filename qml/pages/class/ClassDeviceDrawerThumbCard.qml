// 班级设备面板 · 缩略图卡片（进远程桌面）· QtShadcn 规范（2026-10-29 拆分 + 迁移）
//
// ⚠️ B0 铁律：纯声明式、零逻辑（禁 Math.* / 三元 / JS function / console.log）。
//    hover 与按下态一律用 `State.when` 表达。
//
// ── 迁移要点 ────────────────────────────────────────────────────────────
// · **删 border**（原卡片描边 1px）—— 规范铁律
// · **hover 环不用 border 表达**：原实现是 `border.width:1 + border.color` 的独立环层，
//   规范明文禁 border.width/border.color ⇒ 改为「外扩一层低透明度实色块」做光晕，
//   视觉效果等价、不碰 border 属性
// · 硬编码 hex（#0F172A 缩略图底 / #64748B / #94A3B8）→ Theme 令牌
import QtQuick
import QtQuick.Layouts
import "../../components/theme"       // Theme 令牌
import "../../island"                 // SvgIcon

Item {
    id: thumb

    signal activated()

    implicitWidth: 160 + Theme.spacing.md + 150
    implicitHeight: 90

    // hover 光晕（外扩一层低透明实色，不用 border）
    Rectangle {
        id: halo
        anchors.fill: card
        anchors.margins: -3
        radius: card.radius + 3
        color: Theme.color.success
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
    }

    Rectangle {
        id: card
        width: 160
        height: 90
        radius: Theme.radius.md
        color: Theme.color.surfaceVariant
        clip: true

        scale: 1.0
        Behavior on scale {
            NumberAnimation { duration: Theme.motion.normal; easing.type: Easing.OutBack }
        }

        states: [
            State {
                name: "pressed"
                when: ma.pressed
                PropertyChanges { target: card; scale: Theme.motion.pressScale }
                PropertyChanges { target: halo; opacity: 1 }
            },
            State {
                name: "hovered"
                when: ma.containsMouse && !ma.pressed
                PropertyChanges { target: card; scale: Theme.motion.hoverScale }
                PropertyChanges { target: halo; opacity: 1 }
            }
        ]

        // 真图：image://classthumb/<uid>?t<rev>（源由 C++ 拼；无图时为空串）
        Image {
            anchors.fill: parent
            source: classCenter.thumbSource
            visible: classCenter.thumbAvailable
            fillMode: Image.PreserveAspectCrop
            cache: false
        }

        // 空态：说清「还没有画面」，不是黑框
        Column {
            anchors.centerIn: parent
            spacing: Theme.spacing.xs
            visible: !classCenter.thumbAvailable
            SvgIcon {
                name: "monitor"
                px: 24
                tint: Theme.color.textSecondary
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "暂无画面"
                color: Theme.color.textSecondary
                font.pixelSize: 11
            }
        }

        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: thumb.activated()
        }
    }

    Column {
        anchors.left: card.right
        anchors.leftMargin: Theme.spacing.md
        anchors.verticalCenter: card.verticalCenter
        spacing: Theme.spacing.xs

        Text {
            text: "进入远程桌面"
            color: Theme.color.textPrimary
            font.pixelSize: 14
            font.bold: true
        }
        Text {
            text: "缩略图 160×90 · 16:9"
            color: Theme.color.textSecondary
            font.pixelSize: 12
        }
        Text {
            text: "只读画面；键鼠注入在设备集控页"
            color: Theme.color.textSecondary
            font.pixelSize: 11
        }
    }
}
