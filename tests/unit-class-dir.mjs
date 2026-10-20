// 班级目录单测（2026-10-20 · 班级真源统一）
//
// 这份测试守的是一条铁律：**同一个班只有一个 canonical 码**。
// 只要它红了，就意味着"云端派生的 cls-xxx"与"站点 class_id"又开始各写各的了 ——
// 那是曾经导致同一个班长出两套码、重启后再长出第三套的根因。
//
// 跑法：node tests/unit-class-dir.mjs

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  initClassDir, closeClassDir, deriveClassCode, resolveClassCode,
  registerExternalCode, getClass, listClassDir, classDirSize,
} from '../src/class-dir.js';

let pass = 0;
let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

const dirs = [];
function freshDb() {
  const d = mkdtempSync(join(tmpdir(), 'clsdir-'));
  dirs.push(d);
  closeClassDir();
  return join(d, 'class-dir.db');
}

console.log('班级目录（class-dir）单测');

// ── 1 派生规则 ────────────────────────────────────────────────────────
ok('派生：同名永远同码，且符合 6-16 位码格式', () => {
  const a = deriveClassCode('高一(1)班');
  const b = deriveClassCode('高一(1)班');
  assert.equal(a, b);
  assert.match(a, /^cls-[0-9a-f]{10}$/);
  assert.ok(a.length >= 6 && a.length <= 16, `长度越界: ${a}`);
});

ok('派生：空名不给码（调用方必须自己决定怎么报错）', () => {
  assert.equal(deriveClassCode(''), '');
  assert.equal(deriveClassCode('   '), '');
});

// ── 2 基本解析 ────────────────────────────────────────────────────────
initClassDir(freshDb());

ok('解析：新班名 → 派生 cls- 码并登记', () => {
  const r = resolveClassCode({ name: '1班', source: 'cloud' });
  assert.equal(r.ok, true);
  assert.match(r.code, /^cls-/);
  assert.equal(r.name, '1班');
  assert.equal(classDirSize(), 1);
});

ok('解析：同名（含大小写/多余空格）复用同一个码', () => {
  const first = resolveClassCode({ name: '1班' }).code;
  for (const variant of ['1班', ' 1班 ', '1班\t']) {
    assert.equal(resolveClassCode({ name: variant }).code, first, `变体「${variant}」派生了新码`);
  }
  assert.equal(classDirSize(), 1);
});

ok('解析：已知 code 直接归一，不新建', () => {
  const code = resolveClassCode({ name: '2班' }).code;
  const r = resolveClassCode({ code });
  assert.equal(r.code, code);
  assert.equal(classDirSize(), 2);
});

ok('解析：非法输入明确失败（不静默造码）', () => {
  const r = resolveClassCode({});
  assert.equal(r.ok, false);
  assert.ok(r.error);
});

// ── 3 两套体系合并（本文件存在的理由）──────────────────────────────────
ok('合并：站点码先到 → 云端同名派生归一到站点码（canonical 不让位）', () => {
  closeClassDir();
  initClassDir(freshDb());

  const site = registerExternalCode('class_01', '1班', 'site');
  assert.equal(site.ok, true);
  assert.equal(site.code, 'class_01');
  assert.equal(site.merged, false);

  // 向导/手机页这时用班名绑 —— 必须命中站点那个班，不能新长一个 cls-xxx
  const r = resolveClassCode({ name: '1班', source: 'device' });
  assert.equal(r.code, 'class_01', '同名班被拆成了两个码');
  assert.equal(classDirSize(), 1);

  // 云端派生的码若被人显式拿来用，也应归一到 class_01
  const derived = deriveClassCode('1班');
  const r2 = resolveClassCode({ code: derived, name: '1班' });
  assert.equal(r2.code, 'class_01');
});

ok('合并：云端码先到 → 站点 class_01 降级成 alias，canonical 保持不动', () => {
  closeClassDir();
  initClassDir(freshDb());

  const cloud = resolveClassCode({ name: '2班', source: 'cloud' });
  assert.match(cloud.code, /^cls-/);

  const site = registerExternalCode('class_02', '2班', 'site');
  assert.equal(site.code, cloud.code, 'canonical 被后来的外部码抢走了（已绑设备会跳班）');
  assert.equal(site.merged, true);

  // 站点码从此能反查到同一个班
  assert.equal(getClass('class_02').code, cloud.code);
  assert.deepEqual(getClass(cloud.code).aliases, ['class_02']);
  assert.equal(classDirSize(), 1);

  // 再按站点码绑定，仍然写 canonical
  assert.equal(resolveClassCode({ code: 'class_02' }).code, cloud.code);
});

// ── 4 落盘（跨重启稳定）──────────────────────────────────────────────
ok('落盘：关掉再打开，码/名/别名原样恢复', () => {
  const file = freshDb();
  initClassDir(file);
  const a = resolveClassCode({ name: '3班', source: 'cloud' });
  registerExternalCode('class_03', '3班', 'site');
  const before = listClassDir();
  assert.equal(before.length, 1);

  closeClassDir();
  initClassDir(file);

  assert.equal(classDirSize(), 1);
  const after = getClass(a.code);
  assert.ok(after, '重启后班级消失了');
  assert.equal(after.name, '3班');
  assert.deepEqual(after.aliases, ['class_03']);
  // 重启后同名解析必须还给同一个码（否则每次重启都派生新码）
  assert.equal(resolveClassCode({ name: '3班' }).code, a.code);
  assert.equal(resolveClassCode({ code: 'class_03' }).code, a.code);
});

ok('落盘：目录条目结构完整（对账端依赖这些字段）', () => {
  const [rec] = listClassDir();
  for (const k of ['code', 'name', 'aliases', 'source', 'createdAt', 'updatedAt']) {
    assert.ok(k in rec, `缺字段 ${k}`);
  }
  assert.ok(Array.isArray(rec.aliases));
});

// ── 5 边界 ───────────────────────────────────────────────────────────
ok('边界：外部码非法格式被拒绝，不会污染目录', () => {
  const n = classDirSize();
  assert.equal(registerExternalCode('', 'x').ok, false);
  assert.equal(classDirSize(), n);
});

ok('边界：只有 code 没有名字时，用 code 自己登记为 canonical', () => {
  const r = resolveClassCode({ code: 'class_99', source: 'device' });
  assert.equal(r.ok, true);
  assert.equal(r.code, 'class_99');
});

ok('边界：名字撞上已有别名时不新造班', () => {
  const n = classDirSize();
  // class_03 是 3班的 alias，用它的名字再解析不应新增
  const r = resolveClassCode({ name: '3班' });
  assert.equal(classDirSize(), n);
  assert.ok(r.code.startsWith('cls-'));
});

// ── 收尾 ─────────────────────────────────────────────────────────────
closeClassDir();
for (const d of dirs) { try { rmSync(d, { recursive: true, force: true }); } catch { /* 忽略 */ } }

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
process.exit(fail === 0 ? 0 : 1);
