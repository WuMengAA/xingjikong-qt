// 首次启动引导 · 绑定码输入框（2026-10-20）
// 为什么不用共享 Md3TextField：那是 68 高的表单行，字号 14；本页要求「正中间一个**大尺寸**
// 输入框」，字号 32、字符间距拉开。
// 收两种码（2026-10-20 班级真源统一）：
//   ① 6 位字母数字（云端按班开的一次性码）
//   ② `XJK-XXXX-XXX`（站点后台的班级接入码，云端代为兑换）
// ⇒ 上限放宽到 13 位、允许连字符；格式合法性由 C++ 的 isCodeFormatValid 单点判定。
// ⚠️ 不用 Qt TextField（项目纪律）；TextInput 是 QtQuick 核心项，Md3TextField 内部也用它。
import QtQuick

Item {
    id: root
    property var tk: ({})
    property string text: input.text
    property bool errorMode: false
    signal accepted()

    implicitHeight: 84
    implicitWidth: 320

    function forceUpper() {
        // 保留连字符（XJK-XXXX-XXX 需要），其余非字母数字一律清掉
        var cleaned = input.text.toUpperCase().replace(/[^A-Z0-9-]/g, "");
        if (cleaned !== input.text) input.text = cleaned;
    }

    Rectangle {
        anchors.fill: parent
        radius: tk.rM
        color: tk.cContLow
        border.width: input.activeFocus || root.errorMode ? 2 : 1
        border.color: root.errorMode ? tk.cError
                                     : (input.activeFocus ? tk.cPrimary : tk.cOutlineVar)

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: TextInput.AlignHCenter
            // 两种码长度差一倍：输入长了就把字号收一档，避免顶到边框
            font.pixelSize: input.text.length > 6 ? 22 : 32
            font.letterSpacing: input.text.length > 6 ? 3 : 8
            color: tk.cOnSurface
            selectionColor: tk.cPrimaryCont
            selectedTextColor: tk.cOnSurface
            maximumLength: 13
            // 手机/触屏：只弹大写字母数字键盘
            inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            validator: RegularExpressionValidator { regularExpression: /[A-Z0-9-]{0,13}/ }
            onTextChanged: root.forceUpper()
            onAccepted: root.accepted()
            Keys.onReturnPressed: root.accepted()

            // 占位提示（TextInput 没有 placeholder，用并列 Text 画，聚焦且有字时隐藏）
            Text {
                anchors.centerIn: parent
                visible: input.text.length === 0
                text: "请输入绑定码"
                font.pixelSize: 20
                color: tk.cOutline
            }
        }
    }
}
