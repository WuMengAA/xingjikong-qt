// 班级工作台 · 右栏（2026-10-25 从 ClassDashboard 抽出）
//
// 抽件原因有两条，都是硬的：
//   ① ClassDashboard 被 200 行红线判红（224 行）—— 右栏四块面板占其中 60 余行；
//   ② 右栏本身正在改性质：用户明确「班级看板不是集控界面」，
//      ClassActionPane（关机/重启/锁屏）+ ClassToolPane（静音/专注/噪音）属于
//      设备集控语义，后续会逐步换成班级事务（学生/考勤/班务）。抽成独立件后，
//      这次改造只动本文件，不牵 ClassDashboard 的骨架。
//
// 当前形态仍是"管理 + 动作 + 课堂工具 + 时间线"四块竖排，包在 Flickable 里：
//   四块自然高合计 ≈880，而内容区只有 ~700 ⇒ 不滚动就会把下半截裁到窗口外
//   （用户报「一堆按钮按不动」的真因，2026-10-25 实证）。
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

    signal publishRequested()

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
