// 仪表盘页（S1/S2/S3/S4/S5/S7）· #473 从 MainWindow 内联段抽出
//
// 为什么抽：MainWindow.qml 已到登记上限（710 行），仪表盘内联 270 行让任何
// 接线都会把门禁撞红。抽出来之后外壳只留骨架（导航/顶栏/状态条/挂载点）。
//
// 分工：
//   · 本文件 = 编排（滚动容器 + 两块的顺序与间距）
//   · DashDeviceSection = 统计卡 + 筛选 chip + 设备网格（S1/S5/S7）
//   · DashInsightCards  = 动作排行 + 存储概况 + 回执（S3/S2/S4）
//
// 数据全部由宿主注入（devices 已按班级过滤，stats 已算好），
// 事件原样抛回——本页不写 backend、不改宿主状态（选择路径只有一条）。
import QtQuick
import QtQuick.Layouts

Item {
    id: page

    property var md3: ({})
    property var host: null           // ctx.window
    property var devices: []          // 本班设备（未按 chip 过滤）
    property var stats: ({ total: 0, on: 0, off: 0, pending: "—" })
    property bool portrait: false
    property string filterMode: "all"
    property string openUid: ""
    property var toastFn: null

    signal filterChanged(string mode)
    signal deviceActivated(string uid)

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width          // 纪律：显式声明，否则列宽算不出来
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: col
            width: flick.width
            spacing: page.md3 ? page.md3.spM : 16

            DashDeviceSection {
                Layout.fillWidth: true
                md3: page.md3
                devices: page.devices
                stats: page.stats
                portrait: page.portrait
                filterMode: page.filterMode
                openUid: page.openUid
                onFilterChanged: function (m) { page.filterChanged(m) }
                onDeviceActivated: function (uid) { page.deviceActivated(uid) }
            }

            DashInsightCards {
                md3: page.md3
                host: page.host
                portrait: page.portrait
                toastFn: page.toastFn
            }
        }
    }
}
