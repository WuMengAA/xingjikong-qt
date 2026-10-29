// 班级工作台 · 中栏「成员管理」tab · QtShadcn 规范（2026-10-29 拆分 + 迁移）
//
// ── 2026-10-29 拆分说明 ────────────────────────────────────────────────
// 原单文件 403 行，超出「单文件 ≤300 行」红线 ⇒ 按职责拆成三件：
//   · 本件（ClassMemberPane）= **容器**：持有数据、订阅信号、发起写操作、拼装两段
//   · ClassMemberPeopleSection = 「班级人员（人）」段
//   · ClassMemberDeviceSection = 「设备」段
// 拆完本件约 150 行，两段各约 150 / 130 行，全部达标。
//
// ── 两段各管各的"成员" ─────────────────────────────────────────────────
//   · 上半「班级人员」= **人（账号）**：把老师/助教账号挂到本班，他下次登录就能订
//     这个班、看这个班的设备。数据来自云端 class_members（/api/class/members），
//     角色 owner/teacher/viewer。
//   · 下半「设备」= 设备的去留：移出本班 / 换到别班（设备自己不算"人"）。
//
// ── 数据（全部现取，不另存）────────────────────────────────────────────
//   · 人员清单：backend.classAdmin.members(classCode) → membersList 信号填表
//   · 本班设备：deviceModel（ClassService 班级域快照，QAbstractItemModel）
//   · 换班目标：classes（backend.classes），排除本班自己
// 写操作走 backend.classAdmin（HTTP /api/class/bind|unbind|member-*），成功后由
// ClassAdminPane 的 Connections 统一 toast + 重拉名单 —— 本件只管发起，不管反馈。
import QtQuick
import QtQuick.Layouts
import "../../components"           // AppCard
import "../../components/theme"    // Theme 令牌

Item {
    id: pane

    property var deviceModel: null     // ClassDeviceModel（本班设备）
    property var classes: []           // backend.classes（换班目标）
    property string classCode: ""      // 当前班码（本班）
    property bool subscribed: false

    /** 人员段状态：清单 + 当前选择的角色。 */
    property var memberModel: []       // [{id, role, addedAt…}]（membersList 回填）
    property string selRole: "teacher"
    property bool membersLoading: false

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

    /** 换班目标（排除本班；没订阅时为空列表）。 */
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

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing.sm + 2

        // 未订阅提示
        Text {
            Layout.fillWidth: true
            visible: !pane.subscribed
            text: "未订阅班级：先选择一个班，才能管理它的成员。"
            font.pixelSize: 13
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        // ── 上半：班级人员 ──────────────────────────────────────────
        AppCard {
            Layout.fillWidth: true
            visible: pane.subscribed

            ClassMemberPeopleSection {
                Layout.fillWidth: true
                memberModel: pane.memberModel
                selRole: pane.selRole
                loading: pane.membersLoading
                onReloadRequested: pane.reloadMembers()
                onRolePicked: (role) => pane.selRole = role
                onAddRequested: (uid, role) => {
                    const a = pane.admin()
                    if (a) a.addMember(pane.classCode, uid, role)
                }
                onRemoveRequested: (id) => {
                    const a = pane.admin()
                    if (a) a.removeMember(pane.classCode, id)
                }
            }
        }

        // ── 下半：设备 ──────────────────────────────────────────────
        ClassMemberDeviceSection {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: pane.subscribed
            deviceModel: pane.subscribed ? pane.deviceModel : null
            targets: pane.subscribed ? pane.targetClasses() : []
            onBindRequested: (code, uid) => {
                const a = pane.admin()
                if (a) a.bindDevice(code, uid)
            }
            onUnbindRequested: (uid) => {
                const a = pane.admin()
                if (a) a.unbindDevice(pane.classCode, uid)
            }
        }
    }
}
