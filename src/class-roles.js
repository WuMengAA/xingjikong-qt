// 班级后台六角色 → claims 预设（2026-11-15 · 用户 m30161 第 2 条收敛）
//
// ⚠️ 本模块**不参与权限判定**，不造第二套 RBAC：
//   · 看不看得见 = Stelarith-cloud/src/perms.js 的 canAccess(ident, classCode, {tenantId})；
//   · 改不改得了 = Stelarith-cloud/src/class-admin-shared.js 的 canManageMembers / canReadClass，
//     它查 class-room 的成员表角色（owner / teacher），与票据里的 roles 标签不是一回事；
//   · 平台级 = perms.js 的 isPlatform（schoolId == null 且 roles 含 platform_admin）。
// 本模块只回答一件事：给这六种人签票 / 回填 user-claims 白名单时，claims 该长什么样。
// 好处：六处不再手搓 scopes 形状，且「无 claims ⇒ 零权限」（f943fd8）不会被谁顺手改回全权。

import { TENANT_ID_RE, DEFAULT_TENANT } from './tenant.js';

export const ROLE_KEYS = Object.freeze([
  'superAdmin', 'schoolAdmin', 'classOwner', 'teacher', 'student', 'parent',
]);

export const ROLE_CATALOG = Object.freeze([
  { key: 'superAdmin', label: '超级管理员', reach: '跨校', claims: 'platform_admin + schoolId=null' },
  { key: 'schoolAdmin', label: '学校管理员', reach: '本校', claims: 'scope type=school，校前缀' },
  { key: 'classOwner', label: '班主任', reach: '本班', claims: 'scope type=class + 成员表 owner' },
  { key: 'teacher', label: '任课老师', reach: '本班', claims: 'scope type=class + 成员表 teacher' },
  { key: 'student', label: '学生', reach: '本班只读', claims: 'scope type=class，无管理角色' },
  { key: 'parent', label: '家长', reach: '本班只读', claims: 'scope type=class，无管理角色' },
]);

const CLASS_ROLES = new Set(['classOwner', 'teacher', 'student', 'parent']);
const MANAGE_ROLES = new Set(['classOwner', 'teacher']);

function cleanCode(v) {
  const s = String(v || '').trim();
  if (!s || /[\s/\\]/.test(s)) return '';
  return s;
}

/**
 * 产出可直接塞进票 / user-claims 的 claims 对象（不含 uid，由签发方填）。
 * @returns {{ok:boolean, claims?:object, error?:string}}
 */
export function rolePreset(key, ctx = {}) {
  const k = String(key || '').trim();
  if (!ROLE_KEYS.includes(k)) return { ok: false, error: '未知角色：' + k };

  const tenantId = String(ctx.tenantId || '').trim();
  if (tenantId && !TENANT_ID_RE.test(tenantId)) return { ok: false, error: 'tenantId 格式不合法（应匹配 tn_[a-z0-9_-]{2,32}）' };
  const common = tenantId ? { tenantId } : {};

  if (k === 'superAdmin') {
    // platform_admin 的跨租户豁免由 perms.isPlatform 判定：schoolId 必须缺失 / null。
    return { ok: true, claims: { ...common, schoolId: null, roles: ['platform_admin'], scopes: [] } };
  }

  if (k === 'schoolAdmin') {
    const prefix = cleanCode(ctx.schoolPrefix);
    if (!prefix) return { ok: false, error: '学校管理员必须给 schoolPrefix（校前缀，如 xlzx）' };
    // canAccess 的 school scope 是 classCode.startsWith(id + '_')，故这里不能带尾下划线。
    return { ok: true, claims: { ...common, roles: ['school_admin'], scopes: [{ type: 'school', id: prefix }] } };
  }

  if (CLASS_ROLES.has(k)) {
    const code = cleanCode(ctx.classCode);
    if (!code) return { ok: false, error: '班级级角色必须给 classCode（格式最终由 registry.isClassCodeValid 裁决）' };
    const label = { classOwner: 'class_owner', teacher: 'teacher', student: 'student', parent: 'parent' }[k];
    // roles 只是审计/界面标签；真正的写权限由 class_members 表角色裁决（见文件头）。
    return { ok: true, claims: { ...common, roles: [label], scopes: [{ type: 'class', id: code }] } };
  }
  return { ok: false, error: '角色未覆盖：' + k };
}

/** 给界面用的一句话：这个角色能碰到哪一层数据。 */
export function roleReach(key) {
  const r = ROLE_CATALOG.find((x) => x.key === key);
  return r ? r.reach : '';
}

export const DEFAULT_TENANT_ID = DEFAULT_TENANT;
export const MANAGEABLE_ROLE_KEYS = Object.freeze([...MANAGE_ROLES]);
