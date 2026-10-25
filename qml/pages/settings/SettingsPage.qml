// 系统设置（2026-10-25 重构：左侧分类列表 + 右侧内容）
//
// 为什么重做：
//   ① 原来是一张 2 列卡片墙（598 行、台账里登记为历史超限），再加一行就判红；
//   ② 用户反馈"设置界面不美观，需要列表和分类" —— 卡片墙没有分类层级，
//      找"开机自启"要在一堆卡里扫；
//   ③ 账户/提醒/外观/启动已迁到「个人中心」（pages/account/），
//      这里只留**运维级**四类：连接 / 课表 / 更新 / 关于与诊断。
//
// 权限：本页仍只对管理员开放（system:settings）。老师点头像进的是「个人中心」。
import QtQuick
import QtQuick.Layouts

import "../../components"
import "../../island"

Item {
    id: page

    property var ctx

    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null

    /** 分类表：key 决定内容页，icon 必须是 resources/icons 里实存的（calendar 不存在，用 clock）。 */
    readonly property var cats: [
        { key: "conn",  icon: "wifi",     label: "连接" },
        { key: "sched", icon: "clock",    label: "课表" },
        { key: "upd",   icon: "download", label: "更新" },
        { key: "about", icon: "info",     label: "关于与诊断" }
    ]

    property string cat: "conn"

    function srcOf(k) {
        if (k === "sched") return "SecSchedule.qml"
        if (k === "upd") return "SecUpdate.qml"
        if (k === "about") return "SecAbout.qml"
        return "SecConnection.qml"
    }

    Rectangle { anchors.fill: parent; color: th ? th.body : "#FFFBFE" }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 16

        // ── 左：分类列表（导航真源就是 page.cats，不另写一份）──
        Rectangle {
            Layout.preferredWidth: Math.min(196, Math.max(124, page.width * 0.24))
            Layout.fillHeight: true
            radius: 12
            color: page.th ? page.th.cContLow : "#F7F2FA"
            border.color: page.th ? page.th.cOutlineVar : "#CAC4D0"
            border.width: 1

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Repeater {
                    model: page.cats
                    delegate: Rectangle {
                        width: parent.width
                        height: 44
                        radius: 10
                        color: (page.cat === modelData.key)
                               ? (page.th ? page.th.cSecondaryCont : "#E8DEF8")
                               : "transparent"

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10

                            SvgIcon {
                                name: modelData.icon
                                px: 18
                                anchors.verticalCenter: parent.verticalCenter
                                tint: (page.cat === modelData.key)
                                      ? (page.th ? page.th.cOnSecondaryCont : "#1D192B")
                                      : (page.th ? page.th.cOnSurfaceVar : "#49454F")
                            }
                            Text {
                                width: parent.width - 28
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                elide: Text.ElideRight
                                font.pixelSize: 13
                                font.weight: (page.cat === modelData.key) ? Font.Medium : Font.Normal
                                color: (page.cat === modelData.key)
                                       ? (page.th ? page.th.cOnSecondaryCont : "#1D192B")
                                       : (page.th ? page.th.cOnSurface : "#1C1B1F")
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.cat = modelData.key
                        }
                    }
                }
            }
        }

        // ── 右：内容（单卡，可滚动）──
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: secLoader.item ? secLoader.item.implicitHeight : 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Loader {
                id: secLoader
                // 限宽：超宽屏下让卡片铺满整行会显得空荡（用户反馈"不美观"的一条），
                // 820 是「说明 + 按钮行」都能一行放下的宽度，再宽就只剩空白。
                width: Math.min(820, parent.width)
                source: page.srcOf(page.cat)
                onLoaded: {
                    if (!item) return
                    item.md3 = page.th
                    item.ctx = page.ctx
                }
            }
        }
    }
}
