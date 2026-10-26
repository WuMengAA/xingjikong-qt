import QtQuick 2.15
import "."

// 灵动岛 · 组件面板宿主（2026-10-27 · G4/G5/G6，对标 ClassIsland 的「组件」）
//
// ── 它解决的是什么 ────────────────────────────────────────────────
//   岛原来只有两种内容：通知（事件）和历史（回看）。ClassIsland 那套「组件」
//   （日期/课表/倒计时）属于第三种：**看板** —— 要停下来看清、反复看，
//   而不是"来了一条、看完就走"。
//
// ── 为什么宿主单独一层（而不是在 IslandRoot 里三选一）──────────────────
//   ① IslandRoot 已经 3177 行（门禁白名单在册，不能再往里堆版式）；
//   ② 三面各有自己的数据形状与排版，混在一处必然互相牵连；
//   ③ Loader **按需加载**：没开面板时三枚面的 QML 一个都不实例化（项目铁律丙）。
//
// ── 接口约定 ────────────────────────────────────────────────────
//   face：面名，与 C++ PanelDomain::faces() 逐字一致（date / timetable / countdown）。
//        未知值 ⇒ Loader 为 null ⇒ 整面不可见（不画半块空板子）。
//   info：该面的数据（C++ 组装的 QJsonObject → QVariantMap）。
//        ⚠️ 属性名**不能叫 data** —— Item.data 是默认子元素列表，重名会把
//        子对象装进去、还悄悄改掉布局语义（QML 里踩过）。
Item {
    id: host

    property var    th: null
    property string face: ""
    property var    info: ({})

    readonly property bool known: host.face === "date"
                                  || host.face === "timetable"
                                  || host.face === "countdown"

    Loader {
        id: loader
        anchors.fill: parent
        // 三选一：用 sourceComponent 而不是 source URL —— 同目录组件不需要额外
        // 解析路径，且组件实例化时才创建（切面即销毁上一面，状态天然干净）。
        sourceComponent: host.face === "date"      ? dateFace
                       : host.face === "timetable" ? timetableFace
                       : host.face === "countdown" ? countdownFace
                       : null
    }

    // ⚠️ 显式传 th / info：隐式作用域不会跟着搬进独立文件（D 档拆分的教训）。
    Component {
        id: dateFace
        IslandDateFace { th: host.th; info: host.info }
    }
    Component {
        id: timetableFace
        IslandTimetableFace { th: host.th; info: host.info }
    }
    Component {
        id: countdownFace
        IslandCountdownFace { th: host.th; info: host.info }
    }
}
