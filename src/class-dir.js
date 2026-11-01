// 班级目录（Class Directory）—— **班级身份的唯一真源**（2026-10-20）
//
// ── 为什么要有这个文件 ────────────────────────────────────────────────
// 在这之前「一个班」的身份有两个互不相识的发行方：
//   ① 云端 `deriveClassCode(name)` → `cls-<sha256(name) 前10>`（OOBE 向导 / 二维码绑定走这里）
//   ② 站点 `stelarith_classes.class_id` → `class_01`（老配置窗「接入码 XJK-…」走站点 activate）
//      ⚠️ 2026-10-25 起②那条**发码路已废**（接入码收归我方，见 class-admin.js 的
//      adminIssueBindCode）：不再有新的 site 码进来。`source:'site'` 只留作历史数据的
//      归一依据，别当成还在用的活路径。
// 两条路各写各的键 ⇒ 同一个班能长出两个码；而 registry.js 的 classes 又只是**内存 Map**，
// 云端一重启班级名与归属关系全丢 ⇒ 重启后再绑定会派生出**第三套**码。三重漂移。
//
// 这里把它们收成一张表：
//   · `code`    ：canonical（对外唯一的班级键，写进 registry / 设备 classCode / perms 判定）
//   · `name`    ：显示名（如「1班」），做**同班判定**的第一依据
//   · `aliases` ：同一班的其他写法（外部体系码、历史码、名校验变体），只解析用，不外发
//   · `source`  ：谁是第一个把这个班登记进来的（cloud / site / admin / device）
//   · `tenantId`：**哪所学校**（2026-11-02 多租户）。班名在全校唯一、跨校必然重复
//                 （两所学校都有「1班」），没有这一维 ⇒ 名单/课表/考勤/设备全部串台。
//                 缺省一律 `tn_default`（存量数据落点），见 tenant.js。
//
// 纪律（改这个文件前先读）：
//   · **canonical 一旦定下就不再改**。让位给后来的码 = 已绑定设备的归属当场跳变，
//     这是比"两个码"更严重的生产事故。后来的码一律降级成 alias。
//   · 所有绑班入口必须过 `resolveClassCode()`，**不许在别处再写一份派生逻辑**——
//     漂移的根因就是"同一个判定写了两遍"。
//   · 落盘用 node:sqlite（与 store.js / interact.js 同款，零新增依赖），
//     因为身份必须跨重启稳定；纯内存 Map 做不到。

import { DatabaseSync } from 'node:sqlite';
import { createHash } from 'node:crypto';
import { CLASS_DIR_DB_FILE } from './config.js';
import { DEFAULT_TENANT, normalizeTenantId, sameTenant } from './tenant.js';

/** canonical 码的派生前缀：名字 → `cls-<10 hex>`（14 字符，符合 isClassCodeValid 的 6-16 位） */
const DERIVED_PREFIX = 'cls-';

let db = null;
/**
 * 内存镜像：`${tenantId}\0${code}` → { code, name, aliases, source, tenantId, createdAt, updatedAt }。
 * 读多写少，全量缓存。
 *
 * ⚠️ 2026-11-02 多租户：键**必须带租户**。原来只有 `code` 一个键时，两所学校登记同一个
 *    外部码（站点老体系 `class_0102` 两校都在用）会互相覆盖 —— B 校写进去就把 A 校顶掉，
 *    再解绑/改名/删班全作用到隔壁学校那条记录上。这是 `registerExternalCode` 的必然碰撞，
 *    不是偶发。
 */
const dir = new Map();
/** 反向索引：任意写法（code / alias / name）→ canonical code。O(1) 解析，避免每次全表扫。 */
const lookup = new Map();

/** 班级码基本格式（与 registry.isClassCodeValid 同一口径，这里不 import 是为了避免循环依赖） */
function isValidCode(code) {
  return typeof code === 'string' && /^[A-Za-z0-9_-]{6,16}$/.test(code);
}

function normName(name) {
  return String(name ?? '').trim().toLowerCase().replace(/\s+/g, ' ');
}

function rowToRec(r) {
  let aliases = [];
  try { aliases = JSON.parse(r.aliases || '[]'); } catch { aliases = []; }
  if (!Array.isArray(aliases)) aliases = [];
  return {
    code: r.code,
    name: r.name || '',
    aliases,
    source: r.source || '',
    // 2026-11-02 多租户：老库没有这一列时读出来是 undefined ⇒ 落默认租户（存量兼容）
    tenantId: normalizeTenantId(r.tenant_id),
    createdAt: Number(r.created_at || 0),
    updatedAt: Number(r.updated_at || 0),
  };
}

// 反向索引键：`${tenantId}|\u0000${写法}`。
// 为什么必须带租户：`lookup` 原来是全局的，两所学校各一个「1班」会互相顶掉
// （后登记的把先登记的索引覆盖），再绑设备就绑到隔壁学校去了。
const keyOf = (tenantId, raw) => `${normalizeTenantId(tenantId)}\u0000${String(raw ?? '').trim()}`;

// 内存镜像的键：与 keyOf 同构，但语义是「某个租户下的某个 canonical 码」。
// 直接用 `code` 当键会踩同一个坑：站点老体系的外部码（class_0102）两校都在用，
// A 校写了 B 校就没了 —— 后面改名/删班就作用到隔壁学校的记录上。
const dirKey = (rec) => keyOf(rec.tenantId, rec.code);

function reindex(rec) {
  const tn = normalizeTenantId(rec.tenantId);
  lookup.set(keyOf(tn, rec.code), rec.code);
  if (rec.name) lookup.set(keyOf(tn, normName(rec.name)), rec.code);
  for (const a of rec.aliases) lookup.set(keyOf(tn, a), rec.code);
}

function unindex(rec) {
  const tn = normalizeTenantId(rec.tenantId);
  for (const k of [keyOf(tn, rec.code), keyOf(tn, normName(rec.name)), ...rec.aliases.map((a) => keyOf(tn, a))]) {
    if (lookup.get(k) === rec.code) lookup.delete(k);
  }
}

function persist(rec) {
  const now = Date.now();
  // 2026-10-26：db 未初始化时原来是 `db.prepare(...)` 直接 TypeError
  //   （「Cannot read properties of null」），崩得毫无指向性，排障只能靠猜。
  //   班级目录是**身份真源**，这里**不能**静默降级成内存态 —— 那样班级码会在重启后
  //   重新派生，已绑设备的归属当场跳变（比崩更糟）。所以给一条能直接照做的报错。
  if (!db) {
    throw new Error('班级目录未初始化：请先调用 initClassDir()（身份真源不可降级为内存态）');
  }
  db.prepare(
    `INSERT INTO classes (code, name, aliases, source, tenant_id, created_at, updated_at)
     VALUES (?, ?, ?, ?, ?, ?, ?)
     ON CONFLICT(tenant_id, code) DO UPDATE SET
       name = excluded.name,
       aliases = excluded.aliases,
       updated_at = excluded.updated_at`
  ).run(
    rec.code, rec.name, JSON.stringify(rec.aliases), rec.source,
    normalizeTenantId(rec.tenantId), rec.createdAt || now, now
  );
  rec.updatedAt = now;
}

/**
 * 打开班级目录。**幂等**：重复调用只返回同一实例（index.js 启动调一次即可）。
 * @param {string} [dbFile] 覆盖 DB 路径（单测用临时文件，不影响生产）
 */
export function initClassDir(dbFile = CLASS_DIR_DB_FILE) {
  if (db) return { ok: true, codes: dir.size, file: dbFile };
  db = new DatabaseSync(dbFile);
  // ⚠️ 顺序有坑：`CREATE INDEX ... ON classes(tenant_id)` **必须排在 ALTER 之后**。
  //    老库的表里还没有 tenant_id 列，先建索引会当场 `no such column: tenant_id`
  //    让整个 initClassDir 抛错 —— 那不是"迁移失败"，是服务起不来。
  //    （2026-11-02 被 unit-tenant.mjs 的存量库用例抓到）
  db.exec(`
    CREATE TABLE IF NOT EXISTS classes (
      code       TEXT NOT NULL,
      name       TEXT NOT NULL DEFAULT '',
      aliases    TEXT NOT NULL DEFAULT '[]',
      source     TEXT NOT NULL DEFAULT '',
      tenant_id  TEXT NOT NULL DEFAULT '${DEFAULT_TENANT}',
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      PRIMARY KEY (tenant_id, code)
    );
    CREATE INDEX IF NOT EXISTS idx_classes_name ON classes(name);
  `);
  // 存量库迁移：老表没有 tenant_id 列 ⇒ 补列，已有行自动落默认租户。
  // 用 PRAGMA 判列而不是 try/catch ALTER —— 让"到底迁没迁"可被日志说清。
  const cols = db.prepare('PRAGMA table_info(classes)').all().map((c) => String(c.name));
  if (!cols.includes('tenant_id')) {
    db.exec(`ALTER TABLE classes ADD COLUMN tenant_id TEXT NOT NULL DEFAULT '${DEFAULT_TENANT}'`);
  }
  // 列确定存在之后才建索引
  db.exec('CREATE INDEX IF NOT EXISTS idx_classes_tenant ON classes(tenant_id)');

  // ⚠️ 主键迁移：老表是 `code` 单列主键，两校用同一个外部码（class_0102）时
  //    第二次 INSERT 会撞 UNIQUE 而不是落成两条 —— 多租户白做。
  //    SQLite 改不了主键，只能重建表。判据看 index_list 里有没有 (tenant_id, code) 复合主键。
  const pkCols = db.prepare('PRAGMA table_info(classes)').all()
    .filter((c) => Number(c.pk) > 0)
    .sort((a, b) => Number(a.pk) - Number(b.pk))
    .map((c) => String(c.name));
  const pkOk = pkCols.length === 2 && pkCols[0] === 'tenant_id' && pkCols[1] === 'code';
  if (!pkOk) {
    db.exec(`
      BEGIN;
      CREATE TABLE classes__new (
        code       TEXT NOT NULL,
        name       TEXT NOT NULL DEFAULT '',
        aliases    TEXT NOT NULL DEFAULT '[]',
        source     TEXT NOT NULL DEFAULT '',
        tenant_id  TEXT NOT NULL DEFAULT '${DEFAULT_TENANT}',
        created_at INTEGER NOT NULL,
        updated_at INTEGER NOT NULL,
        PRIMARY KEY (tenant_id, code)
      );
      INSERT OR IGNORE INTO classes__new (code, name, aliases, source, tenant_id, created_at, updated_at)
        SELECT code, name, aliases, source, COALESCE(NULLIF(tenant_id,''),'${DEFAULT_TENANT}'), created_at, updated_at FROM classes;
      DROP TABLE classes;
      ALTER TABLE classes__new RENAME TO classes;
      CREATE INDEX IF NOT EXISTS idx_classes_name ON classes(name);
      CREATE INDEX IF NOT EXISTS idx_classes_tenant ON classes(tenant_id);
      COMMIT;
    `);
  }
  const rows = db.prepare('SELECT code, name, aliases, source, tenant_id, created_at, updated_at FROM classes').all();
  dir.clear();
  lookup.clear();
  for (const r of rows) {
    const rec = rowToRec(r);
    dir.set(dirKey(rec), rec);
    reindex(rec);
  }
  return { ok: true, codes: dir.size, file: dbFile };
}

/** 单测用：关掉当前实例，让下一次 initClassDir 重新打开（生产不要调）。 */
export function closeClassDir() {
  if (db) { db.close(); }
  db = null;
  dir.clear();
  lookup.clear();
}

/**
 * 「给一个班级名，算出它该有的码」——唯一实现。同租户内同名永远同码。
 *
 * ⚠️ 2026-11-02 多租户：**默认租户的算法一字未改**（`cls-<sha256(name) 前10>`），
 * 否则升级当刻全部存量班级码会变、已绑设备归属当场跳变（class-dir 铁律）。
 * 非默认租户把租户 id 混进摘要 ⇒ 两所学校的「1班」拿到两个不同的码，
 * 从此不会在 lookup 里互相顶掉。
 *
 * @param {string} name 班级显示名
 * @param {string} [tenantId] 租户 id（缺省 = 默认组织）
 */
export function deriveClassCode(name, tenantId = DEFAULT_TENANT) {
  const n = String(name ?? '').trim();
  if (!n) return '';
  const tn = normalizeTenantId(tenantId);
  const seed = tn === DEFAULT_TENANT ? n : `${tn}\u0000${n}`;
  const digest = createHash('sha256').update(seed, 'utf8').digest('hex').slice(0, 10);
  return `${DERIVED_PREFIX}${digest}`;
}

/**
 * 解析「这次绑定到底该写哪个班级码」—— **所有入口的唯一解析器**。
 *
 * 判定顺序（从强到弱）：
 *   ① code 已知（命中 canonical 或 alias）      → 用它所属的 canonical
 *   ② name 已知（同名班已登记）                 → 用那个班的 canonical，**并把本次 code 降级成 alias**
 *   ③ 都没有 → 派生 `cls-<hash(name)>`；name 也空则退回用传入的合法 code 自己登记
 *
 * @param {{code?:string, name?:string, source?:string}} input
 * @returns {{ok:boolean, code?:string, name?:string, aliases?:string[], merged?:boolean, error?:string}}
 *   merged=true 表示这次输入碰到了"同一个班的另一种写法"，已合并（调用方可以据此记一条事件）
 */
export function resolveClassCode({ code, name, source = 'cloud', tenantId = DEFAULT_TENANT } = {}) {
  const tn = normalizeTenantId(tenantId);
  const rawCode = String(code ?? '').trim();
  const rawName = String(name ?? '').trim();

  // ① code 已知 → 直接归一到它的 canonical（**只在同一租户内查**：别校的码不能顶掉本校）
  if (rawCode) {
    const hit = lookup.get(keyOf(tn, rawCode));
    if (hit) {
      const rec = dir.get(keyOf(tn, hit));
      // 这次带了新名字 → 补齐显示名（只补空，不覆盖已有的）
      if (rawName && !rec.name) {
        rec.name = rawName;
        reindex(rec);
        persist(rec);
      }
      return { ok: true, code: rec.code, name: rec.name, aliases: rec.aliases.slice() };
    }
  }

  // ② name 已知 → 同一个班（这是"两套体系合并"的关键一步；同样限本租户）
  if (rawName) {
    const hit = lookup.get(keyOf(tn, normName(rawName)));
    if (hit) {
      const rec = dir.get(keyOf(tn, hit));
      let merged = false;
      // 本次带来的另一个码，降级成 alias —— canonical 绝不让位（让位=已绑设备归属跳变）
      if (rawCode && rawCode !== rec.code && !rec.aliases.includes(rawCode)) {
        rec.aliases.push(rawCode);
        lookup.set(keyOf(tn, rawCode), rec.code);
        merged = true;
        persist(rec);
      }
      return { ok: true, code: rec.code, name: rec.name, aliases: rec.aliases.slice(), merged };
    }
  }

  // ③ 全新班级
  // ⚠️ 顺序很重要：**调用方给的合法 code 优先于按名字派生**。
  //   站点 `class_id`（class_01）这类外部体系码必须原样保留 —— 它是对账键，
  //   一改就断。派生只用于"只有名字、没有码"的场景（手机页输班名）。
  const derived = isValidCode(rawCode) ? rawCode : (rawName ? deriveClassCode(rawName, tn) : '');
  if (!isValidCode(derived)) {
    return { ok: false, error: '班级信息不合法，请重新选择' };
  }
  // 派生码恰好已被登记（理论上 D 会命中，这里兜并发）→ 直接复用
  const existed = dir.get(keyOf(tn, derived));
  if (existed) {
    return { ok: true, code: existed.code, name: existed.name, aliases: existed.aliases.slice() };
  }
  const rec = { code: derived, name: rawName, aliases: [], source, tenantId: tn, createdAt: Date.now(), updatedAt: 0 };
  dir.set(dirKey(rec), rec);
  reindex(rec);
  persist(rec);
  return { ok: true, code: rec.code, name: rec.name, aliases: [], merged: false };
}

/**
 * 登记一个「外部体系已有的班级码」（站点 class_id 这类，必须保留原码不动）。
 *
 * 与 resolveClassCode 的分工：resolve 是"给我一个码/名，还我一个统一身份"；
 * 本函数是"这个码本来就存在、名字是它，请把它纳进目录"。
 * 若同名班已由云端派生过 → canonical 仍是**先到的那个**，外部码成为 alias（返回 merged=true）。
 * @returns {{ok:boolean, code?:string, merged?:boolean, error?:string}}
 */
export function registerExternalCode(code, name, source = 'site', tenantId = DEFAULT_TENANT) {
  const tn = normalizeTenantId(tenantId);
  const c = String(code ?? '').trim();
  if (!c) return { ok: false, error: '缺班级码' };
  const existed = lookup.get(keyOf(tn, c));
  if (existed) {
    const rec = dir.get(keyOf(tn, existed));
    const nm = String(name ?? '').trim();
    if (nm && !rec.name) { rec.name = nm; reindex(rec); persist(rec); }
    return { ok: true, code: rec.code, merged: rec.code !== c };
  }
  const r = resolveClassCode({ code: c, name, source, tenantId: tn });
  if (!r.ok) return r;
  return { ok: true, code: r.code, merged: r.code !== c };
}

/**
 * 查一个班（按任意写法）。找不到回 null。
 * @param {string} code 班级码或显示名
 * @param {string} [tenantId] 租户（缺省 = 默认组织）。**跨租户查不到**是设计意图。
 */
export function getClass(code, tenantId = DEFAULT_TENANT) {
  const tn = normalizeTenantId(tenantId);
  const hit = lookup.get(keyOf(tn, code));
  return hit ? dir.get(dirKey({ code: hit, tenantId: tn })) : null;
}

/**
 * 班级目录。默认**只返回本租户**的班 —— 管理员界面上不该看到别校的班。
 * @param {string} [tenantId]
 */
export function listClassDir(tenantId = DEFAULT_TENANT) {
  const tn = normalizeTenantId(tenantId);
  return [...dir.values()]
    .filter((r) => normalizeTenantId(r.tenantId) === tn)
    .sort((a, b) => a.code.localeCompare(b.code))
    .map((r) => ({ ...r, aliases: r.aliases.slice() }));
}

/** 单测/对账用：无视租户列出全部（**不要给界面用**）。 */
export function listClassDirAll() {
  return [...dir.values()]
    .sort((a, b) => a.code.localeCompare(b.code))
    .map((r) => ({ ...r, aliases: r.aliases.slice() }));
}

/** 目录规模（启动日志用）。不传参 = 全库总数；传租户 = 该租户的班数。 */
export function classDirSize(tenantId) {
  if (tenantId === undefined) return dir.size;
  const tn = normalizeTenantId(tenantId);
  let n = 0;
  for (const r of dir.values()) if (normalizeTenantId(r.tenantId) === tn) n++;
  return n;
}

/**
 * 某班级属于哪个租户（设备/权限判定用）。
 *
 * ⚠️ 2026-11-02：外部体系码（站点 `class_0102`）**两校可以同时存在**，此时
 * 「code → 租户」本身是有歧义的。含糊地返回其中一个 = 权限判定会随机串到隔壁学校，
 * 所以这里**歧义就返回 null**，强制调用方带上 tenantId 提示。
 *
 * @param {string} code 班级 canonical 码
 * @param {string} [tenantId] 已知租户提示（传了就在该租户内查，最快也最安全）
 * @returns {string|null} 唯一确定的租户 id；找不到或跨租户重码 → null
 */
export function tenantOfClass(code, tenantId) {
  const c = String(code ?? '').trim();
  if (!c) return null;
  if (tenantId !== undefined) {
    const tn = normalizeTenantId(tenantId);
    return dir.has(keyOf(tn, c)) ? tn : null;
  }
  const found = new Set();
  for (const r of dir.values()) if (r.code === c) found.add(normalizeTenantId(r.tenantId));
  if (found.size !== 1) return null;   // 0 = 没有；>1 = 跨校重码，不给猜
  return [...found][0];
}

/**
 * 改显示名（2026-10-24 班级管理）。**只动 name，canonical 码永远不动**——
 * 让码让位等于已绑设备的归属当场跳变（class-dir 铁律）。
 * @returns {{ok:true, class:object} | {ok:false, error:string}}
 */
export function renameClass(code, newName, tenantId = DEFAULT_TENANT) {
  const tn = normalizeTenantId(tenantId);
  const c = String(code ?? '').trim();
  const n = String(newName ?? '').trim();
  if (!n) return { ok: false, error: '班级名不能为空' };
  const hit = lookup.get(keyOf(tn, c));
  if (!hit) return { ok: false, error: '班级不存在' };
  const rec = dir.get(keyOf(tn, hit));
  const nn = normName(n);
  // 新显示名已被**本租户内别的班**占用 → 拒。两个班共用一个显示名，会让 resolveClassCode
  // 的"同名归一"把后来设备的绑定送到另一个班去（名字就是同班判定第一依据）。
  // 隔壁学校有同名班不算冲突 —— 这正是多租户要解决的事。
  //
  // ⚠️ 文案是**对外契约**：2026-11-02 我曾把它改成「显示名已被本组织内的班级…」，
  //    结果 tests/regression-class-admin.mjs 的 `/已被班级/` 断言当场变红。
  //    租户收窄是行为改进，但报错字符串属于客户端可能已经依赖的接口面，
  //    不为措辞好看去动它 —— 已改回原文案（顺带证明既有回归确实在护着它）。
  const clash = lookup.get(keyOf(tn, nn));
  if (clash && clash !== hit) {
    return { ok: false, error: `显示名已被班级 ${clash} 使用，请换一个` };
  }
  // 旧显示名的索引摘除（仅当仍指向本班；理论上必然指向本班，防御性判断）
  if (lookup.get(keyOf(tn, normName(rec.name))) === hit) lookup.delete(keyOf(tn, normName(rec.name)));
  rec.name = n;
  reindex(rec);
  persist(rec);
  return { ok: true, class: { ...rec, aliases: rec.aliases.slice() } };
}

/**
 * 删班（2026-10-24 班级管理）。**设备归属不在这里处理**——调用方（class-admin.js）
 * 必须先定策略（release 移出 / refuse 有设备就拒），到这里班已经是空的。
 * 别名记录随 canonical 一同消失（aliases 是同一班的其他写法，不是独立实体）。
 * @returns {{ok:true, code, name} | {ok:false, error:string}}
 */
export function deleteClass(code, tenantId = DEFAULT_TENANT) {
  const tn = normalizeTenantId(tenantId);
  const c = String(code ?? '').trim();
  const hit = lookup.get(keyOf(tn, c));
  if (!hit) return { ok: false, error: '班级不存在' };
  const rec = dir.get(keyOf(tn, hit));
  // 摘除该班全部索引键（canonical / 显示名 / 每个 alias），只摘指向自己的
  unindex(rec);
  dir.delete(keyOf(tn, hit));
  db.prepare('DELETE FROM classes WHERE code = ? AND tenant_id = ?').run(hit, tn);
  return { ok: true, code: hit, name: rec.name };
}
