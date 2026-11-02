// 班级内容：学生名单 + 课表（2026-11-02 · 屎山整理 #4，从 class-admin.js 逐字搬出）
//
// 课表口径：ClassIsland 档案 → 归一化信封（TIMETABLE_SCHEMA）。CIMS 退役后课表回落到
// 我们自己的库，但格式保持与 ClassIsland 互通：老师手里的档案能直接导进来。
// adminListTimetableGroups 用 classIslandGroupMeta 的「首次出现序」推断群顺序与
// 「几班」标签 —— 12 个群经常同名，靠名字猜必错。


import { listStudents, addStudent, updateStudent, removeStudent, importStudents,
  getTimetable, saveTimetable } from './class-data.js';
import { normalizeClassIslandProfile, classIslandGroupMeta, TIMETABLE_SCHEMA } from './class-timetable.js';
import { pushEvent } from './registry.js';
import { pushClassEvent } from './class-events.js';
import { pushTimetableToClass, timetableDelivery } from './class-timetable-push.js';
import { tenantOf, findClass, canReadClass, canManageMembers } from './class-admin-shared.js';

/** 学生名单（按座位/学号排）。 */
export function adminListStudents(code, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadClass(ident, hit.code)) return { ok: false, error: '无权限查看该班学生名单' };
  const r = listStudents(hit.code, tn);
  return { ...r, name: hit.name };
}

export function adminAddStudent(code, student = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班学生名单' };
  const r = addStudent(hit.code, student, ident?.uid ?? 'system', tn);
  if (!r.ok) return r;
  pushEvent('info', r.replaced ? '管理端更新学生信息' : '管理端新增学生', {
    classCode: hit.code, studentId: r.student.id, name: r.student.name, by: ident?.uid ?? 'system',
  });
  return r;
}

export function adminUpdateStudent(code, studentId, patch = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班学生名单' };
  const r = updateStudent(hit.code, studentId, patch, ident?.uid ?? 'system', tn);
  if (r.ok) pushEvent('info', '管理端修改学生信息', { classCode: hit.code, studentId, by: ident?.uid ?? 'system' });
  return r;
}

export function adminRemoveStudent(code, studentId, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班学生名单' };
  const r = removeStudent(hit.code, studentId, tn);
  if (r.ok && r.removed) pushEvent('info', '管理端移除学生', { classCode: hit.code, studentId, by: ident?.uid ?? 'system' });
  return r;
}

export function adminImportStudents(code, rows = [], ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班学生名单' };
  const r = importStudents(hit.code, rows, ident?.uid ?? 'system', tn);
  if (r.ok) pushEvent('info', '管理端批量导入学生', {
    classCode: hit.code, added: r.added, updated: r.updated, bad: r.bad, by: ident?.uid ?? 'system',
  });
  return r;
}

/** 班级课表（没存过 → profile:null，前端必须显示「还没排课」而不是空白表）。 */
export function adminGetTimetable(code, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadClass(ident, hit.code)) return { ok: false, error: '无权限查看该班课表' };
  const r = getTimetable(hit.code, tn);
  return { ...r, name: hit.name };
}

export function adminSaveTimetable(code, profile, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限修改该班课表' };
  const r = saveTimetable(hit.code, profile, ident?.uid ?? 'system', tn);
  if (r.ok) pushEvent('info', '管理端保存班级课表', { classCode: hit.code, bytes: r.bytes, by: ident?.uid ?? 'system' });
  return r;
}

/* ───────────── 课表导入：ClassIsland 档案（2026-10-25）─────────────
 * 为什么要在云端做归一化（而不是让每个客户端各写一份解析）：
 *   · 老师的档案来自 ClassIsland，桌面端 / 平板端 / 以后的小程序都要能吃；
 *   · 解析规则（第 N 节课 ↔ 作息第 N 个上课时段）一旦两端各写，必然漂；
 *   · 归一化是纯函数，单测好写、出错能回人话。
 * 多课表群（一份档案含多个班）时必须显式指定 —— 猜错就是把 8 班课表挂到 1 班。
 */
export function adminImportTimetable(code, rawProfile, groupKey = '', ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限修改该班课表' };
  const norm = normalizeClassIslandProfile(rawProfile, groupKey);
  if (!norm.ok) {
    // groups 回给 UI：让老师自己选，而不是我们猜
    return { ok: false, error: norm.error, groups: norm.groups || [] };
  }
  const r = saveTimetable(hit.code, norm.normalized, ident?.uid ?? 'system', tn);
  if (r.ok) {
    // 审计口径统一用「X班」标签（群名可能 12 个都叫「新课表群」，写进日志等于没写）
    const g = norm.normalized.group;
    pushEvent('info', '管理端导入 ClassIsland 课表', {
      classCode: hit.code, group: g.label || g.name,
      days: norm.normalized.days.length, bytes: r.bytes, by: ident?.uid ?? 'system',
    });
    pushClassEvent(hit.code, 'timetable', {
      group: g.label || g.name, days: norm.normalized.days.length,
      schema: TIMETABLE_SCHEMA,
    }, ident?.uid ?? 'system');
  }
  return { ...r, group: norm.normalized.group, days: norm.normalized.days.length,
           lessons: norm.normalized.days.reduce((n, d) => n + d.lessons.length, 0) };
}

/**
 * 列一份档案里的课表群（UI 在导入前预览用；不落库）。
 * nameSource='order' 表示档案里 12 个群同名，班名是按出现顺序推的 ⇒ 界面必须提示核对。
 */
export function adminListTimetableGroups(rawProfile) {
  const meta = classIslandGroupMeta(rawProfile);
  return meta.groups.length
    ? { ok: true, groups: meta.groups, nameSource: meta.nameSource, conflict: meta.conflict }
    : { ok: false, error: '档案里没有任何日课表（ClassPlan 为空或格式不对）', groups: [] };
}

/* ───────────── 课表下发到教室机（2026-11-02）─────────────
 * 在改造前，导入课表到这里就结束了：落库 + 广播给管理端浏览器，**没有一个字节到设备**。
 * 下面的两个函数把「下发」与「送达视图」补上；真正的传输在 class-timetable-push.js。
 * 权限口径与保存课表一致（canManageMembers）—— 能把课表改掉的人，才能让它到教室。
 */
export function adminPushTimetable(code, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限下发该班课表' };
  const r = pushTimetableToClass(hit.code, tn);
  if (r.ok) {
    pushEvent('info', '管理端下发课表到教室机', {
      classCode: hit.code, online: r.online, delivered: r.delivered, by: ident?.uid ?? 'system',
    });
  }
  return { ...r, classCode: hit.code };
}

/** 送达视图：谁已落盘、谁离线没回执。没回执 ≠ 失败，是两件事，界面必须分开显示。 */
export function adminTimetableDelivery(code, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadClass(ident, hit.code)) return { ok: false, error: '无权限查看该班课表送达情况' };
  return { ...timetableDelivery(hit.code, tn), classCode: hit.code };
}
