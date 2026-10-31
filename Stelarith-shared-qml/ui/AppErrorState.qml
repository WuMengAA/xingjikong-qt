// 星集控 · 错误态块（AppErrorState）—— 2026-10-31
//
// ── 为什么需要它 ────────────────────────────────────────────────────────
// 空态（AppEmptyState）回答「本来就没东西」，可用它回答「本来该有，但没拿到」。
// 两者长得像，语义完全不同：把拉取失败画成空态，等于告诉现场"这里确实没有数据"，
// 用户就不会去重试 —— 故障被伪装成正常状态，是比报错更严重的失败。
//
// 设计稿里定的错误态三件事（docs/星集控-被控端界面设计稿-2026-10-31.html）：
//   **说原因、说影响、给动作**。只说「失败了」等于没说 ——
//   用户既不知道能不能继续用，也不知道该不该找人。
//   所以 reason（为什么）与 impact（那我还能干什么）是两个**独立必填维度**，
//   合并成一段话会让其中一项被自然省略。
//
// ── 规范对齐 ────────────────────────────────────────────────────────────
// · 无边框：靠 destructive 底色的低透明度铺出来的块面区分（不写 border.*）
// · 图标 32×32 圆角 9、标题 13 Bold、正文 12 textSecondary（8 的倍数间距）
// · 颜色只用 Theme.color.*，本文件无硬编码 Hex
// · 重试按钮用 secondary（与 AppEmptyState 的 solid 相反）：
//   错误态本身就是"抢注意力"的状态，动作按钮再 solid 会和红块抢焦点
//
// ── 与 AppEmptyState 的取舍 ────────────────────────────────────────────
//   AppEmptyState  → 正常但空（还没采集 / 还没排课）→ 中性留白
//   AppErrorState  → 异常且空（请求失败 / 权限不足）→ 红色块面 + 重试
// 判断口诀：**用户该不该为这个"空"做点什么？** 该 → ErrorState，不该 → EmptyState。
//
// ── 用法 ────────────────────────────────────────────────────────────────
//   AppErrorState {
//       title:  "无法获取插件列表"
//       reason: "云端没有响应（超时 8 秒）。"
//       impact: "本机已安装的插件仍可正常使用，只是看不到新插件。"
//       retryText: "重试"
//       onRetried: agentUi.refreshPlugins()
//   }
// retryText 留空 = 不出按钮（例如"授权失效，请找管理员重新绑定"这种用户自己重试没用的场景）。

import QtQuick
import QtQuick.Layouts
import "theme"

Rectangle {
    id: root

    /** 一句话结论（必填）。说"什么失败了"，不要只写"错误"。 */
    property string title: ""
    /** 原因（必填）：为什么失败。可写具体判据（超时 8 秒 / 401 / 文件不存在）。 */
    property string reason: ""
    /** 影响（必填）：失败之后用户还能做什么。留空等于没说，门禁不管但 review 必须打回。 */
    property string impact: ""
    /** 重试按钮文案。空串 = 不出按钮（用户自己重试无效的场景）。 */
    property string retryText: ""

    signal retried()

    /**
     * 原因 + 影响拼成同一块正文（见下方 Text 处的踩坑说明）。
     * 任一段为空都不留空行 —— 空串拼出来的 `\n` 会在标题下留一道莫名空隙。
     */
    readonly property string _body: {
        if (reason !== "" && impact !== "") return reason + "\n" + impact
        return reason !== "" ? reason : impact
    }

    /**
     * 外层红块的底：destructive 叠很低的透明度。
     * 不用固定 Hex —— 令牌换色（浅色模式 destructive 是 #EF4444）时这里自动跟随。
     */
    readonly property color _tint: Qt.rgba(Theme.color.destructive.r,
                                           Theme.color.destructive.g,
                                           Theme.color.destructive.b, 0.07)

    radius: Theme.radius.md
    color: _tint
    implicitHeight: body.implicitHeight + Theme.spacing.md * 2

    RowLayout {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Theme.spacing.md
        spacing: Theme.spacing.md

        // ── 图标块：和标题同高，避免"大图标配小标题" ──────────────────
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            radius: 9
            color: Qt.rgba(Theme.color.destructive.r,
                           Theme.color.destructive.g,
                           Theme.color.destructive.b, 0.16)

            Text {
                anchors.centerIn: parent
                text: "!"
                font.pixelSize: 15
                font.weight: Font.Bold
                color: Theme.color.destructive
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: root.title
                font.pixelSize: 13
                font.weight: Font.Bold
                color: Theme.color.textPrimary
                wrapMode: Text.Wrap
            }

            /**
             * 原因与影响：**同一块正文里的两行**，不是两个独立段落。
             *
             * 踩过的坑（渲染探针实测）：把它们写成两个 Text + ColumnLayout.spacing 2 时，
             * 标题→原因 的空隙是 10px，而 原因→影响 是 22px —— 结果"影响"看起来像
             * 另一条独立信息，而不是对同一次失败的补充说明。
             *
             * 改用单个 Text + "\n" 拼接后，实测空隙从 22px 降到 19px，两行读起来是一段话。
             * ⚠️ 诚实记录：19px 仍大于 10px，未做到"与标题间距一致"。
             * 19 物理px ÷ 1.5 DPI ≈ 12.7 逻辑px，对 12px 正文约 1.06 倍行距 ——
             * 作为**行内**行距是合理的（行内本就该比段间距小一号观感、但比字大），
             * 只是"原因/影响"在语义上其实是两个维度，用行内间距读起来仍稍紧。
             * 若要更像两段，应给这两个字段各自独立 Text 并显式控制，代价是 Code 更长；
             * 当前按"少一层嵌套"取舍，先这样。
             */
            Text {
                Layout.fillWidth: true
                text: root._body
                visible: root._body !== ""
                font.pixelSize: 12
                lineHeight: 1.5
                color: Theme.color.textSecondary
                wrapMode: Text.Wrap
            }
        }

        AppButton {
            Layout.alignment: Qt.AlignVCenter
            text: root.retryText
            variant: "secondary"
            size: "sm"
            visible: root.retryText !== ""
            onClicked: root.retried()
        }
    }
}
