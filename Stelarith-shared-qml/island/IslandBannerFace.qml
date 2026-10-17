import QtQuick 2.15
import "."

// 顶部通栏横幅（2026-10-17 · #330 形态多样化 · Banner）
//
// ── 为什么要有这一档（不是"胶囊加宽"）────────────────────────────
//   胶囊的宽度由**内容**定，上限 640（IslandOverlay::geometryForContent），
//   长文案到顶就只能折行，高度跟着从 56 涨到 90+ —— 视觉上仍是"胶囊变胖"。
//   Banner 是**定宽通栏**（C++ 算死：可用宽 − 48，封顶 1280）+ **单行省略**，
//   只吃屏幕顶部一条，长句子不会把岛撑高、也不会挡中间课件区。
//
// ── 几何契约（真源在 C++，本面只排版）────────────────────────────
//   IslandOverlay::geometryForForm(Form::Banner)：宽 = min(可用宽 − 48, 1280)，
//   高 = 72（有描述）/ 60（无描述），y = 可用区顶 + 24。
//   ⚠️ 高度两档与 C++ 的 kBannerH/kBannerHFlat 必须一致；本面按"垂直居中单行"
//      排版，两档都不用改（改高度只影响留白）。
//
// ── 版式 ────────────────────────────────────────────────────────
//   图标 26 + 标题（16px Medium，最多吃剩余宽 40%）+ 描述（13px 次色，吃剩下的）
//   全部单行 elide：横幅的可读性靠"一行说完"，折行就不是横幅了。
Item {
    id: bannerFace

    // ⚠️ 显式传依赖（隐式作用域不会跟着搬进独立文件 —— D 档拆分踩过）
    property var    th: null
    property string icon: ""
    property string title: ""
    property string desc: ""
    readonly property bool hasDesc: bannerFace.desc !== ""

    // 左右留白 / 图标块宽度：**常量**。
    // ⚠️ 千万别在 Text.width 的绑定里引用它自己的 x —— x 由锚点驱动，
    //    与锚点重算互相触发，Qt 会报 "Binding loop detected for property width"
    //    （2026-10-17 真机日志实测：只在有图标+有描述的横幅上出现，每秒刷屏）。
    readonly property int padL: 20
    readonly property int padR: 20
    readonly property int gap: 12
    readonly property int iconBlock: bIcon.visible ? (bIcon.width + bannerFace.gap) : 0

    SvgIcon {
        id: bIcon
        name: bannerFace.icon
        tint: bannerFace.th ? bannerFace.th.fg : "#FAFAFA"
        px: 26
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
    }

    // 标题自然宽度度量器（不参与布局）。
    // ⚠️ 必须用它，**不能**在 width 绑定里读 `bTitle.implicitWidth`：
    //    带 `elide` 的 Text，其 implicitWidth 会跟着 width 走 ⇒
    //    width ↔ implicitWidth 互相触发，Qt 报
    //    "Binding loop detected for property width"（2026-10-17 真机实测两次，
    //    第一次误以为是 `.x` 引起；真因是 implicitWidth）。
    //    TextMetrics.advanceWidth 只由 text/font 决定，与 width 无关，天然不成环。
    TextMetrics {
        id: tmTitle
        font: bTitle.font          // 引用而不是复制字体属性，避免两处漂移
        text: bannerFace.title
    }

    Text {
        id: bTitle
        anchors.left: bIcon.visible ? bIcon.right : parent.left
        anchors.leftMargin: bIcon.visible ? bannerFace.gap : bannerFace.padL
        anchors.verticalCenter: parent.verticalCenter
        // 有描述 ⇒ 标题最多吃剩余宽的 40%（横幅的价值在描述那句话）；
        // 无描述 ⇒ 吃满剩余宽。elide 兜住长标题（不折行）。
        width: {
            const avail = Math.max(0, bannerFace.width - bannerFace.padL
                                      - bannerFace.iconBlock - bannerFace.padR)
            if (!bannerFace.hasDesc) return avail
            return Math.min(Math.max(0, tmTitle.advanceWidth), avail * 0.4)
        }
        text: bannerFace.title
        color: bannerFace.th ? bannerFace.th.fg : "#FAFAFA"
        font.pixelSize: 16
        font.weight: Font.Medium
        elide: Text.ElideRight
        maximumLineCount: 1
        visible: text !== ""
    }

    Text {
        id: bDesc
        anchors.left: bTitle.visible ? bTitle.right : (bIcon.visible ? bIcon.right : parent.left)
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        text: bannerFace.desc
        color: bannerFace.th ? bannerFace.th.sub : "#C8C8C8"
        font.pixelSize: 13
        elide: Text.ElideRight
        maximumLineCount: 1
        visible: text !== ""
    }
}
