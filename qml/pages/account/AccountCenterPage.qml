// 个人中心（2026-10-25 新建）
//
// 为什么要有这一页：
//   原来点头像 → 直接跳「系统设置」，而系统设置受 `system:settings` 门禁
//   （permission_manager.cpp 明确 teacher 无此权限）⇒ **老师点自己的头像永远被挡**，
//   连"我是谁、我教哪几个班、退出登录"都看不到。用户报的"个人中心进不去"就是这条。
//
// 职责边界（与系统设置分工）：
//   个人中心 = 我的账号 / 我的班级 / 我的偏好 —— 任何身份都能进；
//   系统设置 = 连接 / 课表 / 更新 / 诊断 —— 运维级，仍只给管理员。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import "../../components"

Item {
    id: page

    property var ctx

    readonly property var th: (ctx && ctx.window && ctx.window.th) ? ctx.window.th : null
    readonly property bool canSettings: (typeof perms !== "undefined") && perms.can("system:settings")

    /** 当前班显示名（classes 里查不到就回班码，不编造）。 */
    function currentClassName() {
        const code = backend.currentClass || ""
        if (code === "") return "全部班级"
        const cs = backend.classes || []
        for (var i = 0; i < cs.length; ++i)
            if (cs[i] && cs[i].code === code) return cs[i].name || code
        return code
    }

    Rectangle { anchors.fill: parent; color: page.th ? page.th.body : "#FFFBFE" }

    Flickable {
        anchors.fill: parent
        anchors.margins: 18
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 6 }

        Column {
            id: col
            width: parent.width
            spacing: 14

            AccountLoginCard {
                width: col.width
                ctx: page.ctx
            }

            // ── 我的班级 ──
            SettingsCard {
                width: col.width
                title: "我的班级"

                Item {
                    width: parent.width
                    height: 20
                    Text {
                        id: k1
                        text: "当前"
                        width: 70
                        color: page.th ? page.th.cOnSurfaceVar : "#49454F"
                        font.pixelSize: 13
                    }
                    Text {
                        anchors.left: k1.right
                        anchors.right: parent.right
                        text: page.currentClassName()
                        elide: Text.ElideRight
                        color: page.th ? page.th.cOnSurface : "#1C1B1F"
                        font.pixelSize: 13
                    }
                }
                Item {
                    width: parent.width
                    height: 20
                    Text {
                        id: k2
                        text: "在册"
                        width: 70
                        color: page.th ? page.th.cOnSurfaceVar : "#49454F"
                        font.pixelSize: 13
                    }
                    Text {
                        anchors.left: k2.right
                        anchors.right: parent.right
                        text: ((backend.classes ? backend.classes.length : 0)) + " 个班级"
                              + (backend.classesFiltered ? "（我带的）" : "")
                        color: page.th ? page.th.cPrimary : "#18181B"
                        font.pixelSize: 13
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: backend.classesFiltered
                          ? "已按你的成员身份过滤：只列你带的班。退出登录后可见全校班级。要加/换班找管理员。"
                          : "班级归属由云端下发（设备首次配置时扫码绑定班级）。要加/换班找管理员。"
                    color: page.th ? page.th.cOnSurfaceVar : "#49454F"
                    font.pixelSize: 12
                }
                Btn {
                    width: 148
                    theme: page.th
                    text: "去班级看板"
                    onClicked: if (page.ctx) page.ctx.window.openSection("classes")
                }
            }

            // ── 偏好（提醒 / 外观 / 启动）──
            AccountPrefsCard {
                width: col.width
                ctx: page.ctx
            }

            // ── 系统设置入口（只给管理员；老师这一卡整块不可见，不是灰按钮）──
            SettingsCard {
                width: col.width
                title: "系统"
                subtitle: "云端连接、课表、更新与诊断"
                visible: page.canSettings

                Btn {
                    width: 148
                    theme: page.th
                    text: "打开系统设置"
                    onClicked: if (page.ctx) page.ctx.window.openSection("settings")
                }
            }
        }
    }
}
