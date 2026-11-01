// 权限消费层的租户维度单测 —— 2026-11-02 · class_id 全链路多租户
//
// 这份测试守的是：**先判租户，再判 scope。**
//
// 最容易写错的地方（也是它存在的理由）：单校时代只比 classCode 就够，多校之后
// 两所学校可能用同一个外部码（站点老体系 class_0102 两校都在用），只比码 = 把
// 隔壁学校的班一起放行。下面「★」那几条就是这个回归的哨兵。
//
// 跑法：node tests/unit-perms-tenant.mjs

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { initTenants, closeTenants, registerTenant, DEFAULT_TENANT } from '../src/tenant.js';
import { identityFromAuth, canAccess, filterDevices } from '../src/perms.js';

let pass = 0;
let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

const tmpDirs = [];
{
  const d = mkdtempSync(join(tmpdir(), 'perms-'));
  tmpDirs.push(d);
  closeTenants();
  initTenants(join(d, 'tenants.json'));
}
const A = registerTenant({ name: '星璃中学' }).tenant.id;
const B = registerTenant({ name: '第二中学' }).tenant.id;

/** 造一个「受限身份」：有 scopes、指定租户。 */
function identOf(tenantId, scopes, extra = {}) {
  return {
    uid: 'u1', tenantId, roles: [], scopes, tenantBypass: false, ...extra,
  };
}

console.log('权限消费层的租户维度单测');

// ── 1 identityFromAuth 怎么取租户 ──────────────────────────────────────
ok('身份：静态令牌 → 全权 + 默认租户 + bypass 标记', () => {
  const id = identityFromAuth({ ok: true, mode: 'token' });
  assert.ok(id);
  assert.equal(id.scopes, null);
  assert.equal(id.tenantId, DEFAULT_TENANT);
  assert.equal(id.tenantBypass, true);
});

ok('身份：未鉴权 → null（后续一律拒）', () => {
  assert.equal(identityFromAuth(null), null);
  assert.equal(identityFromAuth({ ok: false }), null);
});

ok('身份：claims.tenantId 优先（站点已完成租户解析）', () => {
  const id = identityFromAuth({ ok: true, mode: 'ticket', uid: 'u9', claims: { tenantId: A, scopes: [] } });
  assert.equal(id.tenantId, A);
});

ok('身份：只有 schoolId 且它本身就是合法租户 id → 直接用', () => {
  const id = identityFromAuth({ ok: true, mode: 'ticket', uid: 'u9', claims: { schoolId: B, scopes: [] } });
  assert.equal(id.tenantId, B);
});

ok('身份：老票只有学校名 schoolId → 由名字派生租户 id，查不到就落默认', () => {
  // 「星璃中学」已注册成 A
  const hit = identityFromAuth({ ok: true, mode: 'ticket', uid: 'u9', claims: { schoolId: '星璃中学', scopes: [] } });
  assert.equal(hit.tenantId, A, '学校名应能解析回已注册租户');
  // 陌生学校名 → 不创造租户，落默认
  const miss = identityFromAuth({ ok: true, mode: 'ticket', uid: 'u9', claims: { schoolId: '从没见过的中学', scopes: [] } });
  assert.equal(miss.tenantId, DEFAULT_TENANT);
});

ok('身份：完全没有租户信息 → 默认组织（存量票兼容）', () => {
  const id = identityFromAuth({ ok: true, mode: 'ticket', uid: 'u9', claims: { scopes: [{ type: 'class', id: 'cls-aaaaaaaaaa' }] } });
  assert.equal(id.tenantId, DEFAULT_TENANT);
});

// ── 2 ★ 租户闸先于 scope 判定 ─────────────────────────────────────────
ok('★ 跨租户：scope 明明覆盖了那个班，也必须拒', () => {
  const idA = identOf(A, [{ type: 'class', id: 'class_0102' }]);
  // 同校：放行
  assert.equal(canAccess(idA, 'class_0102', { tenantId: A }), true);
  // 跨校：同一个码，但目标属于 B → 必须拒
  assert.equal(canAccess(idA, 'class_0102', { tenantId: B }), false, '跨校同码被放行 = 串台');
});

ok('★ 跨租户：school 级 scope 也不能越过租户边界', () => {
  const idA = identOf(A, [{ type: 'school', id: 'xlzx' }]);
  assert.equal(canAccess(idA, 'xlzx_2028_08', { tenantId: A }), true);
  assert.equal(canAccess(idA, 'xlzx_2028_08', { tenantId: B }), false);
});

ok('★ 不传目标租户 → 退回原 scope 逻辑（向后兼容，不是新漏洞）', () => {
  const idA = identOf(A, [{ type: 'class', id: 'class_0102' }]);
  assert.equal(canAccess(idA, 'class_0102'), true, '无租户信息时按原口径判');
  assert.equal(canAccess(idA, 'class_9999'), false);
});

ok('全权身份跨租户仍放行（运维要能排障，且已标 bypass）', () => {
  const full = { uid: null, tenantId: DEFAULT_TENANT, roles: [], scopes: null, tenantBypass: true };
  assert.equal(canAccess(full, 'class_0102', { tenantId: B }), true);
});

ok('平台超管跨租户仍放行（platform_admin 定义上就是跨校角色）', () => {
  const plat = { uid: 'p', tenantId: DEFAULT_TENANT, roles: ['platform_admin'], scopes: [], schoolId: null };
  assert.equal(canAccess(plat, 'class_0102', { tenantId: B }), true);
});

ok('未鉴权恒拒（带不带租户都一样）', () => {
  assert.equal(canAccess(null, 'class_0102', { tenantId: A }), false);
  assert.equal(canAccess(null, 'class_0102'), false);
});

ok('无班级归属的设备谁都不该碰（受限身份）', () => {
  const idA = identOf(A, [{ type: 'class', id: 'class_0102' }]);
  assert.equal(canAccess(idA, null, { tenantId: A }), false);
  assert.equal(canAccess(idA, '', { tenantId: A }), false);
});

// ── 3 列表过滤 ────────────────────────────────────────────────────────
ok('★ 列表：两校同码设备只漏本站那台', () => {
  const idA = identOf(A, [{ type: 'class', id: 'class_0102' }]);
  const devices = [
    { uid: 'a1', classCode: 'class_0102', tenantId: A },
    { uid: 'b1', classCode: 'class_0102', tenantId: B },   // 隔壁学校同码
    { uid: 'a2', classCode: 'class_9999', tenantId: A },   // 本校别的班，scope 不覆盖
  ];
  const visible = filterDevices(idA, devices);
  assert.deepEqual(visible.map((d) => d.uid), ['a1']);
});

ok('列表：全权身份原样返回（不过滤）', () => {
  const full = { uid: null, tenantId: DEFAULT_TENANT, roles: [], scopes: null };
  const devices = [{ uid: 'x', classCode: 'c1', tenantId: B }];
  assert.equal(filterDevices(full, devices).length, 1);
});

ok('列表：自定义取值器（设备对象字段名不同也能用）', () => {
  const idA = identOf(A, [{ type: 'class', id: 'cls-x' }]);
  const devices = [
    { uid: 'a1', meta: { cls: 'cls-x', tn: A } },
    { uid: 'b1', meta: { cls: 'cls-x', tn: B } },
  ];
  const visible = filterDevices(idA, devices, (d) => d.meta.cls, (d) => d.meta.tn);
  assert.deepEqual(visible.map((d) => d.uid), ['a1']);
});

closeTenants();
for (const d of tmpDirs) { try { rmSync(d, { recursive: true, force: true }); } catch { /* 忽略 */ } }

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
if (fail > 0) process.exit(1);
