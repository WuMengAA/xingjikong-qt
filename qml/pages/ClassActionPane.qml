// 班级工作台 · 右栏上「动作面板」（#461-#472）
//
// 两条动作链，刻意分开（与集控页同一哲学：单机选中管"看哪台"，
// 多选 picked 管"给哪些台发"，并成一条会互相踢）：
//   ① 全班：把**本班在线设备**塞进宿主 picked，再走 pick 系列批量 id
//      （pick-lock / pick-shutdown）—— 权限、能力、危险确认全在宿主的
//      runActionById 里，本面板不复制任何一份校验逻辑；
//   ② 单台：点格子时宿主已 pickDevice(uid)（currentUid/picked 双写），
//      这里直接 runActionById("screenshot"/"lock"/"reboot")，
//      danger 分支在宿主持 backend.currentUid 快照下发，不会发错机器。
//
// 本班在线名单从 backend.devices 按 classCode 现算（零 C++、零新协议）——
// 班级归属只认云端 knownDevices 表口径的 classCode 字段（与 ClassService
// 的班级域事件同一权威），不信任任何本地缓存名单。
//
// 反馈走注入的 toastFn（宿主的页级 toast）：本面板被探针实例化时没有
// 宿主，toastFn 为空则静默 —— 探针不崩、真机有反馈。
import QtQuick
import "../components/md3"     // Md3Btn
import "../island"             // SvgIcon

Item {
    id: pane

    property var md3: ({})
    property var host: null     // ctx.window（宿主：runActionById / picked / goPage）
    property var cs: null       // ClassService（订阅态 + 班码）
    property var devices: []    // backend.devices
    property string selectedUid: ""
    property string selectedName: ""
    property var toastFn: null  // (msg, isErr) 由外壳注入

    readonly property bool subbed: cs && cs.subscribed === true
    readonly property string code: (cs && cs.classCode) ? cs.classCode : ""

    // 高度必须由内容给（在 Column 里当子项用时没有外部 height —— 漏了这句
    // 面板会被压成 0 高，看起来像"动作区没画出来"，#472 预览实证）。
    implicitHeight: bodyCol.implicitHeight

    function toast(msg, isErr) { if (pane.toastFn) pane.toastFn(msg, isErr === true) }

    /** 本班在线设备 uid 列表（backend.devices 按 classCode 过滤；云端名单口径）。 */
    function classOnlineUids() {
        const cc = pane.code
        if (cc === "") return []
        const ds = (pane.devices && pane.devices.length) ? pane.devices : []
        var out = []
        for (var i = 0; i < ds.length; ++i) {
            const d = ds[i]
            if (!d) continue
            if ((d.classCode || "") !== cc) continue
            if (d.online !== false) out.push(d.uid)
        }
        return out
    }

    /** 全班批量：先塞 picked 再走唯一执行入口（顺序不能反，runOnPicked 现读）。 */
    function runBulk(actionId) {
        if (!pane.host) return
        const u = pane.classOnlineUids()
        if (u.length === 0) { pane.toast("当前班没有在线设备，无法批量下发", true); return }
        pane.host.picked = u
        pane.host.runActionById(actionId)
    }

    Column {
        id: bodyCol
        width: parent.width        // 自然高度（不要 anchors.fill —— 会和 implicitHeight 成环）
        spacing: 16

        // ── 全班动作 ──
        Column {
            width: parent.width
            spacing: 8

            Text {
                text: "全班动作"
                font.pixelSize: md3.body13
                font.weight: Font.Medium
                color: md3.cOnSurfaceVar
            }

            Row {
                width: parent.width
                spacing: 8

                Md3Btn {
                    md3: pane.md3
                    text: "全班锁定桌面"
                    variant: "error"
                    enabled: pane.subbed
                    onClicked: pane.runBulk("pick-lock")
                }
                Md3Btn {
                    md3: pane.md3
                    text: "全班远程关机"
                    variant: "error"
                    enabled: pane.subbed
                    onClicked: pane.runBulk("pick-shutdown")
                }
            }

            Text {
                width: parent.width
                text: "范围：本班 " + pane.classOnlineUids().length + " 台在线设备"
                font.pixelSize: md3.bodyS
                color: md3.cOutline
            }
        }

        // ── 单台动作 ──
        Column {
            width: parent.width
            spacing: 8

            Text {
                text: "单台动作"
                font.pixelSize: md3.body13
                font.weight: Font.Medium
                color: md3.cOnSurfaceVar
            }

            // 未选中：说明怎么用（不画灰按钮组 —— 一排禁点按钮不如一句人话）
            Text {
                width: parent.width
                visible: pane.selectedUid === ""
                text: "点选中格里的一台设备，即可对它单独下发动作。"
                font.pixelSize: md3.bodyS
                color: md3.cOutline
                wrapMode: Text.WordWrap
            }

            // 已选中：目标行 + 三动作 + 集控页出口
            Column {
                width: parent.width
                spacing: 8
                visible: pane.selectedUid !== ""

                Row {
                    width: parent.width
                    spacing: 8

                    SvgIcon {
                        name: "circle-check"
                        px: 16
                        tint: md3.cOk
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: "已选中：" + (pane.selectedName !== "" ? pane.selectedName : pane.selectedUid)
                        font.pixelSize: md3.body13
                        color: md3.cOnSurface
                        elide: Text.ElideRight
                        width: parent.width - 24
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Row {
                    width: parent.width
                    spacing: 8

                    Md3Btn {
                        md3: pane.md3
                        text: "屏幕截图"
                        variant: "tonal"
                        onClicked: if (pane.host) pane.host.runActionById("screenshot")
                    }
                    Md3Btn {
                        md3: pane.md3
                        text: "锁定桌面"
                        variant: "error"
                        onClicked: if (pane.host) pane.host.runActionById("lock")
                    }
                    Md3Btn {
                        md3: pane.md3
                        text: "远程重启"
                        variant: "error"
                        onClicked: if (pane.host) pane.host.runActionById("reboot")
                    }
                }

                Md3Btn {
                    md3: pane.md3
                    text: "在集控页打开"
                    variant: "outlined"
                    onClicked: if (pane.host) pane.host.goPage(1)
                }

                Text {
                    width: parent.width
                    text: "鼠标键盘操作请到集控页：那里的坐标换算踩过三次坑才稳定。"
                    font.pixelSize: md3.bodyS
                    color: md3.cOutline
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
