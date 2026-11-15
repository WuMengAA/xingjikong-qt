// 教师扫码绑定网关（2026-11-16 · 用户 m33515 裁决「管理端只做老师扫码绑定班级渠道」）。
// 纯函数：只产出 class-scan 所需的最小调用计划；不暴露 /api/admin/*、不批量、不建班、不越权。
import { XIANLIN_CAMPUS } from './campus-registry.js';

export const TEACHER_GATEWAY_SCHEMA = 1;
export const TEACHER_FORBIDDEN_ACTIONS = Object.freeze([
  'create-class', 'create', 'delete-class', 'students-import', 'attendance-batch',
  'batch-bind', 'batch', 'platform-admin', 'super-admin', 'tenant-create',
]);

/** 明确白名单：老师只能扫码绑定自己班、看本班课表送达。 */
export const TEACHER_ALLOWED_ACTIONS = Object.freeze([
  'my-classes', 'timetable', 'timetable-delivery', 'class-scan-bind',
]);

export function isTeacherActionAllowed(action) {
  return TEACHER_ALLOWED_ACTIONS.includes(String(action || '').trim());
}

export function isAdminOrBatch(action) {
  const a = String(action || '').trim();
  return TEACHER_FORBIDDEN_ACTIONS.includes(a)
    || /^(batch|platform|super)/i.test(a)
    || /class.*(create|delete|register)$/i.test(a);
}

/** 教师扫码：票由云端签发；这里只把渠道钉成 class-scan，绝不复制判权/改租户。 */
export function teacherScanPlan({ ticketCode = '', uid = '', campusId = 'xianlin' } = {}) {
  const code = String(ticketCode || '').trim().toUpperCase();
  const u = String(uid || '').trim();
  if (!/^[A-Z0-9]{6}$/.test(code)) return { ok: false, error: '接入码需 6 位字母数字' };
  if (!u) return { ok: false, error: '缺少设备标识' };
  return {
    ok: true,
    schema: TEACHER_GATEWAY_SCHEMA,
    channel: 'class-scan',
    ticketCode: code,
    uid: u,
    campusId,
    campusName: XIANLIN_CAMPUS.name,
    // 入口层必须调用既有唯一核心 classScanBind(ticket, uid, devices)；
    // 本网关不 import registry/bind-api，避免形成第二套绑定真源。
    bindCore: 'classScanBind',
    requireManage: false,
    allowCreateClass: false,
    allowBatch: false,
  };
}

export function teacherGatewaySummary() {
  return {
    schema: TEACHER_GATEWAY_SCHEMA,
    allowed: TEACHER_ALLOWED_ACTIONS.slice(),
    forbidden: TEACHER_FORBIDDEN_ACTIONS.slice(),
    role: 'teacher',
    capability: '扫码绑定班级',
    batch: false,
    superAdmin: false,
    crossTenant: false,
  };
}
