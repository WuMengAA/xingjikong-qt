// 星集控 · 通用按钮
//
// 全界面只有这一份按钮长相（控制页右栏那些按钮以前各自画，边框深浅三种、圆角两种）。
// 四种状态说清楚就行：
//   常态      描边
//   悬停      底色 +1 档
//   按下      底色再深一档
//   不可用    描边极淡 + 字极淡（**不反白**——置灰的意思是"别点"，不是"这是主操作"）
//
// ⚠️ 别给这个组件写死 width：控制页右栏要它等到容器宽度（wide=true 独占整行），
//    而 Loader/组件里 width 写死会让"跟着容器变"失效。调用方用 Layout/anchors 定尺寸。
import QtQuick
import QtQuick.Controls

Rectangle {
    id: btn
    property var theme: ({ })
    property alias text: label.text
    property bool strong: false      // 主操作：反白（黑白界面里唯一的强调手段）
    property bool disabled: false    // 外部强塞的禁用（用不了/没权限），比 role 门控更早生效
    property string tip: ""          // 悬停说明：置灰时尤其要给，否则灰按钮没人知道为什么灰
    property bool pointToUse: true   // 能点就给手型；不可用时给禁止符（不然鼠标还是箭头）

    signal clicked()

    height: 30
    radius: btn.theme && btn.theme.rCtrl !== undefined ? btn.theme.rCtrl : 8

    readonly property bool live: !btn.disabled && ma.enabled

    color: btn.strong ? btn.theme.inv
         : ma.pressed ? btn.theme.hover2
         : ma.containsMouse ? btn.theme.hover
         : "transparent"
    border.color: btn.strong ? btn.theme.inv
                : btn.disabled ? (btn.theme.line || "#202020")
                : btn.theme.stroke
    border.width: 1

    Text {
        id: label
        anchors.centerIn: parent
        text: btn.text
        // ⚠️ 反白态（strong）的字必须跟着"反转"：inv 是浅底，字要用 win（近黑）。
        //    以前两种态共用 fg3 —— 暗色下 fg3 落在 inv 上只有 2.95:1（改了字号/对比度后
        //    才暴露出来），亮色下反过来一样糊。
        color: btn.strong ? btn.theme.win : btn.theme.fg3
        font.pixelSize: 13
    }

    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: !btn.disabled
        hoverEnabled: true
        cursorShape: !btn.live ? (btn.pointToUse ? Qt.ForbiddenCursor : Qt.ArrowCursor)
                               : Qt.PointingHandCursor
        onClicked: if (btn.live) btn.clicked()
    }

    ToolTip {
        visible: ma.containsMouse && btn.tip !== ""
        delay: 450
        text: btn.tip
    }
}
