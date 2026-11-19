// 校区注册与加入的独立状态层（2026-11-19 · 用户点单「管理端新建校区 / 加入校区」）。
//
// 设计边界（沿用 instance.js 的实例自治契约）：
//   · 本模块只做纯逻辑 + 可注入持久化，不连网、不写生产库、不重启任何服务。
//   · 「新建校区」由部署方在本实例登记：id / 名称 / 对外地址 / 班级数。
//   · 「加入校区」是一个幂等登记：一个 uid 以某 role 加入某校区，重复加入只更新角色，
//     不产生第二条成员记录 —— 与设备「每台独立注册、可加入校区」的口径一致。
//   · 最小权限：join 只接受 member / teacher / admin 三种角色；管理动作（建校区）
//     需要 create 权限，但不是「超管下发」，而是本实例内部授权。
//   · fail-closed：对外地址不接受路径/查询；空名、坏 id、未知校区一律拒绝。

export const CAMPUS_MEMBER_SCHEMA = 1;
export const CAMPUS_MEMBER_ROLES = Object.freeze(["member", "teacher", "admin"]);

function clean(v) { return String(v ?? "").trim(); }

/** 校区 id：小写字母/数字/-，2-32 位。与 device uid 不同，校区 id 不区分大小写。 */
export function normalizeCampusId(raw) {
  const s = clean(raw).toLowerCase();
  return /^[a-z0-9][a-z0-9-]{1,31}$/.test(s) ? s : "";
}

/** 参与角色规范化：未知角色一律拒绝（fail-closed，绝不默认给 admin）。 */
export function normalizeMemberRole(raw) {
  const s = clean(raw).toLowerCase();
  return CAMPUS_MEMBER_ROLES.includes(s) ? s : "";
}

/** 参与者 uid：6-64 位字母数字、_ 或 -（与班级码 6-16 位区分开）。 */
export function normalizeMemberUid(raw) {
  const s = clean(raw);
  return /^[A-Za-z0-9_-]{6,64}$/.test(s) ? s : "";
}

/** 新建校区：返回规范化定义或 {error}，调用方据此回 200/400。 */
export function validateNewCampus(input = {}, normalizeBase) {
  const id = normalizeCampusId(input.id);
  const name = clean(input.name);
  const shortName = clean(input.shortName) || name;
  const publicBase = normalizeBase ? normalizeBase(input.publicBase) : clean(input.publicBase);
  const classCount = Number(input.classCount);
  if (!id) return { ok: false, error: "校区 id 不合法（小写字母/数字/-，2-32 位）" };
  if (!name || name.length > 64) return { ok: false, error: "校区名称不能为空且不超过 64 字" };
  if (!publicBase) return { ok: false, error: "对外地址不合法（只接受协议://主机[:端口]）" };
  if (!Number.isInteger(classCount) || classCount < 1 || classCount > 200)
    return { ok: false, error: "班级数必须是 1-200 的整数" };
  return {
    ok: true,
    campus: { id, name, shortName, publicBase, classCount, schema: CAMPUS_MEMBER_SCHEMA },
  };
}

/** 加入校区：幂等。已存在同 uid 时只更新角色与名称，不新增成员。 */
export function applyJoinCampus(state, input = {}) {
  const uid = normalizeMemberUid(input.uid);
  const campusId = normalizeCampusId(input.campusId);
  const role = normalizeMemberRole(input.role);
  if (!uid) return { ok: false, error: "参与者 uid 不合法（6-64 位字母数字或 _-）" };
  if (!campusId) return { ok: false, error: "校区 id 不合法" };
  if (!role) return { ok: false, error: "参与角色不合法（member/teacher/admin）" };
  const campus = (state.campuses || []).find((c) => c.id === campusId);
  if (!campus) return { ok: false, error: "校区不存在" };
  const members = Array.isArray(state.members) ? state.members.slice() : [];
  const idx = members.findIndex((m) => m.uid === uid);
  const record = { uid, campusId, role, name: clean(input.name), joinedAt: clean(input.joinedAt) };
  if (idx >= 0) members[idx] = { ...members[idx], ...record };
  else members.push(record);
  return { ok: true, state: { ...state, members }, member: record, created: idx < 0 };
}

/** 参与者可见的校区（只回服务端钉死的字段，不信任请求体 URL）。 */
export function campusesForUid(state, uid) {
  const u = normalizeMemberUid(uid);
  if (!u) return [];
  return (state.members || [])
    .filter((m) => m.uid === u)
    .map((m) => (state.campuses || []).find((c) => c.id === m.campusId))
    .filter(Boolean);
}

/** 初始状态：把既有硬编码咸林中学作为第一条校区，历史数据不丢。 */
export function initialCampusState(seedCampus) {
  const c = seedCampus ? { ...seedCampus, schema: CAMPUS_MEMBER_SCHEMA } : null;
  return { schema: CAMPUS_MEMBER_SCHEMA, campuses: c ? [c] : [], members: [] };
}
