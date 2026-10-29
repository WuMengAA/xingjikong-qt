// 首次启动引导 · 绑定码输入框（2026-10-20）
// 为什么不用共享 Md3TextField：那是 68 高的表单行，字号 14；本页要求「正中间一个**大尺寸**
// 输入框」，字号 32、字符间距拉开。
// 收云端发的 6 位一次性码（2026-10-25 班级接入码收归我方后，旧站点 XJK-XXXX-XXX 已废止）。
// ⇒ 上限 6 位；格式合法性由 C++ 的 isCodeFormatValid 单点判定。
// ⚠️ 不用 Qt TextField（项目纪律）；TextInput 是 QtQuick 核心项，Md3TextField 内部也用它。
import QtQuick
import "../components/theme"   // 设计令牌单例 Theme（2026-10-30 起唯一）

Item {
    id: root
    property string text: input.text
    property bool errorMode: false
    signal accepted()

    implicitHeight: 84
    implicitWidth: 320

    function forceUpper() {
        // 只保留大写字母数字（接入码为 6 位纯字母数字）
        var cleaned = input.text.toUpperCase().replace(/[^A-Z0-9]/g, "");
        if (cleaned !== input.text) input.text = cleaned;
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.md
        // 2026-10-30 去边框：原「聚焦/错误靠 2px 描边」改为底色差 + 文字色 ——
        // 聚焦把底提亮一档（surfaceVariant → surface），错误把码文字变红。
        color: input.activeFocus ? Theme.color.surface : Theme.color.surfaceVariant

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: TextInput.AlignHCenter
            // 6 位接入码：固定大字号居中
            font.pixelSize: 32
            font.letterSpacing: 8
            color: root.errorMode ? Theme.color.destructive : Theme.color.textPrimary
            selectionColor: Theme.color.surfaceVariant
            selectedTextColor: Theme.color.textPrimary
            maximumLength: 6
            // 手机/触屏：只弹大写字母数字键盘
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            validator: RegularExpressionValidator { regularExpression: /[A-Z0-9]{0,6}/ }
            onTextChanged: root.forceUpper()
            onAccepted: root.accepted()
            Keys.onReturnPressed: root.accepted()

            // 占位提示（TextInput 没有 placeholder，用并列 Text 画，聚焦且有字时隐藏）
            Text {
                anchors.centerIn: parent
                visible: input.text.length === 0
                text: "请输入绑定码"
                font.pixelSize: 20
                color: Theme.color.textSecondary
            }
        }
    }
}
