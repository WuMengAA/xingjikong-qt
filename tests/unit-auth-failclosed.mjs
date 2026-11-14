// 无 claims 票据 fail-closed 回归（2026-11-15）
// 守的是：能签票 ≠ 能管设备。裸 uid 票一律零权限；只有静态运维令牌全权。
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { closeTenants, initTenants } from '../src/tenant.js';
import { identityFromAuth, canAccess, filterDevices, maskDevice } from '../src/perms.js';
import { initUserClaims, setUserClaims, deleteUserClaims, applyStoredClaims } from '../src/user-claims.js';

let pass = 0, fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log('  ok   ' + name); }
  catch (e) { fail++; console.error('  FAIL ' + name + ' :: ' + e.message); }
}
const d = mkdtempSync(join(tmpdir(), 'authclosed-'));
closeTenants();
initTenants(join(d, 'tenants.json'));
initUserClaims(join(d, 'claims.json'));

const token = { ok: true, mode: 'token' };
const bare = (uid) => ({ ok: true, mode: 'ticket', uid: uid });
const CLA = 'cls_xlzx_2028_08';
console.log('无 claims 票据 fail-closed 回归');

ok('静态运维令牌仍全权（唯一全权通道）', () => {
  const id = identityFromAuth(token);
  assert.equal(id.scopes, null);
  assert.equal(id.tenantBypass, true);
  assert.equal(canAccess(id, CLA), true);
});
ok('裸 uid 票 ⇒ scopes 是空数组，不是 null', () => {
  const id = identityFromAuth(bare('stranger'));
  assert.ok(Array.isArray(id.scopes));
  assert.equal(id.scopes.length, 0);
  assert.notEqual(id.scopes, null);
});
ok('裸 uid 票 ⇒ canAccess 全拒（有班 / 无班）', () => {
  const id = identityFromAuth(bare('stranger'));
  assert.equal(canAccess(id, CLA), false);
  assert.equal(canAccess(id, null), false);
});
ok('裸 uid 票 ⇒ 设备列表过滤空、敏感字段脱敏', () => {
  const id = identityFromAuth(bare('stranger'));
  const devs = [{ uid: 'd1', classCode: CLA, ip: '10.0.0.1', mac: 'aa' }];
  assert.equal(filterDevices(id, devs).length, 0);
  const m = maskDevice(id, devs[0]);
  assert.equal('ip' in m, false);
  assert.equal('mac' in m, false);
});
ok('正向控制：站点 claims 票按 scope 正常放行', () => {
  const id = identityFromAuth({ ok: true, mode: 'ticket', uid: 't1',
    claims: { scopes: [{ type: 'class', id: CLA }] } });
  assert.equal(canAccess(id, CLA), true);
  assert.equal(canAccess(id, 'cls_other'), false);
});
ok('正向控制：platform_admin 仍跨班放行', () => {
  const id = identityFromAuth({ ok: true, mode: 'ticket', uid: 'p1',
    claims: { schoolId: null, roles: ['platform_admin'], scopes: [] } });
  assert.equal(canAccess(id, CLA), true);
});
ok('白名单回填：user-claims 登记过的 uid 恢复权限', () => {
  const r = setUserClaims('teacher1', { scopes: [{ type: 'class', id: CLA }] }, 'test');
  assert.equal(r.ok, true);
  const id = identityFromAuth(applyStoredClaims(bare('teacher1')));
  assert.equal(canAccess(id, CLA), true);
});
ok('删授权不放大：delete 后同一 uid 回到零权限', () => {
  deleteUserClaims('teacher1');
  const id = identityFromAuth(applyStoredClaims(bare('teacher1')));
  assert.equal(canAccess(id, CLA), false);
});
ok('票内 claims 优先：回填不覆盖站点真源', () => {
  setUserClaims('t2', { scopes: [{ type: 'class', id: 'cls_wrong' }] }, 'test');
  const auth = applyStoredClaims({ ok: true, mode: 'ticket', uid: 't2',
    claims: { scopes: [{ type: 'class', id: CLA }] } });
  assert.equal(canAccess(identityFromAuth(auth), CLA), true);
});

rmSync(d, { recursive: true, force: true });
closeTenants();
console.log('PASS ' + pass + ' / FAIL ' + fail);
if (fail) process.exit(1);
