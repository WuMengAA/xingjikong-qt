// 班级工作台 · 删除班级两段确认（行内红字条）
//
// 为什么行内、不借 Md3Dialog：scrim 的 anchors.fill 会把对话框锁在父卡片内，
// 且面板自己会塌高度（2026-10-24 踩过，别再试）。两段确认 = 先点「删除班级」
// 出现本红字条，再点「确认删除」才真删 —— 不可逆操作的缓冲。
// 云端默认策略也是 refuse（班内有设备即拒），这里是双保险。
import QtQuick
import "../../components/md3"     // Md3Btn

Item {
    id: confirm

    property var md3: ({})
    /** 班名（显示名；空则回落班码，由父级算好传入）。 */
    property string targetName: ""

    signal confirmed()
    signal cancelled()

    implicitWidth: 280
    implicitHeight: body.implicitHeight + 20

    Rectangle {
        anchors.fill: parent
        radius: md3.rS
        color: md3.cErrorCont

        Column {
            id: body
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Text {
                width: parent.width
                text: "删除「" + confirm.targetName + "」？班内所有设备将被移出班级（设备本身不受影响），此操作不可撤销。"
                font.pixelSize: md3.bodyS
                color: md3.cOnErrorCont
                wrapMode: Text.WordWrap
            }
            Row {
                width: parent.width
                spacing: 8

                Md3Btn {
                    md3: confirm.md3
                    text: "确认删除"
                    variant: "error"
                    onClicked: confirm.confirmed()
                }
                Md3Btn {
                    md3: confirm.md3
                    text: "再想想"
                    variant: "text"
                    onClicked: confirm.cancelled()
                }
            }
        }
    }
}
