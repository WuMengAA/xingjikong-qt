// 六角色预设单测（2026-11-15）
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { closeTenants, initTenants, registerTenant } from '../src/tenant.js';
import {
  ROLE_KEYS, ROLE_CATALOG, rolePreset, roleReach, MANAGEABLE_ROLE_KEYS, DEFAULT_TENANT_ID,
} from '../src/class-roles.js';
import { identityFromAuth, canAccess, maskDevice } from '../src/perms.js';

let pass = 0, fail = 0;
function ok(name, cond) { if (cond) { pass++; } else { fail++; console.error('FAIL  ' + name); } }
function identOf(key, ctx, uid) {
  const r = rolePreset(key, ctx);
  if (!r.ok) throw new Error(r.error);
  return identityFromAuth({ ok: true, mode: 'ticket', uid, claims: r.claims });
}

// 既有真源语义：未注册的 tenantId 会回落到默认租户（tenant.js normalizeTenantId）。
// 因此跨租户断言必须先把两个租户真正注册进表里，不能用硬写的字符串假装有两所学校。
const d = mkdtempSync(join(tmpdir(), 'classroles-'));
closeTenants();
initTenants(join(d, 'tenants.json'));
const T = registerTenant({ name: '星璃中学' }).tenant.id;
const B = registerTenant({ name: '第二中学' }).tenant.id;

ok('六角色齐全', ROLE_KEYS.length === 6 && ROLE_CATALOG.length === 6);
ok('目录与常量一致', ROLE_CATALOG.every((x) => ROLE_KEYS.includes(x.key)));
ok('默认租户是 tn_default', DEFAULT_TENANT_ID === 'tn_default');
ok('可管理角色只有班主任/任课老师', JSON.stringify(MANAGEABLE_ROLE_KEYS) === JSON.stringify(['classOwner', 'teacher']));
ok('未知角色 fail-closed', rolePreset('hacker').ok === false);
ok('班级级缺 classCode ⇒ 拒', rolePreset('teacher', {}).ok === false);
ok('学校级缺 schoolPrefix ⇒ 拒', rolePreset('schoolAdmin', {}).ok === false);
ok('非法 tenantId ⇒ 拒', rolePreset('teacher', { classCode: 'class_a_01', tenantId: 'BAD' }).ok === false);
ok('classCode 带斜杠 ⇒ 拒', rolePreset('teacher', { classCode: '../class_b' }).ok === false);

const CODE = 'class_xlzx_2028_08';
const OTHER = 'class_xlzx_2028_09';
const ctx = { tenantId: T, classCode: CODE, schoolPrefix: 'class_xlzx' };

const sup = rolePreset('superAdmin', ctx);
ok('超管：platform_admin + schoolId=null + 空 scopes', sup.ok === true && sup.claims.roles.includes('platform_admin') && sup.claims.schoolId === null && sup.claims.scopes.length === 0);
const sch = rolePreset('schoolAdmin', ctx);
ok('校管：school scope 且不带尾下划线', sch.ok === true && sch.claims.scopes[0].type === 'school' && !sch.claims.scopes[0].id.endsWith('_'));
const own = rolePreset('classOwner', ctx);
ok('班主任：class scope 精确等于班码', own.ok === true && own.claims.scopes[0].id === CODE);
const tea = rolePreset('teacher', ctx);
ok('任课老师：不带 platform_admin', tea.ok === true && !tea.claims.roles.includes('platform_admin'));
const stu = rolePreset('student', ctx);
const par = rolePreset('parent', ctx);
ok('学生/家长：无管理角色标签', !stu.claims.roles.includes('class_owner') && !par.claims.roles.includes('teacher'));
ok('学生/家长：scopes 仍非空数组（不是 null 全权）', Array.isArray(stu.claims.scopes) && stu.claims.scopes !== null && Array.isArray(par.claims.scopes));

// ★ 关键：预设必须经得起既有判定真源的检验（不是文档里写写而已）
const iTea = identOf('teacher', ctx, 't1');
ok('任课老师能看本班', canAccess(iTea, CODE, { tenantId: T }) === true);
ok('任课老师看不到别班', canAccess(iTea, OTHER, { tenantId: T }) === false);
ok('任课老师跨租户被拒', canAccess(iTea, CODE, { tenantId: B }) === false);
const iSup = identOf('superAdmin', ctx, 'root');
ok('超管跨租户放行（isPlatform 唯一豁免）', canAccess(iSup, OTHER, { tenantId: B }) === true);
const iSch = identOf('schoolAdmin', ctx, 's1');
ok('校管能看本校前缀下的班', canAccess(iSch, OTHER, { tenantId: T }) === true);
ok('校管看不到外校的班', canAccess(iSch, 'class_other_2028_08', { tenantId: T }) === false);
ok('受限身份设备字段被脱敏', maskDevice(iTea, { uid: 'd1', ip: '10.0.0.2', mac: 'aa:bb' }).ip === undefined && maskDevice(iTea, { uid: 'd1', ip: '10.0.0.2' }).uid === 'd1');
ok('roleReach 对未知角色返回空串', roleReach('hacker') === '');

closeTenants();
rmSync(d, { recursive: true, force: true });
console.log('PASS ' + pass + ' / FAIL ' + fail);
if (fail) process.exit(1);
