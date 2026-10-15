// 星集控管理端 · 互动审批面板「离屏探针假桥」（2026-10-15 · 组件6 取证用）
//
// 谁在用：scripts/_interact_probe.py（离屏 QQuickView 探针）。
//
// 为什么要有它：真桥 InteractBridge 的数据面走云端 HTTP `/api/interact/*`，
//   离屏取证环境里没有登录态、也不保证有云端在跑 ⇒ 面板会停在空态，截不出
//   "有数据"的样子。这里给一份**固定演示数据**把面板渲染满。
//
// ⚠️ 属性面必须与真桥**逐一对齐**，缺一个属性，岛不会报错、只会静默显示空值
//    ——这是最难查的一类"看起来正常"。本文件的清单由双向核对而来：
//     · 岛侧：grep -o 'bridgeRef\.[A-Za-z_]*' island/IslandRoot.qml | sort -u
//     · 面板侧：grep -o 'br\.[A-Za-z_]*' qml/InteractDialog.qml | sort -u
//
// ⚠️ 本文件不是产品件：放在 scripts/ 下，运行期由探针临时拷进 build/qml/Stelarith/，
//    不写进 qml/（那会被 POST_BUILD 拷进产物、也会被 git 跟踪）。
import QtQuick

QtObject {
    id: fake

    // ══ 审批数据面（InteractDialog 读的）═════════════════════════
    property string state: "ok"
    property string error: ""
    property var songRequests: [
        { id: 1041, title: "起风了",         artist: "买辣椒也用券", classCode: "高一(3)班", uid: "S1024", state: "pending" },
        { id: 1043, title: "夜空中最亮的星", artist: "逃跑计划",     classCode: "高二(1)班", uid: "S2088", state: "pending" },
        { id: 1046, title: "晴天",           artist: "周杰伦",       classCode: "高一(3)班", uid: "S1024", state: "pending" }
    ]
    property var blessings: [
        { id: 2201, text: "祝三班的大家期中考试顺利！", classCode: "高一(3)班", uid: "S1024", state: "pending" },
        { id: 2205, text: "王老师生日快乐！",           classCode: "高二(1)班", uid: "S2088", state: "pending" }
    ]
    property var feedbackSummary: ({
        ok: true, likeTotal: 128, likeToday: 36, feedbackTotal: 9,
        byClass: [
            { classCode: "高一(3)班", n: 54 },
            { classCode: "高二(1)班", n: 41 },
            { classCode: "高三(2)班", n: 33 }
        ]
    })
    function reload() { fake.state = "ok" }
    function handleSong(id, action) { fake._consume(fake.songRequests, id, true) }
    function handleBlessing(id, action) { fake._consume(fake.blessings, id, true) }
    // 面板正筛"待审"⇒ 处理完的那条立刻从列表消失（与真桥 applyHandleResult 同口径）
    function _consume(list, id, isSong) {
        var kept = []
        for (var i = 0; i < list.length; ++i)
            if (list[i].id !== id) kept.push(list[i])
        if (isSong) fake.songRequests = kept
        else        fake.blessings = kept
    }

    // ══ 岛桥面（IslandRoot 读的）═════════════════════════════════
    property string title: ""
    property string desc: ""
    readonly property string descHtml: fake.desc
    property string icon: ""
    property string severity: ""
    property bool light: false
    readonly property bool idle: fake.title.length === 0
    property bool expanded: true
    readonly property bool interactive: false
    property var actions: []
    property var states: []
    property string whaleBubble: ""
    property bool whaleAudioOn: false
    property var history: []
    property bool fileTransferActive: false
    property real fileTransferBytes: 0
    property real fileTransferTotal: 0
    property var spectrumBands: []
    property bool spectrumActive: false
    property bool mediaActive: false
    property string mediaTitle: ""
    property real mediaElapsedMs: 0
    property real mediaTotalMs: 0
    property string mediaNextTitle: ""
    property string mediaNextCover: ""
    property bool mediaPresenting: false
    property string mediaPhase: ""
    property int blessingCooldownSec: 0

    // 岛内交互（预览场景不会被点到，最小实现即可）
    function fireAction(k) {}
    function requestExpand(on) { fake.expanded = on }
    function showHistoryItem(i) {}
    function clearHistory() {}
    function whaleClicked() {}
    function whaleLongPressed() {}
    function sendBlessing(t) { return 3 }
    function reportFeedback(k, v) {}
    function submitSongRequest(t, a, s) {}

    // ══ 岛预览（面板点条目时调；口径照抄 InteractBridge::preview）══
    property string previewKind: ""
    function preview(kind, title, sub) {
        fake.previewKind = kind
        if (kind === "song") {
            fake.title = title.length ? title : "（未命名）"
            fake.desc = ""
            fake.icon = "music"
            fake.mediaActive = true
            fake.mediaPresenting = true
            fake.mediaTitle = fake.title
            fake.mediaTotalMs = 269000          // 4:29（与真桥同口径，可复现）
            fake.mediaElapsedMs = 83000         // 1:23
            fake.mediaNextTitle = ""
            fake.mediaNextCover = ""
            fake.mediaPhase = "playing"
        } else if (kind === "blessing") {
            fake.title = title.length ? title : "（空祝福）"
            fake.desc = sub
            fake.icon = "mic"
            fake.mediaActive = false; fake.mediaPresenting = false
            fake.mediaTitle = ""; fake.mediaPhase = ""
            fake.mediaTotalMs = 0; fake.mediaElapsedMs = 0
            fake.mediaNextTitle = ""
        } else if (kind === "feedback") {
            fake.title = title.length ? title : "课堂反馈"
            fake.desc = sub
            fake.icon = "heart"
            fake.mediaActive = false; fake.mediaPresenting = false
            fake.mediaTitle = ""; fake.mediaPhase = ""
            fake.mediaTotalMs = 0; fake.mediaElapsedMs = 0
            fake.mediaNextTitle = ""
        } else {
            fake.clearPreview()
            return
        }
        fake.expanded = true        // 面板里看的就是展开态
    }
    function clearPreview() {
        fake.previewKind = ""
        fake.title = ""; fake.desc = ""; fake.icon = ""
        fake.mediaActive = false; fake.mediaPresenting = false
        fake.mediaTitle = ""; fake.mediaPhase = ""
        fake.mediaTotalMs = 0; fake.mediaElapsedMs = 0
        fake.mediaNextTitle = ""; fake.mediaNextCover = ""
    }
}
