// 多租户（tenant）单测 —— 2026-11-02 · class_id 全链路隔离
//
// 这份测试守的是一条铁律：**班名在两所学校之间必然重复，而它们必须是两个班。**
//
// 只要它红了，就意味着下面任意一件事发生了：
//   · 默认租户的派生算法被改了（⇒ 升级当刻全部存量班级码变化、已绑设备归属跳变）
//   · lookup 反向索引又变回全局的（⇒ 两校各一个「1班」互相顶掉指针）
//   · 租户缺省值从「默认租户」变成了「放行一切」（⇒ 存量数据当场越权）
//
// 跑法：node tests/unit-tenant.mjs

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  DEFAULT_TENANT, initTenants, closeTenants, registerTenant, listTenants, tenantCount,
  deriveTenantId, tenantSlug, normalizeTenantId, sameTenant, isTenantUsable,
  setTenantDisabled, getTenant, tenantBypass,
} from '../src/tenant.js';
import {
  initClassDir, closeClassDir, deriveClassCode, resolveClassCode,
  registerExternalCode, getClass, listClassDir, listClassDirAll, classDirSize,
  tenantOfClass, renameClass, deleteClass,
} from '../src/class-dir.js';

let pass = 0;
let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

const tmpDirs = [];
function freshDir() {
  const d = mkdtempSync(join(tmpdir(), 'tenant-'));
  tmpDirs.push(d);
  return d;
}

console.log('多租户（tenant）单测');

// ── 1 租户身份本身 ────────────────────────────────────────────────────
{
  const d = freshDir();
  closeTenants();
  const r = initTenants(join(d, 'tenants.json'));
  ok('初始化：首次启动即自动建出默认租户（存量数据落点）', () => {
    assert.equal(r.ok, true);
    assert.equal(r.loaded, 0);
    assert.equal(tenantCount(), 1);
    assert.ok(getTenant(DEFAULT_TENANT), '默认租户必须存在');
    assert.equal(getTenant(DEFAULT_TENANT).disabled, false);
  });

  ok('派生：同名永远同 id；纯中文名退回哈希（稳定）', () => {
    assert.equal(deriveTenantId('星璃中学'), deriveTenantId('星璃中学'));
    assert.match(deriveTenantId('星璃中学'), /^tn_[a-z0-9_-]{2,32}$/);
    assert.equal(deriveTenantId('Stelarith High'), 'tn_stelarith-high');
    assert.equal(deriveTenantId(''), '');
  });

  ok('注册：同名幂等，不重复建（created=false）', () => {
    const a = registerTenant({ name: '星璃中学' });
    assert.equal(a.ok, true);
    assert.equal(a.created, true);
    const b = registerTenant({ name: '星璃中学' });
    assert.equal(b.ok, true);
    assert.equal(b.created, false);
    assert.equal(a.tenant.id, b.tenant.id);
    assert.equal(tenantCount(), 2);
  });

  ok('注册：非法 id 被拒（不会污染租户表）', () => {
    const bad = registerTenant({ name: 'x', id: 'BAD ID!' });
    assert.equal(bad.ok, false);
  });

  ok('归一化：缺省/非法/未登记 → 一律落默认租户（不是放行一切）', () => {
    assert.equal(normalizeTenantId(undefined), DEFAULT_TENANT);
    assert.equal(normalizeTenantId(''), DEFAULT_TENANT);
    assert.equal(normalizeTenantId('not-a-tenant'), DEFAULT_TENANT);
    assert.equal(normalizeTenantId('tn_nonexistent_zzz'), DEFAULT_TENANT);
  });

  ok('判据：sameTenant 认默认租户的多种写法为同一租户', () => {
    assert.equal(sameTenant(undefined, DEFAULT_TENANT), true);
    assert.equal(sameTenant(DEFAULT_TENANT, DEFAULT_TENANT), true);
    // ⚠️ 未登记的 id 会被 normalize 归一到默认租户 —— 所以这里必须先真注册再断言。
    //    这正是「非法/未登记一律落默认租户」那条设计的结果，不是 bug。
    const other = registerTenant({ name: 'Stelarith High' }).tenant.id;
    assert.equal(other, 'tn_stelarith-high');
    assert.equal(sameTenant(other, other), true);
    assert.equal(sameTenant(other, DEFAULT_TENANT), false);
    assert.equal(sameTenant('tn_not_registered_xx', DEFAULT_TENANT), true, '未登记 ⇒ 落默认租户');
  });

  ok('停用：默认租户不可停用；普通租户可停可恢复', () => {
    const r1 = setTenantDisabled(DEFAULT_TENANT, true);
    assert.equal(r1.ok, false);
    const other = registerTenant({ name: '第二中学' }).tenant.id;
    assert.equal(isTenantUsable(other), true);
    assert.equal(setTenantDisabled(other, true).ok, true);
    assert.equal(isTenantUsable(other), false);
    assert.equal(setTenantDisabled(other, false).ok, true);
    assert.equal(isTenantUsable(other), true);
  });

  ok('落盘：损坏的 tenants.json 不崩，退化为只有默认租户', () => {
    const bad = join(freshDir(), 'tenants.json');
    writeFileSync(bad, '{ 这不是 JSON', 'utf8');
    closeTenants();
    const rr = initTenants(bad);
    assert.equal(rr.ok, true);
    assert.equal(tenantCount(), 1);
    assert.ok(getTenant(DEFAULT_TENANT));
  });

  ok('tenantBypass：跨租户是一个具名调用（可被 grep 审计）', () => {
    const b = tenantBypass('platform_admin 对账');
    assert.equal(b.bypass, true);
    assert.equal(b.reason, 'platform_admin 对账');
  });
}

// ── 2 班级目录的租户隔离（本文件最重要的一节）──────────────────────────
{
  const d = freshDir();
  closeTenants();
  initTenants(join(d, 'tenants.json'));
  const A = registerTenant({ name: '星璃中学' }).tenant.id;
  const B = registerTenant({ name: '第二中学' }).tenant.id;

  closeClassDir();
  initClassDir(join(d, 'class-dir.db'));

  ok('★ 默认租户派生算法一字未改（升级不跳变的唯一保证）', () => {
    // 与 unit-class-dir.mjs 的既有断言同口径：cls-<sha256(name) 前 10>
    const code = deriveClassCode('高一(1)班');
    assert.match(code, /^cls-[0-9a-f]{10}$/);
    // 显式传默认租户 == 不传
    assert.equal(deriveClassCode('高一(1)班', DEFAULT_TENANT), code);
    // 非默认租户必须不同（否则「两校 1 班」还是同一个码）
    assert.notEqual(deriveClassCode('高一(1)班', A), code);
    assert.notEqual(deriveClassCode('高一(1)班', A), deriveClassCode('高一(1)班', B));
  });

  ok('★ 两所学校各有一个「1班」→ 两个不同的码（不串台）', () => {
    const ra = resolveClassCode({ name: '1班', source: 'cloud', tenantId: A });
    const rb = resolveClassCode({ name: '1班', source: 'cloud', tenantId: B });
    assert.equal(ra.ok, true);
    assert.equal(rb.ok, true);
    assert.notEqual(ra.code, rb.code, '两校同名班拿到了同一个码 = 串台');
    assert.equal(ra.name, '1班');
    assert.equal(rb.name, '1班');
  });

  ok('★ lookup 按租户隔离：A 校的班名不会把 B 校顶掉', () => {
    // 再解析一次，必须各自稳定回到自己的码
    const ra2 = resolveClassCode({ name: '1班', source: 'cloud', tenantId: A });
    const rb2 = resolveClassCode({ name: '1班', source: 'cloud', tenantId: B });
    assert.equal(ra2.code, resolveClassCode({ name: '1班', tenantId: A }).code);
    assert.equal(rb2.code, resolveClassCode({ name: '1班', tenantId: B }).code);
    assert.notEqual(ra2.code, rb2.code);
  });

  ok('查询：getClass 跨租户查不到（这是设计意图）', () => {
    const ra = resolveClassCode({ name: '1班', tenantId: A });
    assert.ok(getClass(ra.code, A), '本租户应能查到');
    assert.equal(getClass(ra.code, B), null, '别校不该查到');
    assert.equal(getClass(ra.code), null, '缺省 = 默认租户；A 不是默认租户 ⇒ 查不到');
  });

  ok('列表：listClassDir 只返回本租户的班', () => {
    assert.equal(listClassDir(A).length, 1);
    assert.equal(listClassDir(B).length, 1);
    assert.ok(listClassDirAll().length >= 2, '对账用全量接口应看到全部');
    assert.equal(classDirSize(A), 1);
    assert.equal(classDirSize(B), 1);
  });

  ok('归属：tenantOfClass 能反查班属于哪所学校（perms 判定入口）', () => {
    const ra = resolveClassCode({ name: '1班', tenantId: A });
    assert.equal(tenantOfClass(ra.code), A);
    assert.equal(tenantOfClass('cls-doesnotexist'), null);
  });

  ok('改名：本租户内重名被拒；隔壁学校同名不算冲突', () => {
    const ra = resolveClassCode({ name: '1班', tenantId: A });
    const rc = resolveClassCode({ name: '2班', tenantId: A });
    const bad = renameClass(rc.code, '1班', A);
    assert.equal(bad.ok, false, '同租户内重名必须拒');
    // B 校已有「1班」，A 校把「2班」改名成「1班」不行（A 自己也已有 1班）；换个不冲突的名字
    const good = renameClass(rc.code, '实验班', A);
    assert.equal(good.ok, true);
    assert.equal(getClass(rc.code, A).name, '实验班');
  });

  ok('外部码：同一 class_id 在两校是两条独立记录（码相同、归属不同）', () => {
    // ⚠️ 注意语义：外部码（站点 class_id）本身是**同一个字符串**，两校拿到的 canonical 也就相同。
    //    真正要多租户保证的是「两条独立记录」：各自的 name/aliases/归属互不影响，
    //    删一家不带走另一家。（早先我以为该派生出两个不同码，那是把「派生码」和
    //    「外部码」混为一谈了 —— 外部码必须原样保留，不能改写。）
    const ea = registerExternalCode('class_0102', '高二(3)班', 'site', A);
    const eb = registerExternalCode('class_0102', '高二(3)班', 'site', B);
    assert.equal(ea.ok, true);
    assert.equal(eb.ok, true);
    assert.equal(ea.code, 'class_0102', '外部码必须原样保留（对账键，一改就断）');
    assert.equal(eb.code, 'class_0102');
    const ra = getClass('class_0102', A);
    const rb = getClass('class_0102', B);
    assert.ok(ra && rb, '两校都应查得到');
    assert.notEqual(ra, rb, '必须是两条独立记录，不能是同一个对象');
    assert.equal(tenantOfClass('class_0102', A), A);
    assert.equal(tenantOfClass('class_0102', B), B);
    // 歧义判据：不带租户提示时，跨校重码必须返回 null（不许瞎猜一个）
    assert.equal(tenantOfClass('class_0102'), null, '跨校重码 ⇒ 不给猜，返回 null');
  });

  ok('删除：删 A 校的班不影响 B 校同码班', () => {
    const ea = registerExternalCode('class_0103', '高三(1)班', 'site', A);
    const eb = registerExternalCode('class_0103', '高三(1)班', 'site', B);
    const before = listClassDir(B).length;
    assert.equal(deleteClass(ea.code, A).ok, true);
    assert.equal(listClassDir(B).length, before, 'B 校的班数不该变');
    assert.ok(getClass(eb.code, B), 'B 校的班必须还在');
  });

  ok('兼容：不传租户的既有调用一律走默认租户（存量代码零改动）', () => {
    const r1 = resolveClassCode({ name: '默认班' });
    assert.equal(r1.ok, true);
    assert.equal(tenantOfClass(r1.code), DEFAULT_TENANT);
    assert.ok(getClass(r1.code));
    assert.ok(listClassDir().some((c) => c.code === r1.code));
  });

  closeClassDir();
}

// ── 3 存量库迁移（老表没有 tenant_id 列）──────────────────────────────
{
  const d = freshDir();
  const dbFile = join(d, 'class-dir.db');

  // 先用「老 schema」造一个库：完全没有 tenant_id 列
  const { DatabaseSync } = await import('node:sqlite');
  const old = new DatabaseSync(dbFile);
  old.exec(`
    CREATE TABLE classes (
      code       TEXT PRIMARY KEY,
      name       TEXT NOT NULL DEFAULT '',
      aliases    TEXT NOT NULL DEFAULT '[]',
      source     TEXT NOT NULL DEFAULT '',
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    );
  `);
  const now = Date.now();
  old.prepare('INSERT INTO classes (code,name,aliases,source,created_at,updated_at) VALUES (?,?,?,?,?,?)')
    .run('cls-aabbccddee', '1班', '[]', 'cloud', now, now);
  old.close();

  closeTenants();
  initTenants(join(d, 'tenants.json'));
  closeClassDir();
  const r = initClassDir(dbFile);

  ok('★ 存量库自动迁移：老表补 tenant_id 列，已有行落默认租户', () => {
    assert.equal(r.ok, true);
    const rec = getClass('cls-aabbccddee');
    assert.ok(rec, '老数据必须还在');
    assert.equal(rec.name, '1班');
    assert.equal(rec.tenantId, DEFAULT_TENANT, '存量数据必须落默认租户');
    assert.equal(tenantOfClass('cls-aabbccddee'), DEFAULT_TENANT);
    assert.ok(listClassDir(DEFAULT_TENANT).some((c) => c.code === 'cls-aabbccddee'));
  });

  ok('迁移后：默认租户派生码不变（老设备绑定不会跳变）', () => {
    assert.equal(deriveClassCode('1班', DEFAULT_TENANT), deriveClassCode('1班'));
  });

  closeClassDir();
}

// ── 收尾 ──────────────────────────────────────────────────────────────
closeClassDir();
closeTenants();
for (const d of tmpDirs) { try { rmSync(d, { recursive: true, force: true }); } catch { /* 忽略 */ } }

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
if (fail > 0) process.exit(1);
