// registry 的租户维度单测 —— 2026-11-02 · class_id 全链路多租户
//
// 这份测试守的是：**设备归属与班级索引都带租户，两校同码不串台。**
//
// 背景（为什么非要有它）：registry 的 `classes` 是一个内存 Map，原来拿 `classCode`
// 直接当键。单校时代够用；多校之后两所学校可能都在用站点老体系的外部码
// （`class_0102`），只用 code 当键 ⇒ 后注册的学校把前一所的**整个设备列表**覆盖掉，
// 按班下发（sendToClassDevices）就会发到隔壁学校去。这类事故在管理端看不出来
// （界面只是"这个班没设备了"），但广播是真的发错了人。
//
// 跑法：node tests/unit-registry-tenant.mjs

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { initTenants, closeTenants, registerTenant, DEFAULT_TENANT } from '../src/tenant.js';
import { initClassDir, closeClassDir, resolveClassCode } from '../src/class-dir.js';
import {
  hydrateClassesFromDir, bindDeviceToClass, unbindDeviceFromClass,
  listClasses, getDevicesOfClass, dropClassIndex,
  getDeviceClassCode, getDeviceTenant, setDeviceMeta, listDevices,
} from '../src/registry.js';

let pass = 0;
let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

const tmpDirs = [];
const d = mkdtempSync(join(tmpdir(), 'regtn-'));
tmpDirs.push(d);
closeTenants();
closeClassDir();
initTenants(join(d, 'tenants.json'));
initClassDir(join(d, 'class-dir.db'));

const A = registerTenant({ name: '星璃中学' }).tenant.id;
const B = registerTenant({ name: '第二中学' }).tenant.id;

console.log('registry 租户维度单测');

// ── 1 绑班带租户，两校同码不互相覆盖 ──────────────────────────────────
ok('★ 两校绑同一个外部码 → 各自独立一份设备列表', () => {
  const a = bindDeviceToClass('class_0102', 'dev-a1', '高二(3)班', 'cloud', A);
  const b = bindDeviceToClass('class_0102', 'dev-b1', '高二(3)班', 'cloud', B);
  assert.equal(a.ok, true);
  assert.equal(b.ok, true);
  assert.deepEqual(getDevicesOfClass('class_0102', A), ['dev-a1']);
  assert.deepEqual(getDevicesOfClass('class_0102', B), ['dev-b1']);
});

ok('★ 设备归属：getDeviceClassCode / getDeviceTenant 各回到自己学校', () => {
  bindDeviceToClass('class_0102', 'dev-a2', '高二(3)班', 'cloud', A);
  assert.equal(getDeviceClassCode('dev-a2'), 'class_0102');
  assert.equal(getDeviceTenant('dev-a2'), A);
  assert.equal(getDeviceTenant('dev-b1'), B);
});

ok('★ 不传租户 → 默认组织（既有调用方行为不变）', () => {
  bindDeviceToClass('class_9999', 'dev-d1', '默认班', 'cloud');
  assert.equal(getDeviceTenant('dev-d1'), DEFAULT_TENANT);
  assert.deepEqual(getDevicesOfClass('class_9999'), ['dev-d1']);
});

ok('未知设备 → 默认租户（绝不返回 null 让调用方误判为放行）', () => {
  assert.equal(getDeviceTenant('从未见过的uid'), DEFAULT_TENANT);
  assert.equal(getDeviceClassCode('从未见过的uid'), null);
});

// ── 2 解绑只作用于本租户 ──────────────────────────────────────────────
ok('★ 解绑 A 校的设备，B 校同码班的列表不动', () => {
  unbindDeviceFromClass('class_0102', 'dev-a1', A);
  assert.deepEqual(getDevicesOfClass('class_0102', A), ['dev-a2'], 'A 校少了一台');
  assert.deepEqual(getDevicesOfClass('class_0102', B), ['dev-b1'], 'B 校必须原样');
});

ok('★ 用错租户解绑：不该动到别的学校', () => {
  // 拿 B 的租户去解 A 的设备 —— 应无效果
  unbindDeviceFromClass('class_0102', 'dev-a2', B);
  assert.ok(getDevicesOfClass('class_0102', A).includes('dev-a2'), 'A 校设备不该被 B 的解绑带走');
});

// ── 3 列表 ────────────────────────────────────────────────────────────
ok('列表：不传租户 = 全部租户（对账口径）', () => {
  const all = listClasses();
  const codes = all.map((c) => c.code);
  assert.ok(codes.includes('class_0102'));
  // 两条独立记录（两校各一条），不是一条
  const same = all.filter((c) => c.code === 'class_0102');
  assert.equal(same.length, 2, '两校同码必须是两条独立记录');
  assert.notEqual(same[0].tenantId, same[1].tenantId);
});

ok('★ 列表：传租户只列本校（管理端界面口径）', () => {
  const onlyA = listClasses(A);
  const onlyB = listClasses(B);
  assert.ok(onlyA.every((c) => c.tenantId === A));
  assert.ok(onlyB.every((c) => c.tenantId === B));
  assert.equal(onlyA.filter((c) => c.code === 'class_0102').length, 1);
  // 设备列表也跟着隔离
  const aClass = onlyA.find((c) => c.code === 'class_0102');
  assert.deepEqual(aClass.devices, ['dev-a2']);
});

// ── 4 删班索引 ────────────────────────────────────────────────────────
ok('★ 删 A 校的班索引，B 校同码班索引必须还在', () => {
  const r = dropClassIndex('class_0102', A);
  assert.equal(r.ok, true);
  assert.equal(r.existed, true);
  assert.deepEqual(getDevicesOfClass('class_0102', A), [], 'A 校索引已清');
  assert.deepEqual(getDevicesOfClass('class_0102', B), ['dev-b1'], 'B 校索引必须还在');
});

// ── 5 启动灌入 ────────────────────────────────────────────────────────
ok('★ 启动灌入：必须灌全部租户（只灌默认会让别校班级重启后消失）', () => {
  // 先往目录里登记两个非默认租户的班（不经设备，直接进目录）
  resolveClassCode({ name: '灌入A班', tenantId: A });
  resolveClassCode({ name: '灌入B班', tenantId: B });
  const n = hydrateClassesFromDir();
  assert.ok(n >= 1, '应至少灌入新班');
  const all = listClasses();
  const names = all.map((c) => c.name);
  assert.ok(names.includes('灌入A班'), 'A 校的班必须在（灌入的是全租户）');
  assert.ok(names.includes('灌入B班'), 'B 校的班必须在（灌入的是全租户）');
});

// ── 6 设备表带租户字段 ────────────────────────────────────────────────
ok('设备列表回带 tenantId（管理端据此过滤）', () => {
  bindDeviceToClass('class_0102', 'dev-a3', '高二(3)班', 'cloud', A);
  const rows = listDevices();
  const row = rows.find((r) => r.uid === 'dev-a3');
  assert.ok(row, '设备应出现在列表里');
  assert.equal(row.tenantId, A);
});

ok('setDeviceMeta 能改租户，且改了租户会重算班级索引归属', () => {
  setDeviceMeta('dev-a3', { tenantId: B });
  assert.equal(getDeviceTenant('dev-a3'), B);
});

ok('setDeviceMeta 不传 tenantId = 不改（与 role 同口径）', () => {
  setDeviceMeta('dev-a3', { name: '改个名字' });
  assert.equal(getDeviceTenant('dev-a3'), B, '只改名不该动租户');
});

closeClassDir();
closeTenants();
for (const x of tmpDirs) { try { rmSync(x, { recursive: true, force: true }); } catch { /* 忽略 */ } }

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
if (fail > 0) process.exit(1);
