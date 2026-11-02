// 班级管理 HTTP 分发（2026-11-02 · 屎山整理 #4）
//
// 本文件从 734 行拆出：业务函数按「班级身份 / 学生与课表 / 考勤」三个对象分到
// class-admin-classes.js / class-admin-students.js / class-admin-attendance.js，
// 权限与租户判定进 class-admin-shared.js。本文件只剩**路由分发**与 **re-export**。
//
// 为什么保留 re-export：index.js 写的是
//     import { handleClassAdminApi, adminMyClasses } from './class-admin.js';
// 对外 API 面一个字不改 —— 拆文件不该逼着调用方跟着改，那会把风险摊到每个调用点。

import { readJsonBody } from './class-admin-shared.js';
// ⚠️ 下面这批**必须是 import**，不能只靠文件末尾的 `export { … } from`：
//    `export { X } from './y.js'` 只把 X 挂到本模块导出面上，**不引入本地作用域**。
//    首版就是这么写的 —— handleClassAdminApi 里每个 admin* 都成了未定义标识符，
//    而模块 import 照样成功、28 个导出一个不少（ESM 不做标识符静态检查），
//    **一调用就 ReferenceError → 500**。教训：拆文件后必须真发一次请求，
//    「能 import」证明不了任何事。
import {
  adminCreateClass, adminRenameClass, adminDeleteClass, adminUnbindDevice, adminBindDevice,
  adminAddMember, adminRemoveMember, adminListMembers, adminMyClasses,
  adminIssueBindCode, adminRevokeBindCode, adminPublish,
} from './class-admin-classes.js';
import {
  adminListStudents, adminAddStudent, adminUpdateStudent, adminRemoveStudent,
  adminImportStudents, adminGetTimetable, adminSaveTimetable, adminImportTimetable,
  adminListTimetableGroups,
} from './class-admin-students.js';
import {
  adminRecordAttendance, adminBatchAttendance, adminListAttendance,
  adminListAttendanceRange, adminAttendanceStats, adminRemoveAttendance,
} from './class-admin-attendance.js';

/**
 * HTTP 路由：POST /api/class/{create,rename,delete,unbind,bind,member-add,member-remove,members,my-classes,publish,
 *    students,student-add,student-update,student-remove,students-import,
 *    timetable,timetable-save,timetable-import,timetable-groups,
 *    attendance-record,attendance-batch,attendance-list,attendance-range,attendance-stats,attendance-remove}
 * （鉴权在 index.js 的 isViewerApi 组，与 /api/device/bind-class 同一凭据口径；写操作过重认证门）。
 * timetable-import 收 **ClassIsland 原始档案** + 可选 group（多群时必填），云端归一化后落库；
 * timetable-groups 只预览档案里的课表群，不落库（导入前的选择器用）。
 */
export function handleClassAdminApi(req, res, u, send, ident = null) {
  const action = u.pathname.replace('/api/class/', '');
  readJsonBody(req, (body) => {
    if (body === null) return send(400, { ok: false, error: '请求体不是合法 JSON' });
    try {
      let r;
      if (action === 'create') {
        // ⚠️ ident 必须传：建班要落在**本租户**，否则两校都会往默认组织建班。
        r = adminCreateClass({ code: String(body.classCode ?? ''), name: String(body.className ?? '') }, ident);
      } else if (action === 'rename') {
        r = adminRenameClass(String(body.classCode ?? ''), String(body.className ?? ''), ident);
      } else if (action === 'delete') {
        r = adminDeleteClass(String(body.classCode ?? ''), String(body.strategy ?? 'refuse'), ident);
      } else if (action === 'unbind') {
        r = adminUnbindDevice(String(body.classCode ?? ''), String(body.uid ?? ''), ident);
      } else if (action === 'bind') {
        r = adminBindDevice(String(body.classCode ?? ''), String(body.uid ?? ''),
                            String(body.className ?? ''), ident);
      } else if (action === 'member-add') {
        r = adminAddMember(String(body.classCode ?? ''), String(body.uid ?? ''),
                           String(body.role ?? 'teacher'), ident);
      } else if (action === 'member-remove') {
        r = adminRemoveMember(String(body.classCode ?? ''), String(body.uid ?? ''), ident);
      } else if (action === 'members') {
        // ident 必须传：成员清单带 uid+角色，不能无鉴权可读（见 adminListMembers 注释）
        r = adminListMembers(String(body.classCode ?? ''), ident);
      } else if (action === 'my-classes') {
        r = adminMyClasses(ident);
      } else if (action === 'bindcode') {
        // 班级接入码（2026-10-25 收归我方）：发码 / 作废。权限同 member-*。
        r = adminIssueBindCode(String(body.classCode ?? ''), ident);
      } else if (action === 'bindcode-revoke') {
        r = adminRevokeBindCode(String(body.code ?? ''), ident);
      } else if (action === 'publish') {
        r = adminPublish(String(body.classCode ?? ''), body.card ?? {}, ident);
      } else if (action === 'students') {
        r = adminListStudents(String(body.classCode ?? ''), ident);
      } else if (action === 'student-add') {
        r = adminAddStudent(String(body.classCode ?? ''), body.student ?? {}, ident);
      } else if (action === 'student-update') {
        r = adminUpdateStudent(String(body.classCode ?? ''), String(body.studentId ?? ''),
                               body.patch ?? {}, ident);
      } else if (action === 'student-remove') {
        r = adminRemoveStudent(String(body.classCode ?? ''), String(body.studentId ?? ''), ident);
      } else if (action === 'students-import') {
        r = adminImportStudents(String(body.classCode ?? ''), body.rows ?? [], ident);
      } else if (action === 'timetable') {
        r = adminGetTimetable(String(body.classCode ?? ''), ident);
      } else if (action === 'timetable-save') {
        r = adminSaveTimetable(String(body.classCode ?? ''), body.profile ?? null, ident);
      } else if (action === 'timetable-import') {
        r = adminImportTimetable(String(body.classCode ?? ''), body.profile ?? null,
                                 String(body.group ?? ''), ident);
      } else if (action === 'timetable-groups') {
        r = adminListTimetableGroups(body.profile ?? null);
      } else if (action === 'attendance-record') {
        // 2026-10-31 考勤：单条记录 / 改判（upsert）。写权限 = canManageMembers。
        r = adminRecordAttendance(String(body.classCode ?? ''), body.record ?? {}, ident);
      } else if (action === 'attendance-batch') {
        // 全班一次性提交。写权限同 attendance-record。
        r = adminBatchAttendance(String(body.classCode ?? ''), body.records ?? [], ident);
      } else if (action === 'attendance-list') {
        // 按班按日列出（可带 session）。读权限 = canReadClass（本班任一角色 + scopes）。
        r = adminListAttendance(String(body.classCode ?? ''), body, ident);
      } else if (action === 'attendance-range') {
        // 按班按日期区间列出。读权限同 attendance-list。
        r = adminListAttendanceRange(String(body.classCode ?? ''), body, ident);
      } else if (action === 'attendance-stats') {
        // 某日考勤统计（四状态计数 + 未记录人数）。读权限同 attendance-list。
        r = adminAttendanceStats(String(body.classCode ?? ''), body, ident);
      } else if (action === 'attendance-remove') {
        // 删一条（幂等）。写权限 = canManageMembers。
        r = adminRemoveAttendance(String(body.classCode ?? ''), body, ident);
      } else {
        return send(404, { ok: false, error: `未知班级管理操作：${action}` });
      }
      send(r.ok ? 200 : 400, { ok: r.ok, ...r });
    } catch (e) {
      // 业务执行异常（如缺依赖的 ReferenceError）必须如实暴露，绝不可伪装成
      // 「请求体不是合法 JSON」——否则真实 bug 会被静默吞掉（本项目铁律：不许 fail-silent）。
      // 这里落日志并回 500，而不是让异常冒泡成未捕获异常把进程带崩。
      console.error('[class-admin] 处理异常：', e && e.stack || e);
      if (!res.headersSent) send(500, { ok: false, error: '服务器内部错误' });
    }
  });
}

// ── 对外 API 面（原样再导出，调用方无需改动）─────────────────────────────
// 再导出的是上面 import 进来的本地绑定（不再写 `from`）：
// 写成 `export { X } from './y.js'` 会与本地 import 的 X 撞成"重复导出名"，
// 而那个错要等到有人 import 这个模块时才报，比现在这样更难查。
export {
  adminCreateClass, adminRenameClass, adminDeleteClass, adminUnbindDevice, adminBindDevice,
  adminAddMember, adminRemoveMember, adminListMembers, adminMyClasses,
  adminIssueBindCode, adminRevokeBindCode, adminPublish,
  adminListStudents, adminAddStudent, adminUpdateStudent, adminRemoveStudent,
  adminImportStudents, adminGetTimetable, adminSaveTimetable, adminImportTimetable,
  adminListTimetableGroups,
  adminRecordAttendance, adminBatchAttendance, adminListAttendance,
  adminListAttendanceRange, adminAttendanceStats, adminRemoveAttendance,
};
