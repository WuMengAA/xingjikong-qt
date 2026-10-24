// 班级工作台 · 中栏「成员管理」tab（2026-10-24 · 方案 B）
//
// 对「设备」的班级操作：移出本班 / 换到别班。与右栏 ClassAdminPane 的分工：
// 那边管"班"（建/改名/删），这边管"班的成员"（设备的去留）。
//
// 数据（全部现取，不另存）：
//   · 本班设备：deviceModel（ClassService 班级域快照，QAbstractItemModel）——
//     ⚠️ delegate 里必须 model.<role>，不许 modelData.xxx（#475 真数据才炸的坑）；
//   · 换班目标：classes（backend.classes，云端 /api/classes 名单），排除本班自己。
// 写操作走 backend.classAdmin（HTTP /api/class/bind|unbind），成功后由
// ClassAdminPane 的 Connections 统一 toast + 重拉名单——本件只管发起，不管反馈。
import QtQuick
import "../../components/md3"     // Md3Btn
import "../../island"             // SvgIcon

Item {
    id: pane

    property var md3: ({})
    property var deviceModel: null     // ClassDeviceModel（本班设备）
    property var classes: []           // backend.classes（换班目标）
    property string classCode: ""      // 当前班码（本班）
    property bool subscribed: false

    implicitWidth: 320
    implicitHeight: 200

    function admin() {
        return (typeof backend !== "undefined" && backend && backend.classAdmin) ? backend.classAdmin : null
    }

    /** 换班目标（排除本班；没订阅时为空列表，按钮行不画）。 */
    function targetClasses() {
        const out = []
        for (var i = 0; i < pane.classes.length; ++i) {
            const c = pane.classes[i]
            if (!c || c.code === pane.classCode) continue
            out.push(c)
        }
        return out
    }

    Column {
        anchors.fill: parent
        spacing: 10

        Text {
            width: parent.width
            visible: !pane.subscribed
            text: "未订阅班级：先选择一个班，才能管理它的成员。"
            font.pixelSize: md3.body13
            color: md3.cOutline
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            visible: pane.subscribed
            text: "点设备行内的「换到…」选目标班，或「移出」让它暂时不属于任何班（设备本身不受影响）。"
            font.pixelSize: md3.bodyS
            color: md3.cOutline
            wrapMode: Text.WordWrap
        }

        ListView {
            width: parent.width
            height: parent.height - 60
            clip: true
            visible: pane.subscribed
            model: pane.deviceModel
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                required property var model
                // 展开态必须在 delegate 根作用域（放 Md3Btn 里 Flow 引用不到 ——
                // QML 作用域不向内穿透子组件实例）
                property bool rowExpanded: false
                width: ListView.view.width
                height: rowCol.implicitHeight + 16
                radius: pane.md3.rS
                color: pane.md3.cSurfaceVar

                Column {
                    id: rowCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Row {
                        width: parent.width
                        spacing: 8

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: (model.online === true) ? md3.cOk : md3.cOutline
                        }
                        Text {
                            text: (model.name && model.name !== "") ? model.name : model.uid
                            font.pixelSize: md3.body13
                            color: md3.cOnSurface
                            elide: Text.ElideRight
                            width: parent.width - 16 - 150
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: model.version || ""
                            font.pixelSize: md3.bodyS
                            color: md3.cOutline
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // 动作行：换到…（展开目标班按钮）+ 移出
                    Row {
                        width: parent.width
                        spacing: 6

                        Md3Btn {
                            md3: pane.md3
                            text: "换到…"
                            variant: "text"
                            onClicked: rowExpanded = !rowExpanded
                        }
                        Md3Btn {
                            md3: pane.md3
                            text: "移出本班"
                            variant: "text"
                            onClicked: {
                                const a = pane.admin()
                                if (a) a.unbindDevice(pane.classCode, model.uid)
                            }
                        }
                    }

                    // 目标班按钮横排（点即换班，无需 dialog——班数通常个位数）
                    Flow {
                        width: parent.width
                        visible: rowExpanded
                        spacing: 6

                        Repeater {
                            model: pane.targetClasses()
                            delegate: Md3Btn {
                                required property var modelData
                                md3: pane.md3
                                text: modelData.name || modelData.code
                                variant: "outlined"
                                onClicked: {
                                    const a = pane.admin()
                                    if (a) a.bindDevice(modelData.code, model.uid)
                                }
                            }
                        }
                        Text {
                            visible: pane.targetClasses().length === 0
                            text: "没有别的班可换（先去右栏「新建班级」）"
                            font.pixelSize: md3.bodyS
                            color: md3.cOutline
                        }
                    }
                }
            }
        }
    }
}
