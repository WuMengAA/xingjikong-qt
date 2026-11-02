// 班级工作台 · 发布中心表单（P1 第一刀：通知三件套 + 课后作业）
//
// 从 ClassPublishDialog 抽出来的理由：QML 组件守 200 行红线（AGENTS.md §10），
// 光「类型段 + 标题 + 正文」就占百来行；抽件后弹窗只管开合/校验/发请求/收回执。
//
// 状态归属（重要）：type / titleText / bodyText 三个真源**在本件内** —— 弹窗只在
// 发布那一刻读一次、复位时调 reset()。不两边各存一份（两份状态必然漂移）。
//
// 类型白名单与云端 class-admin.PUBLISH_TYPES 严格一致（顺序即段序）。
// 不新开共享件 —— shared-qml 不在「只改三工程」范围内，等第二处消费方再提升。
import QtQuick
import "../../components"           // AppInput / AppSegmentedButton
import "../../components/theme"   // Theme 设计令牌单例（pragma Singleton）

Item {
    id: form


    /** 类型段：icon 取已验证存在的 lucide 名（SvgIcon 由 C++ provider 供图）。 */
    readonly property var typeModel: [
        { value: "student-notice",    label: "学生通知", icon: "users" },
        { value: "class-notice",      label: "班级通知", icon: "monitor" },
        { value: "fullscreen-notice", label: "全屏通知", icon: "tv" },
        { value: "homework",          label: "课后作业", icon: "circle-check" }
    ]
    property string type: "class-notice"
    property string titleText: ""
    property string bodyText: ""

    implicitWidth: 320
    implicitHeight: col.implicitHeight

    /** 当前类型的渲染落点（人话：发了去哪看，避免"发了不知道在哪显示"）。 */
    function typeHint() {
        if (form.type === "fullscreen-notice" || form.type === "homework")
            return "被控端：全屏文字窗显示"
        return "被控端：顶部跑马灯滚动显示"
    }

    /** 复位（打开时、发布成功后）：不留上一轮残值。
     *  两个输入框都必须走显式赋值 —— 用户键入会摧毁 text 绑定（Qt 语义），
     *  只改 root 属性不再同步到可见输入框。 */
    function reset() {
        form.type = "class-notice"
        titleField.setText("")
        form.titleText = ""
        bodyEdit.text = ""
        form.bodyText = ""
    }

    Column {
        id: col
        width: parent.width
        spacing: 12

        // 类型（四选一；窄屏可横向滑动，不截断选项）
        Flickable {
            width: parent.width
            height: 32
            contentWidth: seg.implicitWidth
            clip: true
            AppSegmentedButton {
                id: seg
                // 显式给宽：Flickable 不像 Row/Column 会按 implicitWidth 布局子项，
                // 不写这行 width 就是 0（段位全部叠在左上角）。
                width: implicitWidth
                model: form.typeModel
                current: form.type
                onActivated: function (v) { form.type = v }
            }
        }
        Text {
            width: parent.width
            text: form.typeHint()
            font.pixelSize: 12
            color: Theme.color.textSecondary || "#6B7280"
        }

        // 标题（单行）
        AppInput {
            id: titleField
            width: parent.width
            placeholder: "标题（可留空）"
            text: form.titleText
            onTextChanged: form.titleText = text
        }

        Rectangle {
            width: parent.width
            height: 96
            radius: Theme.radius.sm
            color: Theme.color.surfaceVariant
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 2
                color: bodyEdit.activeFocus ? Theme.color.primary : Theme.color.textSecondary
            }
            TextEdit {
                id: bodyEdit
                anchors.fill: parent
                anchors.margins: 12
                font.pixelSize: 14
                color: Theme.color.textPrimary
                wrapMode: TextEdit.Wrap
                text: form.bodyText
                onTextChanged: form.bodyText = text
            }
            Text {
                visible: form.bodyText === ""
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 12
                anchors.topMargin: 12
                text: "正文（可留空，支持换行）"
                font.pixelSize: 14
                color: Theme.color.textSecondary
            }
        }
    }
}
