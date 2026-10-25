// 班级工作台 · 中栏「成员管理」tab（2026-10-24 · 方案 B）
//
// 分两段，各管各的"成员"：
//   · 上半「班级人员」= **人（账号）**的成员管理 —— 2026-10-24 用户诉求
//     「人可以被添加」：把老师/助教账号挂到本班，他下次登录就能订这个班、
//     看这个班的设备。数据来自云端 class_members 表（/api/class/members），
//     角色 owner/teacher/viewer。
//   · 下半「设备」= 设备的去留：移出本班 / 换到别班（设备自己不算"人"）。
//
// 数据（全部现取，不另存）：
//   · 人员清单：backend.classAdmin.members(classCode) → membersList 信号填表；
//   · 本班设备：deviceModel（ClassService 班级域快照，QAbstractItemModel）——
//     ⚠️ delegate 里必须 model.<role>，不许 modelData.xxx（#475 真数据才炸的坑）；
//   · 换班目标：classes（backend.classes，云端 /api/classes 名单），排除本班自己。
// 写操作走 backend.classAdmin（HTTP /api/class/bind|unbind|member-*），成功后由
// ClassAdminPane 的 Connections 统一 toast + 重拉名单——本件只管发起，不管反馈。
import QtQuick
import "../../components/md3"     // Md3Btn / Md3Card
import "../../island"             // SvgIcon
import "../../components/theme"   // Md3 令牌（pragma Singleton）

Item {
    id: pane

    property var deviceModel: null     // ClassDeviceModel（本班设备）
    property var classes: []           // backend.classes（换班目标）
    property string classCode: ""      // 当前班码（本班）
    property bool subscribed: false

    /** 人员段状态：清单 + 添加行的输入 + 当前选择的角色。 */
    property var memberModel: []       // [{id, role, addedAt…}]（membersList 回填）
    property string selRole: "teacher" // 添加行的角色三选
    property bool  membersLoading: false

    implicitWidth: 320
    implicitHeight: 200

    function admin() {
        return (typeof backend !== "undefined" && backend && backend.classAdmin) ? backend.classAdmin : null
    }

    /** 拉一次当前班的人员清单（切班 / 订阅变化 / 手动点刷新用）。 */
    function reloadMembers() {
        if (!pane.subscribed || pane.classCode === "") return
        const a = pane.admin()
        if (!a) return
        pane.membersLoading = true
        a.members(pane.classCode)
    }
    onClassCodeChanged: reloadMembers()
    onSubscribedChanged: reloadMembers()

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

    Connections {
        target: pane.admin()
        ignoreUnknownSignals: true
        function onMembersList(code, list) {
            if (code !== pane.classCode) return   // 别班的清单不落本地表
            pane.memberModel = list
            pane.membersLoading = false
        }
    }

    Column {
        anchors.fill: parent
        spacing: 10

        Text {
            width: parent.width
            visible: !pane.subscribed
            text: "未订阅班级：先选择一个班，才能管理它的成员。"
            font.pixelSize: Md3.body13
            color: Md3.cOutline
            wrapMode: Text.WordWrap
        }

        // ══ 上半：班级人员（人可以被添加）══
        Md3Card {
            width: parent.width
            visible: pane.subscribed
            variant: "filled"

            Column {
                width: parent.width
                spacing: 8

                Row {
                    width: parent.width
                    spacing: 8
                    height: Math.max(20, addHead.implicitHeight)

                    SvgIcon {
                        name: "users"
                        px: 14
                        tint: Md3.cOnSurfaceVar
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: addHead
                        text: "班级人员（" + pane.memberModel.length + "）"
                        font.pixelSize: Md3.body13
                        font.weight: Font.Medium
                        color: Md3.cOnSurfaceVar
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Item { width: Math.max(6, parent.width - 156 - 46); height: 1 }
                    Md3Btn {
                        text: "刷新"
                        variant: "text"
                        enabled: !pane.membersLoading
                        onClicked: pane.reloadMembers()
                    }
                }

                // 添加行：账号输入 + 角色三选 + 添加
                Row {
                    width: parent.width
                    spacing: 6

                    Rectangle {
                        width: Math.max(96, parent.width - 6 - 96 - 6 - 58)
                        height: 32
                        radius: 6
                        color: Md3.cSurfaceVar

                        TextInput {
                            id: addUid
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: TextInput.AlignVCenter
                            font.pixelSize: Md3.bodyS
                            color: Md3.cOnSurface
                            clip: true
                            selectByMouse: true

                            // TextInput 没有 placeholderText（那是 TextField 的，
                            // 2026-10-24 探针实证），手动画占位符：文本为空才显示。
                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                visible: addUid.text === ""
                                text: "老师账号/uid"
                                font.pixelSize: Md3.bodyS
                                color: Md3.cOutline
                            }
                        }
                    }
                    Rectangle {
                        width: 96
                        height: 32
                        radius: 6
                        color: Md3.cSurfaceVar

                        TextInput {
                            id: addRoleHint
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: TextInput.AlignVCenter
                            font.pixelSize: Md3.bodyS
                            color: Md3.cOnSurface
                            readOnly: true
                            text: pane.selRole
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: rolePop.visible = !rolePop.visible
                        }
                    }
                    Md3Btn {
                        text: "添加"
                        variant: "tonal"
                        enabled: addUid.text.trim() !== ""
                        onClicked: {
                            const a = pane.admin()
                            if (a) a.addMember(pane.classCode, addUid.text.trim(), pane.selRole)
                            addUid.text = ""
                        }
                    }
                }
                // 角色选择浮层（三档，点外收起；初始隐藏，点角色框展开）
                Rectangle {
                    id: rolePop
                    width: parent.width
                    height: 30
                    visible: false
                    radius: 6
                    color: Md3.cSurfaceVar

                    Row {
                        id: roleRow
                        anchors.fill: parent
                        spacing: 6

                        Repeater {
                            model: ["owner", "teacher", "viewer"]
                            delegate: Md3Btn {
                                required property string modelData
                                text: modelData
                                variant: pane.selRole === modelData ? "tonal" : "text"
                                onClicked: {
                                    pane.selRole = modelData
                                    rolePop.visible = false
                                }
                            }
                        }
                    }
                }
            }
        }

        // 人员清单（可变高，最长留 5 行）
        ListView {
            width: parent.width
            height: Math.min(152, Math.max(0, pane.memberModel.length * 26))
            clip: true
            visible: pane.subscribed
            model: pane.memberModel
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                required property var modelData
                width: ListView.view.width
                height: 24
                radius: 4
                color: Md3.cSurfaceVar

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Text {
                        text: (modelData && modelData.id) ? String(modelData.id) : "?"
                        font.pixelSize: Md3.bodyS
                        color: Md3.cOnSurface
                        elide: Text.ElideRight
                        width: parent.width - 96 - 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: (modelData && modelData.role) ? String(modelData.role) : ""
                        font.pixelSize: Md3.bodyS
                        font.weight: String(modelData && modelData.role) === "owner" ? Font.Bold : Font.Normal
                        color: String(modelData && modelData.role) === "owner"
                               ? Md3.cPrimary : (String(modelData && modelData.role) === "teacher"
                               ? Md3.cOnSurfaceVar : Md3.cOutline)
                        width: 68
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Md3Btn {
                        text: "移出"
                        variant: "text"
                        width: 28
                        onClicked: {
                            const a = pane.admin()
                            if (a) a.removeMember(pane.classCode, String(modelData.id))
                        }
                    }
                }
            }
        }
        Text {
            width: parent.width
            visible: pane.subscribed && pane.memberModel.length === 0 && !pane.membersLoading
            text: "还没有成员。填上面的账号后点「添加」—— 那人下次登录即可进入本班。"
            font.pixelSize: Md3.bodyS
            color: Md3.cOutline
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: pane.subscribed && pane.membersLoading
            text: "拉取成员清单中…"
            font.pixelSize: Md3.bodyS
            color: Md3.cOutline
        }

        // ══ 下半：设备成员 ══
        Text {
            width: parent.width
            visible: pane.subscribed
            text: "设备：点行内的「换到…」选目标班，或「移出」让它暂时不属于任何班。"
            font.pixelSize: Md3.bodyS
            color: Md3.cOutline
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
                radius: Md3.rS
                color: Md3.cSurfaceVar

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
                            color: (model.online === true) ? Md3.cOk : Md3.cOutline
                        }
                        Text {
                            text: (model.name && model.name !== "") ? model.name : model.uid
                            font.pixelSize: Md3.body13
                            color: Md3.cOnSurface
                            elide: Text.ElideRight
                            width: parent.width - 16 - 150
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: model.version || ""
                            font.pixelSize: Md3.bodyS
                            color: Md3.cOutline
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // 动作行：换到…（展开目标班按钮）+ 移出
                    Row {
                        width: parent.width
                        spacing: 6

                        Md3Btn {
                            text: "换到…"
                            variant: "text"
                            onClicked: rowExpanded = !rowExpanded
                        }
                        Md3Btn {
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
                            font.pixelSize: Md3.bodyS
                            color: Md3.cOutline
                        }
                    }
                }
            }
        }
    }
}
