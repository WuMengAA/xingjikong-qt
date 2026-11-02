// 班级考勤（2026-11-02 · 屎山整理 #4，从 class-admin.js 逐字搬出）
//
// 纯数据层在 class-attendance.js，这里只做权限封装：写 = canManageMembers，
// 读 = canReadAttendance（本班任一角色 + scopes）。


import { recordAttendance, batchRecord, listByClassDate, listByClassRange,
  removeAttendance, attendanceStats } from './class-attendance.js';
import { pushEvent } from './registry.js';
import { tenantOf, findClass, canManageMembers, canReadAttendance } from './class-admin-shared.js';

/** 单条记录 / 改判。 */
export function adminRecordAttendance(code, input = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限记录该班考勤' };
  const r = recordAttendance(hit.code, input, ident?.uid ?? 'system', tn);
  if (!r.ok) return r;
  pushEvent('info', r.replaced ? '管理端改判考勤' : '管理端记录考勤', {
    classCode: hit.code, studentId: r.record.studentId, status: r.record.status, by: ident?.uid ?? 'system',
  });
  return r;
}

/** 批量记录（全班一次性提交）。 */
export function adminBatchAttendance(code, rows = [], ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限记录该班考勤' };
  const r = batchRecord(hit.code, rows, ident?.uid ?? 'system', tn);
  if (r.ok) pushEvent('info', '管理端批量记录考勤', {
    classCode: hit.code, total: r.total, succeeded: r.succeeded, failed: r.failed, by: ident?.uid ?? 'system',
  });
  return r;
}

/** 按班按日列出（可带 session 过滤）。 */
export function adminListAttendance(code, q = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadAttendance(ident, hit.code)) return { ok: false, error: '无权限查看该班考勤' };
  return listByClassDate(hit.code, String(q.date ?? ''), String(q.session ?? ''), tn);
}

/** 按班按日期区间列出。 */
export function adminListAttendanceRange(code, q = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadAttendance(ident, hit.code)) return { ok: false, error: '无权限查看该班考勤' };
  return listByClassRange(hit.code, String(q.fromDate ?? ''), String(q.toDate ?? ''), tn);
}

/** 某日考勤统计（含未记录人数）。 */
export function adminAttendanceStats(code, q = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadAttendance(ident, hit.code)) return { ok: false, error: '无权限查看该班考勤统计' };
  return attendanceStats(hit.code, String(q.date ?? ''), tn);
}

/** 删一条考勤（幂等）。 */
export function adminRemoveAttendance(code, q = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限修改该班考勤' };
  const r = removeAttendance(hit.code, String(q.studentId ?? ''), String(q.date ?? ''), String(q.session ?? ''), tn);
  if (r.ok && r.removed) pushEvent('info', '管理端删除考勤记录', { classCode: hit.code, studentId: q.studentId, by: ident?.uid ?? 'system' });
  return r;
}
