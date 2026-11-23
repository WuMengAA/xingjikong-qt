// 星集控云端 · 班级隔离单元（ClassroomUnit）· 2026-11-10
//
// 用户裁决：「班级 = 隔离单元」「新建并列服务 + 旧路标记退役」「不要在旧基础上修改，直接重构片段」。
//
// ── 为什么要有这个文件 ────────────────────────────────────────────────
// 在此之前，云端**没有「班级」这个对象**：classCode 只是一个字符串参数，
// 被 16 个 class-*.js 与 index.js 里 14 段路由各自传递。
//   · tenant.js:7-8 自己写着「knownDevices 是全局 Map，classCode 只是其中一个字段」；
//   · bind-api.js 有 5 条并行绑定通道（admin/code/qr/device/class-scan）；
//   · 设备归属、成员、课表、考勤、权限各查各的表，没有统一边界。
// ⇒ 「单个独立班级用于接入」没有真身：接入时无法回答「这个班的完整边界是什么」。
//
// 本文件是**新地基**：把「一个班」定义成唯一对象，所有派生（接入码 / 设备组 /
// 成员范围 / 权限判据 / 数据分区）都从它出发。纯函数、零 IO、零全局状态 ⇒ 可离屏单测。
// 不 import 任何旧 class-*.js，避免把旧网状依赖带进新地基。
//
// 分层（AGENTS.md §0）：域只算数与定契约；注册表/HTTP 在 classroom-service.js。
//
// 时间单位：毫秒（与云端 bind-qr / ticket 同口径）；标识一律字符串。
export const CLASSROOM_SCHEMA = 1;

/**
 * 班级隔离单元的**不可变**定义。
 * 创建后只许整体替换，不许就地改字段 —— id 一旦签发不再改（同 tenant.js canonical 铁律）。
 */
export function createClassroomUnit({
  tenantId = 'tn_default',
  classCode = '',
  className = '',
  ownerUid = null,
  createdAtMs = 0,
  // 该班专属的接入码（由云端签发，不由客户端指定）
  joinCode = null,
  joinCodeIssuedAtMs = 0,
  joinCodeTtlMs = 24 * 3600 * 1000,
  state = 'active',        // active | suspended | archived
} = {}) {
  const code = String(classCode || '').trim();
  if (!code) throw new Error('classCode 不能为空');
  const tid = String(tenantId || 'tn_default').trim() || 'tn_default';
  return Object.freeze({
    schema: CLASSROOM_SCHEMA,
    // 全局唯一 = 租户 + 班码。两所学校各有一个「1班」也不会串台。
    id: `${tid}:${code}`,
    tenantId: tid,
    classCode: code,
    className: String(className || '').trim() || code,
    ownerUid: ownerUid ? String(ownerUid) : null,
    createdAtMs: Number(createdAtMs) || 0,
    joinCode: joinCode ? String(joinCode) : null,
    joinCodeIssuedAtMs: Number(joinCodeIssuedAtMs) || 0,
    joinCodeTtlMs: Number(joinCodeTtlMs) || 0,
    state: ['active', 'suspended', 'archived'].includes(state) ? state : 'active',
  });
}

/** 从旧世界的对象（knownDevices 行 / class-dir 行）归一成单元；不信任客户端自报。 */
export function unitFromLegacy(row = {}, tenantId = 'tn_default') {
  return createClassroomUnit({
    tenantId,
    classCode: row.classCode ?? row.code ?? row.class_code ?? '',
    className: row.className ?? row.name ?? '',
    ownerUid: row.ownerUid ?? null,
    createdAtMs: row.createdAt ?? row.created_at ?? 0,
  });
}

/** 判据：两台设备/两个请求是否落在同一个班级隔离单元内。 */
export function sameUnit(a, b) {
  if (!a || !b) return false;
  return String(a.tenantId || 'tn_default') === String(b.tenantId || 'tn_default')
      && String(a.classCode || '') === String(b.classCode || '');
}

/**
 * ★ 单一权限判据：一个身份能不能触达这个班。
 * 旧实现是 perms.canAccess(ident, classCode) 只看班码；新地基把租户 + 班级状态一起判。
 *  - platform: 全校（租户内）
 * - classOwner: 只看自己班（用户裁决「班主任只看自己班，管理员看全校」）
 * - device: 只看自己所属班
 */
export function canReachUnit(ident = {}, unit = null, nowMs = 0) {
  if (!unit) return { ok: false, reason: '班级单元不存在' };
  if (unit.state === 'archived') return { ok: false, reason: '班级已归档' };
  const role = String(ident.role || '');
  const tid = String(ident.tenantId || 'tn_default');
  if (tid !== unit.tenantId) return { ok: false, reason: '跨租户访问被拒' };
  if (role === 'platform') return { ok: true, scope: 'tenant' };
  if (unit.state === 'suspended' && role !== 'platform')
    return { ok: false, reason: '班级已停用' };
  if (role === 'classOwner') {
    return String(ident.classCode || '') === unit.classCode
      ? { ok: true, scope: 'unit' }
      : { ok: false, reason: '班主任只能访问自己班' };
  }
  if (role === 'device') {
    return sameUnit({ tenantId: tid, classCode: ident.classCode }, unit)
      ? { ok: true, scope: 'unit' }
      : { ok: false, reason: '设备不属于该班' };
  }
  if (Array.isArray(ident.scopes) && ident.scopes.length === 0)
    return { ok: false, reason: '显式零权限' };
  return { ok: false, reason: '身份无班级访问权' };
}

/**
 * ★ 单一接入判据：一个接入码能不能把设备接到这个班。
 * 旧世界 5 条通道各判各的；新地基只有这一个函数。
 * 返回 { ok, reason, remainingMs }；调用方不得自行再判一次。
 */
export function canJoinUnit(unit, code, nowMs) {
  if (!unit) return { ok: false, reason: '班级单元不存在' };
  if (unit.state !== 'active') return { ok: false, reason: `班级状态为 ${unit.state}` };
  if (!unit.joinCode) return { ok: false, reason: '该班未签发接入码' };
  if (String(code || '').trim() !== unit.joinCode)
    return { ok: false, reason: '接入码不匹配' };
  const age = Number(nowMs) - unit.joinCodeIssuedAtMs;
  if (age < 0) return { ok: false, reason: '接入码尚未生效' };
  if (unit.joinCodeTtlMs > 0 && age > unit.joinCodeTtlMs)
    return { ok: false, reason: '接入码已过期' };
  return {
    ok: true,
    reason: 'ok',
    remainingMs: unit.joinCodeTtlMs > 0 ? unit.joinCodeTtlMs - age : Infinity,
  };
}

/** 一站式快照：接入/展示/权限共用的**唯一**派生产物（不许第二份状态）。 */
export function unitSnapshot(unit, nowMs, {
  devices = [], memberCount = 0, timetableUpdatedAt = 0, pendingCount = 0,
} = {}) {
  const own = devices.filter((d) => sameUnit(
    { tenantId: d.tenantId ?? unit.tenantId, classCode: d.classCode }, unit));
  return {
    schema: CLASSROOM_SCHEMA,
    id: unit.id,
    tenantId: unit.tenantId,
    classCode: unit.classCode,
    className: unit.className,
    state: unit.state,
    ownerUid: unit.ownerUid,
    nowMs,
    devices: {
      total: own.length,
      online: own.filter((d) => d.online).length,
      uids: own.map((d) => String(d.uid || '')).filter(Boolean),
    },
    memberCount,
    timetableUpdatedAt,
    pendingCount,
    join: {
      hasCode: !!unit.joinCode,
      remainingMs: unit.joinCode && unit.joinCodeTtlMs > 0
        ? Math.max(0, unit.joinCodeTtlMs - (nowMs - unit.joinCodeIssuedAtMs))
        : null,
    },
  };
}
