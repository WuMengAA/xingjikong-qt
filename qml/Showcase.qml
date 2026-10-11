import QtQuick 2.15
import "island"

// 批次 1/2 独立渲染内容（纯 Item，不含 Window —— 便于 grabToImage 截图与嵌入任意宿主）
//
// 批次 3 接入被控端后，本文件仅作回归探针：不进 exe、不参与打包。
Item {
    id: showcase
    implicitWidth: 480
    implicitHeight: col.implicitHeight + 60

    Rectangle {
        anchors.fill: parent
        color: "#0E0E0E"
    }

    // ⚠️ 不用 Column：Column 会把子项拉到父宽，岛的自适应宽度被架空
    //    （implicitWidth 96/320/400 全失效 —— 第一版截图踩到过）
    //    改用「逐个定位 + 各自 width: implicitWidth」。
    Item {
        id: col
        anchors.centerIn: parent
        width: parent.width
        height: parent.height

        // ── 场景 1：空闲半胶囊（HH:mm + 横杠 + 箭头）──────────
        IslandRoot {
            id: isl1
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 40
            bridge: scIdle
            width: implicitWidth; height: implicitHeight
        }
        QtObject {
            id: scIdle
            property string title: ""; property string desc: ""; property string icon: ""
            property string form: "capsule"; property string severity: ""
            property bool light: false; property bool idle: true
            property bool expanded: false; property bool interactive: false
            property var states: []; property var actions: []
            function fireAction(l) {}
            function requestExpand(on) {}
        }

        // ── 场景 2：收起态（图标 + 标题 + 描述 elide）─────────
        IslandRoot {
            id: isl2
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: isl1.bottom
            anchors.topMargin: 26
            bridge: scCollapsed
            width: implicitWidth; height: implicitHeight
        }
        QtObject {
            id: scCollapsed
            property string title: "老师正在讲话"
            property string desc: "请保持安静，认真听讲"
            property string icon: "bell"
            property string form: "capsule"; property string severity: ""
            property bool light: false; property bool idle: false
            property bool expanded: false; property bool interactive: false
            property var states: []; property var actions: []
            function fireAction(l) {}
            function requestExpand(on) {}
        }

        // ── 场景 3：展开态（鲸鱼娘 + 跑马灯 + 多状态列表 + 动作）──
        IslandRoot {
            id: isl3
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: isl2.bottom
            anchors.topMargin: 26
            bridge: scExpanded
            width: implicitWidth; height: implicitHeight
        }
        QtObject {
            id: scExpanded
            property string title: "考试模式已开始"
            property string desc: "本次考试时长 45 分钟，请勿切换窗口；切屏会被记录并上报给监考老师，请自觉遵守考试纪律。"
            property string icon: "ban"
            property string form: "capsule"; property string severity: ""
            property bool light: false; property bool idle: false
            property bool expanded: true; property bool interactive: true
            property var states: [
                { k: "exam",  t: "考试模式已开始", d: "剩余 44 分钟", p: -1 },
                { k: "voice", t: "老师正在讲话",   d: "请保持安静",   p: -1 },
                { k: "file",  t: "分发试卷.pdf",   d: "已完成 100%",  p: 1.0 },
                { k: "alert", t: "2 台设备离线",   d: "已重试 3 次",  p: -1 }
            ]
            property var actions: [
                { label: "知道了",   role: "primary" },
                { label: "需要帮助", role: "secondary" }
            ]
            function fireAction(l) {}
            function requestExpand(on) {}
        }

        // ── 场景 4：浅色展开态（验证深浅两套令牌）─────────────
        IslandRoot {
            id: isl4
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: isl3.bottom
            anchors.topMargin: 26
            bridge: scLight
            width: implicitWidth; height: implicitHeight
        }
        QtObject {
            id: scLight
            property string title: "收到文件"
            property string desc: "作业模板.docx 已保存到桌面"
            property string icon: "download"
            property string form: "capsule"; property string severity: "inform"
            property bool light: true; property bool idle: false
            property bool expanded: true; property bool interactive: true
            property var states: [
                { k: "file", t: "收到文件", d: "作业模板.docx", p: 1.0 }
            ]
            property var actions: [ { label: "打开", role: "primary" } ]
            function fireAction(l) {}
            function requestExpand(on) {}
        }
    }
}
