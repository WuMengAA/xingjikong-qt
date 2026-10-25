// 班级工作台 · 右栏（2026-10-25 从 ClassDashboard 抽出）
//
// 抽件原因：ClassDashboard 撞 200 行红线（224 行），右栏四块面板占其中 60 余行，
//   抽成独立件后，改右栏只动本文件，不牵 ClassDashboard 的骨架。
//
// 右栏四块（竖排，包在 Flickable 里，可滚动）：
//   ① ClassAdminPane   —— 管"班"本身（建/改名/删）；
//   ② ClassActionPane  —— 本班设备管控：锁定/关机/重启/截图/发布中心；
//   ③ ClassToolPane    —— 对全班一键的课堂工具：音量三档/专注/噪音监测；
//   ④ ClassTimelinePane—— 班级时间线。
//
// ── 与「设备集控」页的关系（2026-10-25 用户裁决）──
//   班级页**可以**包含电脑管理相关操作：② ③ 就是，且只作用于**本班在线设备**
//   （classOnlineUids 按 classCode 过滤，老师越不了界）。
//   「设备集控」页（MainWindow 的 control section）是**管理员**视角的多班管控，
//   作用域=全部设备、使用对象=管理员。两页共用同一套动作（host.runActionById +
//   pick-* 指令），只是**作用域（本班 vs 全部）与使用者（老师 vs 管理员）不同**，
//   并非两套系统。所以班级页保留设备管控面板，不做"去集控化"。
//
// 四块自然高合计 ≈880，而内容区只有 ~700 ⇒ 不滚动会裁掉下半截
// （用户报「一堆按钮按不动」的真因，2026-10-25 实证）。
import QtQuick

Item {
    id: rail

    property var md3: ({})
    property var cs: null
    property var host: null
    property var devices: []
    property var evModel: null
    property string selectedUid: ""
    property string selectedName: ""
    property var toastFn: null
    property var openControlFn: null
    /** 班级总数（backend.classes 长度）：给「上手引导」决定第①步文案。 */
    property int classCount: 0

    signal publishRequested()
    /** 上手引导里的「新建班级」→ 宿主开 ClassCreateDialog。 */
    signal createRequested()

    readonly property bool subbed: rail.cs && rail.cs.subscribed === true

    implicitWidth: 300

    Flickable {
        id: scroll
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: col
            width: parent.width
            spacing: 16

            // 上手引导（2026-10-25 用户点单「班级创建繁琐，没有引导」）：
            // 还没选/还没建班时置顶，三步讲清怎么从零把设备接进来。
            ClassOnboardGuide {
                visible: !rail.subbed
                width: parent.width
                md3: rail.md3
                classCount: rail.classCount
                onCreateRequested: rail.createRequested()
            }

            // 班级管理（2026-10-24 方案 B）：建/改名/删。管"班"本身；
            // 成员（人员与设备去留）走中栏「成员管理」tab。两处不混。
            ClassAdminPane {
                width: parent.width
                md3: rail.md3
                cs: rail.cs
                toastFn: rail.toastFn
            }

            ClassActionPane {
                width: parent.width
                md3: rail.md3
                host: rail.host
                cs: rail.cs
                devices: rail.devices
                selectedUid: rail.selectedUid
                selectedName: rail.selectedName
                toastFn: rail.toastFn
                openControlFn: rail.openControlFn
                onPublishRequested: rail.publishRequested()
            }

            // 课堂工具（2026-10-25 教室小工具第一刀）：对全班一键的音量档位 /
            // 专注 / 噪音监测。独立面板而不是塞进 ClassActionPane —— 那个已顶
            // 200 行红线，且语义不同（那边是电源类危险动作，这边是教学常用）。
            ClassToolPane {
                width: parent.width
                md3: rail.md3
                host: rail.host
                cs: rail.cs
                devices: rail.devices
                toastFn: rail.toastFn
            }

            ClassTimelinePane {
                width: parent.width
                // 固定高（不再 parent.height - 220）：滚动容器里父高是视口高，
                // 反推会把 contentHeight 算错，且小窗口下会算出负数被 clamp 成 120。
                height: 240
                md3: rail.md3
                model: rail.evModel
            }
        }
    }
}
