// 班级目录（Class Directory）—— **班级身份的唯一真源**（2026-10-20）
//
// ── 为什么要有这个文件 ────────────────────────────────────────────────
// 在这之前「一个班」的身份有两个互不相识的发行方：
//   ① 云端 `deriveClassCode(name)` → `cls-<sha256(name) 前10>`（OOBE 向导 / 二维码绑定走这里）
//   ② 站点 `stelarith_classes.class_id` → `class_01`（老配置窗「接入码 XJK-…」走站点 activate）
// 两条路各写各的键 ⇒ 同一个班能长出两个码；而 registry.js 的 classes 又只是**内存 Map**，
// 云端一重启班级名与归属关系全丢 ⇒ 重启后再绑定会派生出**第三套**码。三重漂移。
//
// 这里把它们收成一张表：
//   · `code`    ：canonical（对外唯一的班级键，写进 registry / 设备 classCode / perms 判定）
//   · `name`    ：显示名（如「1班」），做**同班判定**的第一依据
//   · `aliases` ：同一班的其他写法（外部体系码、历史码、名校验变体），只解析用，不外发
//   · `source`  ：谁是第一个把这个班登记进来的（cloud / site / admin / device）
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

/** canonical 码的派生前缀：名字 → `cls-<10 hex>`（14 字符，符合 isClassCodeValid 的 6-16 位） */
const DERIVED_PREFIX = 'cls-';

let db = null;
/** 内存镜像：code → { code, name, aliases, source, createdAt, updatedAt }。读多写少，全量缓存。 */
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
    createdAt: Number(r.created_at || 0),
    updatedAt: Number(r.updated_at || 0),
  };
}

function reindex(rec) {
  lookup.set(rec.code, rec.code);
  if (rec.name) lookup.set(normName(rec.name), rec.code);
  for (const a of rec.aliases) lookup.set(String(a), rec.code);
}

function persist(rec) {
  const now = Date.now();
  db.prepare(
    `INSERT INTO classes (code, name, aliases, source, created_at, updated_at)
     VALUES (?, ?, ?, ?, ?, ?)
     ON CONFLICT(code) DO UPDATE SET
       name = excluded.name,
       aliases = excluded.aliases,
       updated_at = excluded.updated_at`
  ).run(rec.code, rec.name, JSON.stringify(rec.aliases), rec.source, rec.createdAt || now, now);
  rec.updatedAt = now;
}

/**
 * 打开班级目录。**幂等**：重复调用只返回同一实例（index.js 启动调一次即可）。
 * @param {string} [dbFile] 覆盖 DB 路径（单测用临时文件，不影响生产）
 */
export function initClassDir(dbFile = CLASS_DIR_DB_FILE) {
  if (db) return { ok: true, codes: dir.size, file: dbFile };
  db = new DatabaseSync(dbFile);
  db.exec(`
    CREATE TABLE IF NOT EXISTS classes (
      code       TEXT PRIMARY KEY,
      name       TEXT NOT NULL DEFAULT '',
      aliases    TEXT NOT NULL DEFAULT '[]',
      source     TEXT NOT NULL DEFAULT '',
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_classes_name ON classes(name);
  `);
  const rows = db.prepare('SELECT code, name, aliases, source, created_at, updated_at FROM classes').all();
  dir.clear();
  lookup.clear();
  for (const r of rows) {
    const rec = rowToRec(r);
    dir.set(rec.code, rec);
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

/** 「给一个班级名，算出它该有的码」——唯一实现。同名永远同码。 */
export function deriveClassCode(name) {
  const n = String(name ?? '').trim();
  if (!n) return '';
  const digest = createHash('sha256').update(n, 'utf8').digest('hex').slice(0, 10);
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
export function resolveClassCode({ code, name, source = 'cloud' } = {}) {
  const rawCode = String(code ?? '').trim();
  const rawName = String(name ?? '').trim();

  // ① code 已知 → 直接归一到它的 canonical
  if (rawCode) {
    const hit = lookup.get(rawCode);
    if (hit) {
      const rec = dir.get(hit);
      // 这次带了新名字 → 补齐显示名（只补空，不覆盖已有的）
      if (rawName && !rec.name) {
        rec.name = rawName;
        reindex(rec);
        persist(rec);
      }
      return { ok: true, code: rec.code, name: rec.name, aliases: rec.aliases.slice() };
    }
  }

  // ② name 已知 → 同一个班（这是"两套体系合并"的关键一步）
  if (rawName) {
    const hit = lookup.get(normName(rawName));
    if (hit) {
      const rec = dir.get(hit);
      let merged = false;
      // 本次带来的另一个码，降级成 alias —— canonical 绝不让位（让位=已绑设备归属跳变）
      if (rawCode && rawCode !== rec.code && !rec.aliases.includes(rawCode)) {
        rec.aliases.push(rawCode);
        lookup.set(rawCode, rec.code);
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
  const derived = isValidCode(rawCode) ? rawCode : (rawName ? deriveClassCode(rawName) : '');
  if (!isValidCode(derived)) {
    return { ok: false, error: '班级信息不合法，请重新选择' };
  }
  // 派生码恰好已被登记（理论上 D 会命中，这里兜并发）→ 直接复用
  const existed = dir.get(derived);
  if (existed) {
    return { ok: true, code: existed.code, name: existed.name, aliases: existed.aliases.slice() };
  }
  const rec = { code: derived, name: rawName, aliases: [], source, createdAt: Date.now(), updatedAt: 0 };
  dir.set(rec.code, rec);
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
export function registerExternalCode(code, name, source = 'site') {
  const c = String(code ?? '').trim();
  if (!c) return { ok: false, error: '缺班级码' };
  const existed = lookup.get(c);
  if (existed) {
    const rec = dir.get(existed);
    const nm = String(name ?? '').trim();
    if (nm && !rec.name) { rec.name = nm; reindex(rec); persist(rec); }
    return { ok: true, code: rec.code, merged: rec.code !== c };
  }
  const r = resolveClassCode({ code: c, name, source });
  if (!r.ok) return r;
  return { ok: true, code: r.code, merged: r.code !== c };
}

/** 查一个班（按任意写法）。找不到回 null。 */
export function getClass(code) {
  const hit = lookup.get(String(code ?? '').trim());
  return hit ? dir.get(hit) : null;
}

/** 全量班级目录（管理员 / 对账用）。 */
export function listClassDir() {
  return [...dir.values()]
    .sort((a, b) => a.code.localeCompare(b.code))
    .map((r) => ({ ...r, aliases: r.aliases.slice() }));
}

/** 目录规模（启动日志用）。 */
export function classDirSize() { return dir.size; }
