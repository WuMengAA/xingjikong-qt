// 指令队列落盘：把"已真正写进设备 socket、但还没等到回执"的指令存下来，做两件事——
//   ① 云端重启不丢（本功能的验收核心）；
//   ② 设备重连时按原下发顺序补发，补上"掉线一次就永久漏掉那条指令"的洞。
//
// 存哪儿：用 Node 内置的 node:sqlite（本机 v22.22.2 与生产运行的 v24.14.0 均实测可用，零新增依赖）。
//   为什么不用 better-sqlite3 / sqlite3：node_modules 里没有，为"轻中转"引原生依赖不划算。
//   为什么不用纯 JSON 文件：并发下单条追加 + 整表改状态要自己写原子写与去重，SQLite 现成且事务安全。
//   node:sqlite 目前标 experimental（启动会打一行 ExperimentalWarning），但两版运行时都用得了，先按最省的来。
//
// 纪律：只存"已下发"的指令；设备不在线根本没下发的那条**不进队列**（离线一律仍返回 409，见 registry）。

import fs from 'node:fs';
import path from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { QUEUE_DB_FILE, PENDING_TTL_MS, PENDING_MAX_PER_DEVICE, SETTLED_RETENTION_MS } from './config.js';

let db = null;

/**
 * 打开（或新建）队列表。失败**不让云端整体死掉**（云端一挂等于整个星集控全断），
 * 但必须大声报出来——失败时本功能退化为"不持久化"，重启会丢，这是要尽快修的降级态。
 */
export function initStore() {
  try {
    const dir = path.dirname(path.resolve(QUEUE_DB_FILE));
    if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
    db = new DatabaseSync(QUEUE_DB_FILE);
    db.exec('PRAGMA journal_mode = DELETE');   // 默认回滚日志足够；不引入 -wal/-shm 两个额外文件
    db.exec('PRAGMA synchronous = FULL');      // 进程被杀也不丢最后一条：队列量小，写代价无所谓
    db.exec(`CREATE TABLE IF NOT EXISTS instructions (
      id            INTEGER PRIMARY KEY AUTOINCREMENT,
      uid           TEXT    NOT NULL,
      action        TEXT    NOT NULL,
      params        TEXT    NOT NULL,
      state         TEXT    NOT NULL,          -- dispatched(已下发未回执) / done / failed / expired
      dispatched_at INTEGER NOT NULL,          -- 下发时间（ms）
      settled_at    INTEGER,                   -- 回执或作废时间（ms）
      result        TEXT,                      -- 设备回执 result：done / failed
      error         TEXT
    )`);
    db.exec('CREATE INDEX IF NOT EXISTS idx_instr_uid_state ON instructions(uid, state, id)');
  } catch (e) {
    console.error(`[cloud] FAIL 指令队列不可持久化（${QUEUE_DB_FILE}）：${e.message} —— 本进程重启会丢未回执指令，请尽快修复`);
    db = null;
  }
  return db;
}

export function storeReady() { return db !== null; }

/**
 * 落一条"已下发"记录，返回队列内稳定 id（设备回执按它配对；AUTOINCREMENT 保证重启后仍单调不重号）。
 * 失败返回 null：调用方仍照常下发，只是这条失去"重启不丢"的兜底。
 */
export function enqueueDispatch(uid, action, params) {
  if (!db) return null;
  try {
    const info = db.prepare(
      'INSERT INTO instructions (uid, action, params, state, dispatched_at) VALUES (?, ?, ?, ?, ?)',
    ).run(String(uid), String(action), JSON.stringify(params ?? {}), 'dispatched', Date.now());
    return Number(info.lastInsertRowid);
  } catch (e) {
    console.error(`[cloud] FAIL 指令落盘失败: ${e.message}`);
    return null;
  }
}

/** 下发时写 socket 抛错 → 回滚这条记录，别把"根本没送出去"的指令留成"等回执"。 */
export function dropDispatch(id) {
  if (!db || id == null) return;
  try { db.prepare('DELETE FROM instructions WHERE id = ?').run(Number(id)); }
  catch (e) { console.error(`[cloud] FAIL 回滚指令记录失败 id=${id}: ${e.message}`); }
}

/** 回执到达：把这条从"未回执"改成已完成/已失败。返回 true 表示确实更新到了一条。 */
export function settle(id, result, error) {
  if (!db || id == null) return false;
  try {
    const r = db.prepare(
      "UPDATE instructions SET state = ?, settled_at = ?, result = ?, error = ? WHERE id = ? AND state = 'dispatched'",
    ).run(result === 'done' ? 'done' : 'failed', Date.now(), String(result ?? ''), error || null, Number(id));
    return r.changes > 0;
  } catch (e) {
    console.error(`[cloud] FAIL 回执落盘失败 id=${id}: ${e.message}`);
    return false;
  }
}

/** 按指令 id 取回执对账所需的原始参数（2026-10-04：站点广播要拿 notice_id 把
 *  已执行回执关联回通知；云端表里 params 完整保留，settle 前查出来带上）。 */
export function paramsOf(id) {
  if (!db || id == null) return null;
  try {
    const r = db.prepare("SELECT action, params FROM instructions WHERE id = ?").get(Number(id));
    return r ? { action: r.action, params: safeParse(r.params) } : null;
  } catch (e) {
    console.error(`[cloud] FAIL 读取指令参数失败 id=${id}: ${e.message}`);
    return null;
  }
}

/** 按 notice_id 查这批指令的回执状态（2026-10-04：站点广播对账用）。
 * 下发 notify 时 params 里带 notice_id；回执 settle 后 state=done/failed。
 * @returns [{uid, state, result}] 按 uid 去重取最新一条。 */
export function receiptsByNotice(noticeId) {
  if (!db || noticeId == null) return [];
  try {
    const rows = db.prepare(
      "SELECT uid, state, result FROM instructions WHERE params LIKE ? AND action = 'notify' ORDER BY id DESC",
    ).all(`%\"notice_id\":${Number(noticeId)}%`);
    // 同 uid 多条 → 保留最新（id 最大）一条
    const seen = new Map();
    for (const r of rows) {
      if (!seen.has(r.uid)) seen.set(r.uid, { uid: r.uid, state: r.state, result: r.result ?? r.state });
    }
    return [...seen.values()];
  } catch (e) {
    console.error(`[cloud] FAIL 按 notice_id 查回执失败 id=${noticeId}: ${e.message}`);
    return [];
  }
}

/** 某设备"已下发未回执"的指令，按原下发顺序（id 升序）返回——重连补发就用它。 */
export function pendingFor(uid) {
  if (!db) return [];
  try {
    const rows = db.prepare(
      "SELECT id, action, params, dispatched_at FROM instructions WHERE uid = ? AND state = 'dispatched' ORDER BY id ASC",
    ).all(String(uid));
    return rows.map((r) => ({ id: r.id, action: r.action, params: safeParse(r.params), dispatchedAt: r.dispatched_at }));
  } catch (e) {
    console.error(`[cloud] FAIL 读取待补发指令失败 uid=${uid}: ${e.message}`);
    return [];
  }
}

/** 全部设备的"已下发未回执"指令，按 uid 分组（给 /api/instructions/pending 看）。 */
export function listPendingAll() {
  if (!db) return {};
  try {
    const rows = db.prepare(
      "SELECT id, uid, action, params, dispatched_at FROM instructions WHERE state = 'dispatched' ORDER BY id ASC",
    ).all();
    const out = {};
    for (const r of rows) {
      (out[r.uid] ||= []).push({ id: r.id, action: r.action, params: safeParse(r.params), dispatchedAt: r.dispatched_at });
    }
    return out;
  } catch (e) {
    console.error(`[cloud] FAIL 读取待补发指令失败: ${e.message}`);
    return {};
  }
}

/**
 * 清扫：① TTL 过期未回执 → expired；② 每台超上限的旧指令 → expired；③ 已回执行超保留期 → 删除。
 * 不扫的后果：队列表只增不减，且"重连瞬间批量补发"会随离线时间线性放大。返回各计数用于记流水。
 */
export function sweepQueue() {
  if (!db) return { expiredByTtl: 0, expiredByCap: 0, purged: 0 };
  const now = Date.now();
  let expiredByTtl = 0, expiredByCap = 0, purged = 0;
  try {
    const r1 = db.prepare(
      "UPDATE instructions SET state = 'expired', settled_at = ? WHERE state = 'dispatched' AND dispatched_at < ?",
    ).run(now, now - PENDING_TTL_MS);
    expiredByTtl = r1.changes;

    // 每台只保留最新的 PENDING_MAX_PER_DEVICE 条未回执，其余作废
    const uids = db.prepare("SELECT DISTINCT uid FROM instructions WHERE state = 'dispatched'").all();
    for (const { uid } of uids) {
      const r2 = db.prepare(
        `UPDATE instructions SET state = 'expired', settled_at = ?
           WHERE state = 'dispatched' AND uid = ? AND id NOT IN (
             SELECT id FROM instructions WHERE state = 'dispatched' AND uid = ? ORDER BY id DESC LIMIT ?
           )`,
      ).run(now, uid, uid, PENDING_MAX_PER_DEVICE);
      expiredByCap += r2.changes;
    }

    const r3 = db.prepare(
      "DELETE FROM instructions WHERE state <> 'dispatched' AND settled_at IS NOT NULL AND settled_at < ?",
    ).run(now - SETTLED_RETENTION_MS);
    purged = r3.changes;
  } catch (e) {
    console.error(`[cloud] FAIL 队列清扫异常: ${e.message}`);
  }
  return { expiredByTtl, expiredByCap, purged };
}

export function closeStore() {
  try { db?.close(); } catch { /* 关不上就算了，进程要退 */ }
  db = null;
}

function safeParse(s) {
  try { return JSON.parse(s); } catch { return {}; }
}
