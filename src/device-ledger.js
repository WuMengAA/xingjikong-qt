// 被控端客户端账本（Device Ledger）—— 每台被控端在云端的持久身份档案（2026-11-18）
//
// ── 为什么必须有这个文件 ─────────────────────────────────────────────
// 用户点单原话：「云端绑定识别问题的缺失 / 重新拟定被控端的主体地位，每一个被控端
// 都作为一个客户端进行注册，以及相关信息的补充初始化」。
//
// 改造前的真实状态（不是猜测，是 grep 过的）：
//   registry.js:39  const knownDevices = new Map();
// 这张表纯内存、无任何落盘，注释自己写着「knownDevices 不落盘（重连重建）」。
// 后果链很短：云端一重启（发版、崩溃、watchdog 拉起）→ 设备名、班级归属、租户、
// 用途全部归零 → 只剩设备下次 register 自报的那几个字段 → 凡是「管理端改过」的信息
// （改名、改用途、改绑班）全部丢失；改名钉住（device-identity.js 的 namePins）也是
// 内存 Map，一起丢。用户看到的症状就是「绑定识别没了」。
//
// ── 定位：设备身份注册表，不是在线表 ─────────────────────────────────
// 云端设备状态有两层，本文件只管第一层：
//   ① 身份（本文件）：这台设备是谁、归哪个班/哪所学校、叫什么、用途、首次接入时间、
//      第几次接入、名字是不是被人钉住过 —— 与「在线」无关，掉线、重启、换版本都不变。
//   ② 在线（registry.devices）：ws 连接、心跳、帧计数 —— 一变就变，不落盘。
// 两者混在一起就是原始缺陷的根源：把「身份」当成「连接的附属物」，连接没了身份也没了。
//
// 参照（行业一手，2026-11-18 实读原文）：
//   Azure IoT Hub 的 identity registry ——「Every IoT hub has an identity registry
//   that stores information about the devices and modules permitted to connect to
//   that IoT hub.」「The lifecycle of a device twin is linked to its corresponding
//   device identity.」设备身份是独立于连接的实体，连不连都在。本文件就是这张表。
//
// ── 纪律 ─────────────────────────────────────────────────────────────
//   · 只读写自己的表，不 import registry（否则循环依赖：registry 要 import 本文件取 hydrate）。
//   · 初始化失败不许拖垮云端：降级成内存态并大声报错（现状即降级态，不假装已修复）。
//   · 行数红线 600（§10.5 js/mjs）。

import { DatabaseSync } from 'node:sqlite';
import { createHash } from 'node:crypto';
import path from 'node:path';
import fs from 'node:fs';

/**
 * 账本落盘路径。与 registry 的 vnc-access.json 同口径（生产 run-cloud.cmd 不设 CLOUD_DATA_DIR）。
 *
 * 测试态守卫：单测进程（argv 落在 tests/ 下）且没显式指定文件时走 :memory:。
 * 为什么必须这样做：registry.js 在模块加载时就 hydrate 一次，若按 cwd 落盘，
 * 19 个既有单测会在仓库根目录互相同一份 device-ledger.db 里累积设备，
 * 后跑的用例 listDevices 立刻多出几台幽灵设备（假失败，且越跑越脏）。
 * 显式设置 CLOUD_DEVICE_LEDGER_DB_FILE 的用例（落盘/hydrate 测试）不受影响。
 */
export function ledgerFile() {
  const explicit = process.env.CLOUD_DEVICE_LEDGER_DB_FILE;
  if (explicit) return explicit;
  if (/(^|[\\/])tests[\\/]/.test(String(process.argv[1] || ''))) return ':memory:';
  const dir = process.env.CLOUD_DATA_DIR || process.cwd();
  return path.join(dir, 'device-ledger.db');
}

/** 客户端编号前缀：dev-<10 hex>，由 uid 派生 ⇒ 同一台设备永远是同一个号。 */
const CLIENT_PREFIX = 'dev-';

let db = null;
let dbFile = '';
let hydrating = false; // 装载期间禁止回写（否则把读出来的行原样再写一遍）
let degraded = false; // true = 落盘不可用，退回内存态（诚实记账，不假装已修复）

const SCHEMA = [
  'CREATE TABLE IF NOT EXISTS devices (' + [
    "  uid TEXT PRIMARY KEY",
    "  name TEXT NOT NULL DEFAULT ''",
    "  class_code TEXT NOT NULL DEFAULT ''",
    "  tenant_id TEXT NOT NULL DEFAULT ''",
    "  role TEXT NOT NULL DEFAULT ''",
    "  version TEXT NOT NULL DEFAULT ''",
    "  env TEXT NOT NULL DEFAULT ''",
    "  caps TEXT NOT NULL DEFAULT ''",
    "  actions TEXT NOT NULL DEFAULT ''",
    "  cid TEXT NOT NULL DEFAULT ''",
    "  first_seen INTEGER NOT NULL DEFAULT 0",
    "  last_seen INTEGER NOT NULL DEFAULT 0",
    "  register_count INTEGER NOT NULL DEFAULT 0",
    "  name_pin TEXT NOT NULL DEFAULT ''",
    "  updated_at INTEGER NOT NULL DEFAULT 0",
  ].join(', ') + ')',
  'CREATE INDEX IF NOT EXISTS idx_devices_class ON devices(class_code)',
  'CREATE INDEX IF NOT EXISTS idx_devices_tenant ON devices(tenant_id)',
].join(';\n');

/**
 * 打开账本。幂等；失败降级不抛（云端启动不能因为这个文件起不来）。
 * @param {string} [file] 覆盖路径（单测用临时文件）
 */
export function initDeviceLedger(file = ledgerFile()) {
  if (db) return { ok: true, file: dbFile, degraded };
  try {
    fs.mkdirSync(path.dirname(path.resolve(file)), { recursive: true });
    db = new DatabaseSync(file);
    db.exec('PRAGMA journal_mode = DELETE');
    db.exec('PRAGMA synchronous = FULL');
    db.exec(SCHEMA);
    dbFile = file;
    degraded = false;
    return { ok: true, file: dbFile, degraded };
  } catch (e) {
    db = null;
    degraded = true;
    // 大声报错：身份不可落盘 = 重启即丢绑定，属生产事故级，绝不能静默。
    console.error('[device-ledger] 打开客户端账本失败，已降级为内存态（重启后绑定信息会丢）：', e && e.message ? e.message : e);
    return { ok: false, file, degraded: true, error: e && e.message ? e.message : String(e) };
  }
}

/** 单测用：关掉当前实例，让下一次初始化重新打开（生产不要调）。 */
export function closeDeviceLedger() {
  if (db) { try { db.close(); } catch { /* 已关，不必再报 */ } }
  db = null;
  dbFile = '';
  hydrating = false;
  degraded = false;
}

export function ledgerStatus() {
  return { ok: !!db, file: dbFile, degraded, rows: db ? count() : 0 };
}

function count() {
  try { return Number(db.prepare('SELECT COUNT(*) AS n FROM devices').all()[0]?.n || 0); } catch { return 0; }
}

function rowOf(uid) {
  if (!db) return null;
  try {
    const r = db.prepare('SELECT * FROM devices WHERE uid = ?').all(String(uid));
    return r && r[0] ? r[0] : null;
  } catch { return null; }
}

function str(v, max = 128) {
  return String(v ?? '').trim().slice(0, max);
}

/** 由 uid 派生稳定客户端编号（同一台设备永远同一个号）。 */
export function clientIdOf(uid) {
  const h = createHash('sha256').update(String(uid ?? ''), 'utf8').digest('hex').slice(0, 10);
  return CLIENT_PREFIX + h;
}

function jsonOrEmpty(v) {
  if (v === null || v === undefined) return '';
  try { return JSON.stringify(v); } catch { return ''; }
}

function parseJson(s) {
  if (!s) return null;
  try { return JSON.parse(s); } catch { return null; }
}

/**
 * 写一条设备身份（合并语义，不是整行覆盖）。
 *
 * 合并规则来自原始缺陷的教训 —— 寄存器与元数据是不同来源的写入：
 *   · register 每次都会带 name（主机名）—— 不能因此把管理端改的名字顶掉，
 *     所以「已有 name_pin 时以 pin 为准」；
 *   · register 可能带空 classCode（离线期被绑班的设备自报空）—— 不能把云端已知的班级清空；
 *   · role/tenantId 只在首次采纳自报值（与 registry.markConnected 的权威口径一致）。
 *
 * @param {string} uid
 * @param {object} rec registry.knownDevices 里的那条记录
 * @param {object} [opts] { isRegister:boolean } 是否算一次「接入」（自增 register_count）
 */
export function ledgerPut(uid, rec, opts = {}) {
  if (!db || hydrating) return null;
  const u = str(uid, 200);
  if (!u) return null;
  const cur = rowOf(u);
  const now = Date.now();
  const isRegister = opts.isRegister === true;

  const pickStr = (next, old) => (str(next) || str(old));
  const namePin = str(cur && cur.name_pin, 64);
  const recName = str(rec && rec.name, 64);

  const row = {
    uid: u,
    name: namePin || recName || str(cur && cur.name, 64),
    class_code: pickStr(rec && rec.classCode, cur && cur.class_code),
    tenant_id: pickStr(rec && rec.tenantId, cur && cur.tenant_id),
    role: pickStr(rec && rec.role, cur && cur.role),
    version: pickStr(rec && rec.version, cur && cur.version),
    env: pickStr(rec && rec.env, cur && cur.env),
    caps: jsonOrEmpty(rec && rec.caps) || str(cur && cur.caps, 65536),
    actions: jsonOrEmpty(rec && rec.actions) || str(cur && cur.actions, 65536),
    cid: str(cur && cur.cid) || clientIdOf(u),
    first_seen: Number(cur && cur.first_seen) || now,
    last_seen: Number(rec && rec.lastSeen) || Number(cur && cur.last_seen) || now,
    register_count: (Number(cur && cur.register_count) || 0) + (isRegister ? 1 : 0),
    name_pin: namePin,
    updated_at: now,
  };
  try {
    db.prepare('INSERT INTO devices'
      + ' (uid, name, class_code, tenant_id, role, version, env, caps, actions, cid,'
      + ' first_seen, last_seen, register_count, name_pin, updated_at)'
      + ' VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)'
      + ' ON CONFLICT(uid) DO UPDATE SET'
      + ' name = excluded.name, class_code = excluded.class_code, tenant_id = excluded.tenant_id,'
      + ' role = excluded.role, version = excluded.version, env = excluded.env,'
      + ' caps = excluded.caps, actions = excluded.actions,'
      + ' last_seen = excluded.last_seen, register_count = excluded.register_count,'
      + ' updated_at = excluded.updated_at')
      .run(row.uid, row.name, row.class_code, row.tenant_id, row.role, row.version, row.env,
        row.caps, row.actions, row.cid, row.first_seen, row.last_seen,
        row.register_count, row.name_pin, row.updated_at);
    return row;
  } catch (e) {
    console.error('[device-ledger] 写设备身份失败：', e && e.message ? e.message : e);
    return null;
  }
}

function ledgerDelete(uid) {
  if (!db || hydrating) return;
  try { db.prepare('DELETE FROM devices WHERE uid = ?').run(str(uid, 200)); } catch { /* 删不掉不影响内存态 */ }
}

/** 全部身份档案（装载用）。 */
export function allLedgerRows() {
  if (!db) return [];
  try { return db.prepare('SELECT * FROM devices').all() || []; } catch { return []; }
}

/**
 * 把账本灌进 registry 的 knownDevices。
 *
 * 值形状必须与 registry.markConnected 写进去的完全一致（否则 listDevices 的
 * kd.name ?? null / getDeviceClassCode 读到的字段名会错位）：
 *   { uid, name, classCode, tenantId, role, version, env, lastSeen }
 * caps/actions 额外带上 —— listDevices 对离线设备可以据此显示「它能做什么」。
 */
export function hydrateKnown(map) {
  const r = initDeviceLedger();
  if (!r.ok) return map;
  const rows = allLedgerRows();
  hydrating = true;
  try {
    for (const x of rows) {
      map.set(String(x.uid), {
        uid: String(x.uid),
        name: x.name || null,
        classCode: x.class_code || null,
        tenantId: x.tenant_id || null,
        role: x.role || null,
        version: x.version || 'unknown',
        env: x.env || 'prod',
        lastSeen: Number(x.last_seen) || 0,
        caps: parseJson(x.caps),
        actions: parseJson(x.actions),
      });
    }
  } finally {
    hydrating = false;
  }
  return map;
}

/* ---------- 就地写落盘（Proxy）---------- */

/**
 * 记录字段 → 表列。为什么要这一层：
 * registry 里有一半写入是就地改字段而不是整条 set ——
 *   addToClassIndex:       known.classCode = code; known.tenantId = tn;
 *   unbindDeviceFromClass: known.classCode = null;
 *   markDisconnected:      known.lastSeen = Date.now();
 *   recomputeDeviceEnv:    kd.env = 'test' | 'prod';
 * 只拦 Map.set 的话这些改动全都不落盘 —— 表现就是「解绑了但重启后班又回来了」。
 * 这类 bug 比不落盘更难查，所以就地写也必须落。
 *
 * 这里不能复用 ledgerPut 的合并语义：合并规则是「新值为空就沿用旧值」，
 * 而就地写 classCode = null 的语义恰恰是「显式清空」。所以走单列表更新。
 */
const COLS = {
  name: 'name',
  classCode: 'class_code',
  tenantId: 'tenant_id',
  role: 'role',
  version: 'version',
  env: 'env',
  lastSeen: 'last_seen',
  caps: 'caps',
  actions: 'actions',
};

function ledgerPatch(uid, key, value) {
  if (!db || hydrating) return;
  const col = COLS[key];
  if (!col) return; // uid / 临时字段不回写
  const u = str(uid, 200);
  if (!u) return;
  const isNum = col === 'last_seen';
  const isJson = col === 'caps' || col === 'actions';
  const val = isNum ? (Number(value) || 0) : isJson ? jsonOrEmpty(value) : str(value, 65536);
  try {
    if (!rowOf(u)) {
      // 先建档再落这一列（首次出现就是就地写的场景，例如未注册就被绑班）。
      db.prepare('INSERT INTO devices (uid, cid, name, first_seen, last_seen, updated_at)'
        + ' VALUES (?, ?, ?, ?, ?, ?)')
        .run(u, clientIdOf(u), col === 'name' ? val : '',
          Date.now(), col === 'last_seen' ? val : 0, Date.now());
    }
    db.prepare('UPDATE devices SET ' + col + ' = ?, updated_at = ? WHERE uid = ?')
      .run(val, Date.now(), u);
  } catch (e) {
    console.error('[device-ledger] 就地写落盘失败：', e && e.message ? e.message : e);
  }
}

/** 原对象 → 可落盘代理（同一对象只包一层，避免每次 get 都新建代理）。 */
const proxyCache = new WeakMap();
const IS_PROXY = Symbol('ledgerProxy');

function wrapRecord(raw) {
  if (!raw || typeof raw !== 'object') return raw;
  if (raw[IS_PROXY]) return raw;
  const hit = proxyCache.get(raw);
  if (hit) return hit;
  const uid = String(raw.uid || '');
  const px = new Proxy(raw, {
    get(t, k, r) {
      if (k === IS_PROXY) return true;
      return Reflect.get(t, k, r);
    },
    set(t, k, v) {
      t[k] = v;
      if (typeof k === 'string') ledgerPatch(uid, k, v);
      return true;
    },
    deleteProperty(t, k) {
      delete t[k];
      if (typeof k === 'string') ledgerPatch(uid, k, null);
      return true;
    },
  });
  proxyCache.set(raw, px);
  return px;
}

/**
 * registry 的 knownDevices 替身：写入/删除自动落盘。
 *
 * 关键细节：同对象重复 set 不算一次接入。setDeviceMeta 对已有设备是就地改字段
 * （不调 set），但绑班会把同一个对象再 set 一次（刷新索引）。若把这种 set 也算
 * 一次 register，register_count 会随绑班次数虚高 —— 那个计数是「这台设备接入了
 * 多少次」的审计口径，掺进无关写入就没有意义了。判据：super.get(k) === v 即同对象。
 */
/** 把库里的合并结果写回内存记录（就地改，保持对象引用稳定）。 */
function applyMerged(rec, row) {
  rec.name = row.name || null;
  rec.classCode = row.class_code || null;
  rec.tenantId = row.tenant_id || null;
  rec.role = row.role || null;
  rec.version = row.version || rec.version;
  rec.env = row.env || rec.env;
  if (row.last_seen) rec.lastSeen = Number(row.last_seen);
}

export class LedgerMap extends Map {
  /** 返回可落盘代理：registry 拿到后无论就地改字段还是整条替换，都会落到账本里。 */
  get(k) {
    return wrapRecord(super.get(k));
  }

  set(k, v) {
    const prev = super.get(k);
    const isRegister = prev !== v;
    if (!hydrating && v && typeof v === 'object') {
      // 一次写入完成两件事：落库 + 把库里的合并结果写回内存。
      // 合并语义（空值沿用旧值、name_pin 优先）必须**同时**作用于内存，
      // 否则 registry 读内存当权威 ⇒「库里对的、面板显示错的」这种分裂最难查。
      // 就地写回，保持对象引用不变（registry 里多处持有同一个 known 引用）。
      const merged = ledgerPut(k, v, { isRegister });
      if (merged) applyMerged(v, merged);
    }
    super.set(k, v);
    return this;
  }

  delete(k) {
    const had = super.has(k);
    const okDel = super.delete(k);
    if (had && okDel) ledgerDelete(k);
    return okDel;
  }
}

/* ---------- 名字钉住（持久化）---------- */

/** 读钉住的名字（空串 = 没钉住）。 */
export function getNamePin(uid) {
  if (!db) return '';
  const row = rowOf(uid);
  return str(row && row.name_pin, 64);
}

/** 钉住/取消钉住名字（name='' 表示取消）。改名成功即调用，云端重启后仍然生效。 */
export function setNamePin(uid, name) {
  if (!db) return false;
  const u = str(uid, 200);
  if (!u) return false;
  const n = str(name, 64);
  const now = Date.now();
  try {
    const cur = rowOf(u);
    if (!cur) {
      // 还没接入过就改名（管理端对离线历史设备改名）：建一条最小档案，别丢这次改名。
      db.prepare('INSERT INTO devices (uid, name, cid, first_seen, last_seen, name_pin, updated_at)'
        + ' VALUES (?, ?, ?, ?, ?, ?, ?)').run(u, n, clientIdOf(u), now, now, n, now);
    } else {
      db.prepare('UPDATE devices SET name_pin = ?, name = ?, updated_at = ? WHERE uid = ?')
        .run(n, n || cur.name, now, u);
    }
    return true;
  } catch (e) {
    console.error('[device-ledger] 写名字钉住失败：', e && e.message ? e.message : e);
    return false;
  }
}

/* ---------- 客户端档案（设备自查 / 管理端详情）---------- */

/**
 * 一台被控端的客户端档案：身份 + 初始化状态。
 *
 * 「补充信息初始化」在这里落地：新设备第一次 register 即有完整档案（客户端编号、
 * 首次接入时间、接入次数、四个绑定维度各自的完成情况），不需要管理端再点一次
 * 「初始化」。
 *
 * @param {string} uid
 * @param {object} [online] registry 的在线记录（有就补上实时字段）
 * @returns {object|null} null = 这台设备从来没接入过（不许凭空造档案）
 */
export function clientProfile(uid, online = null) {
  const u = str(uid, 200);
  if (!u) return null;
  const x = rowOf(u);
  if (!x) return null;
  const firstSeen = Number(x.first_seen) || 0;
  const lastSeen = Number(x.last_seen) || 0;
  const pin = str(x.name_pin, 64);
  return {
    clientId: str(x.cid) || clientIdOf(u),
    uid: u,
    name: x.name || null,
    namePinned: !!pin,
    classCode: x.class_code || null,
    tenantId: x.tenant_id || null,
    role: x.role || null,
    version: (online && online.version) || x.version || 'unknown',
    env: x.env || 'prod',
    online: !!online,
    // 补充信息初始化状态：四个维度各自是否已就绪（未就绪的由 OOBE / 管理端补）
    initialized: {
      name: !!x.name,
      class: !!x.class_code,
      tenant: !!x.tenant_id,
      role: !!x.role,
      complete: !!(x.name && x.class_code && x.tenant_id && x.role),
    },
    firstSeenAt: firstSeen ? new Date(firstSeen).toISOString() : null,
    lastSeenAt: lastSeen ? new Date(lastSeen).toISOString() : null,
    lastSeenAgoSec: lastSeen ? Math.round((Date.now() - lastSeen) / 1000) : null,
    registerCount: Number(x.register_count) || 0,
    caps: parseJson(x.caps),
    actions: parseJson(x.actions),
  };
}

/** 运维诊断：账本规模 + 初始化完成度分布（管理端「客户端注册总览」用）。 */
export function ledgerStats() {
  const rows = allLedgerRows();
  const out = { total: rows.length, complete: 0, partial: 0, pinned: 0, tenants: {}, file: dbFile, degraded };
  for (const x of rows) {
    const done = !!(x.name && x.class_code && x.tenant_id && x.role);
    if (done) out.complete++; else out.partial++;
    if (x.name_pin) out.pinned++;
    const t = x.tenant_id || '(未定)';
    out.tenants[t] = (out.tenants[t] || 0) + 1;
  }
  return out;
}
