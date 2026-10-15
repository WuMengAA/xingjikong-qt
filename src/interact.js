// 互动数据层（2026-10-15 · 灵动岛扩展组件6）：点歌 / 反馈点赞 / 祝福弹幕三张表。
//
// 为什么单独一个模块 + 单独一个库文件（不并进 store.js 的指令队列）：
//   ① 关注点不同：指令队列是"运维管道"（已下发未回执、24h 强 TTL、回执即删），
//      互动数据是"内容审核流"（学生产生的内容等管理端审批，pending 永不扫）；
//   ② 保留策略不同：混在一个库里，sweepQueue 的 SETTLED_RETENTION_MS(24h) 会把
//      待审的祝福/点歌当"已回执垃圾"删掉 —— 那是要把学生内容弄丢的生产事故；
//   ③ 限额口径不同：指令按"每台未回执条数"限，互动按"每台每小时条数"限。
// 同样用 Node 内置 node:sqlite（与 store.js 同理由：零新增依赖，两版运行时实测可用）。
//
// 纪律（继承 store.js / registry.js 三条）：
//   1. 失败必须报出来，不许静默；
//   2. 不许"报了成功其实没做"——DB 打不开时提交一律如实回 STORE_UNAVAILABLE；
//   3. 学生内容是不可信输入：长度/限额在云端再闸一次（端侧已有闸门，但端侧可被改包绕过）。

import fs from 'node:fs';
import path from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import {
  INTERACT_DB_FILE, INTERACT_RETENTION_MS, INTERACT_FEEDBACK_RETENTION_MS,
  INTERACT_SONG_PER_HOUR, INTERACT_DANMAKU_PER_HOUR, INTERACT_FEEDBACK_PER_HOUR,
} from './config.js';

let db = null;

/** 与端侧同口径的长度规则（组件5 详规）：歌名 100 / 祝福 30 / 歌手 100。 */
const LIMITS = {
  songTitle: 100,
  songArtist: 100,
  blessing: 30,
};

/** 点歌状态机：pending → approved | rejected | played | cancelled（只进不退）。 */
const SONG_STATES = new Set(['pending', 'approved', 'rejected', 'played', 'cancelled']);
/** 祝福状态机：pending → approved | rejected（只进不退）。 */
const BLESSING_STATES = new Set(['pending', 'approved', 'rejected']);

/** 待审队列容量护栏：没人审批时 pending 也不能无限涨（超限拒收新条并如实回错）。 */
const PENDING_CAP = Number(process.env.CLOUD_INTERACT_PENDING_CAP || 500);

export function interactReady() { return db !== null; }

/**
 * 打开（或新建）互动库并建三张表。失败**不让云端整体死掉**（云端一挂等于全断），
 * 但必须大声报出来 —— 失败时本功能退化为"不持久化"：提交如实回 STORE_UNAVAILABLE，
 * 管理端查询回空，绝不假装成功（纪律 2）。
 */
export function initInteract() {
  try {
    const dir = path.dirname(path.resolve(INTERACT_DB_FILE));
    if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
    db = new DatabaseSync(INTERACT_DB_FILE);
    db.exec('PRAGMA journal_mode = DELETE');   // 与指令队列同口径：不引入 -wal/-shm
    db.exec('PRAGMA synchronous = FULL');      // 进程被杀也不丢最后一条：量小，写代价无所谓
    db.exec(`CREATE TABLE IF NOT EXISTS song_requests (
      id          INTEGER PRIMARY KEY AUTOINCREMENT,
      uid         TEXT    NOT NULL,
      class_code  TEXT    NOT NULL DEFAULT '',
      title       TEXT    NOT NULL,
      artist      TEXT    NOT NULL DEFAULT '',
      source      TEXT    NOT NULL DEFAULT 'manual',   -- search | hot | manual
      state       TEXT    NOT NULL DEFAULT 'pending',  -- pending/approved/rejected/played/cancelled
      note        TEXT    NOT NULL DEFAULT '',         -- 管理端处理备注
      created_at  INTEGER NOT NULL,
      handled_at  INTEGER,
      handled_by  TEXT    NOT NULL DEFAULT ''
    )`);
    db.exec(`CREATE TABLE IF NOT EXISTS danmaku (
      id            INTEGER PRIMARY KEY AUTOINCREMENT,
      uid           TEXT    NOT NULL,
      class_code    TEXT    NOT NULL DEFAULT '',
      text          TEXT    NOT NULL,                  -- 已按 30 字规则截断/校验
      state         TEXT    NOT NULL DEFAULT 'pending',-- pending/approved/rejected
      reject_reason TEXT    NOT NULL DEFAULT '',
      created_at    INTEGER NOT NULL,
      handled_at    INTEGER,
      handled_by    TEXT    NOT NULL DEFAULT ''
    )`);
    db.exec(`CREATE TABLE IF NOT EXISTS feedback (
      id          INTEGER PRIMARY KEY AUTOINCREMENT,
      uid         TEXT    NOT NULL,
      class_code  TEXT    NOT NULL DEFAULT '',
      kind        TEXT    NOT NULL,                     -- like | feedback
      value       INTEGER NOT NULL DEFAULT 1,
      created_at  INTEGER NOT NULL
    )`);
    db.exec('CREATE INDEX IF NOT EXISTS idx_song_state ON song_requests(state, id DESC)');
    db.exec('CREATE INDEX IF NOT EXISTS idx_song_uid_at ON song_requests(uid, created_at)');
    db.exec('CREATE INDEX IF NOT EXISTS idx_danmaku_state ON danmaku(state, id DESC)');
    db.exec('CREATE INDEX IF NOT EXISTS idx_danmaku_uid_at ON danmaku(uid, created_at)');
    db.exec('CREATE INDEX IF NOT EXISTS idx_feedback_kind_at ON feedback(kind, created_at)');
    db.exec('CREATE INDEX IF NOT EXISTS idx_feedback_uid_at ON feedback(uid, created_at)');
  } catch (e) {
    console.error(`[cloud] FAIL 互动数据不可持久化（${INTERACT_DB_FILE}）：${e.message} —— 提交将如实回 STORE_UNAVAILABLE，请尽快修复`);
    db = null;
  }
  return db;
}

/* ---------- 内部工具 ---------- */

function clampText(s, max) {
  return String(s ?? '').trim().slice(0, max);
}

/** 每台每小时条数闸门（云端兜底；端侧 30s 冷却/点歌配额是第一道，但可被改包绕过）。 */
function hourlyCount(table, uid) {
  const since = Date.now() - 60 * 60 * 1000;
  const r = db.prepare(`SELECT COUNT(*) AS n FROM ${table} WHERE uid = ? AND created_at >= ?`).get(String(uid), since);
  return Number(r?.n || 0);
}

function pendingCount(table) {
  const r = db.prepare(`SELECT COUNT(*) AS n FROM ${table} WHERE state = 'pending'`).get();
  return Number(r?.n || 0);
}

function rowToItem(r) {
  return {
    id: r.id, uid: r.uid, classCode: r.class_code,
    state: r.state, createdAt: r.created_at,
    handledAt: r.handled_at ?? null, handledBy: r.handled_by || '',
    note: r.note ?? '', rejectReason: r.reject_reason ?? '',
    title: r.title ?? '', artist: r.artist ?? '', source: r.source ?? '',
    text: r.text ?? '', kind: r.kind ?? '', value: r.value ?? 0,
  };
}

/* ---------- 学生侧上报（agent → cloud） ---------- */

/**
 * 点歌提交。返回 {ok:true, id} 或 {ok:false, code, message}（如实回错，不静默）。
 * code: STORE_UNAVAILABLE / BAD_PAYLOAD / RATE_LIMITED / QUEUE_FULL
 */
export function submitSongRequest({ uid, classCode = '', title = '', artist = '', source = 'manual' }) {
  if (!db) return { ok: false, code: 'STORE_UNAVAILABLE', message: '互动数据层不可用' };
  const u = String(uid || '').trim();
  if (!u) return { ok: false, code: 'BAD_PAYLOAD', message: '缺 uid' };
  const t = clampText(title, LIMITS.songTitle);
  if (!t) return { ok: false, code: 'BAD_PAYLOAD', message: '歌名不能为空' };
  if (hourlyCount('song_requests', u) >= INTERACT_SONG_PER_HOUR) {
    return { ok: false, code: 'RATE_LIMITED', message: `点歌太频繁（每台每小时上限 ${INTERACT_SONG_PER_HOUR} 条）` };
  }
  if (pendingCount('song_requests') >= PENDING_CAP) {
    return { ok: false, code: 'QUEUE_FULL', message: '待审点歌已满，请稍后再试' };
  }
  try {
    const info = db.prepare(
      'INSERT INTO song_requests (uid, class_code, title, artist, source, state, created_at)'
      + " VALUES (?, ?, ?, ?, ?, 'pending', ?)",
    ).run(u, clampText(classCode, 32), t, clampText(artist, LIMITS.songArtist),
      ['search', 'hot', 'manual'].includes(source) ? source : 'manual', Date.now());
    return { ok: true, id: Number(info.lastInsertRowid) };
  } catch (e) {
    console.error(`[cloud] FAIL 点歌落盘失败 uid=${u}: ${e.message}`);
    return { ok: false, code: 'STORE_UNAVAILABLE', message: '落盘失败' };
  }
}

/**
 * 反馈/点赞上报。kind: 'like' | 'feedback'；value 正整数（点赞恒 1）。
 */
export function submitFeedback({ uid, classCode = '', kind = 'like', value = 1 }) {
  if (!db) return { ok: false, code: 'STORE_UNAVAILABLE', message: '互动数据层不可用' };
  const u = String(uid || '').trim();
  if (!u) return { ok: false, code: 'BAD_PAYLOAD', message: '缺 uid' };
  if (kind !== 'like' && kind !== 'feedback') {
    return { ok: false, code: 'BAD_PAYLOAD', message: 'kind 只支持 like / feedback' };
  }
  const v = Number(value);
  if (!Number.isFinite(v) || v < 1 || v > 100) {
    return { ok: false, code: 'BAD_PAYLOAD', message: 'value 必须是 1-100 的整数' };
  }
  if (hourlyCount('feedback', u) >= INTERACT_FEEDBACK_PER_HOUR) {
    return { ok: false, code: 'RATE_LIMITED', message: `反馈太频繁（每台每小时上限 ${INTERACT_FEEDBACK_PER_HOUR} 条）` };
  }
  try {
    const info = db.prepare(
      'INSERT INTO feedback (uid, class_code, kind, value, created_at) VALUES (?, ?, ?, ?, ?)',
    ).run(u, clampText(classCode, 32), kind, Math.floor(v), Date.now());
    return { ok: true, id: Number(info.lastInsertRowid) };
  } catch (e) {
    console.error(`[cloud] FAIL 反馈落盘失败 uid=${u}: ${e.message}`);
    return { ok: false, code: 'STORE_UNAVAILABLE', message: '落盘失败' };
  }
}

/**
 * 祝福弹幕上报（学生不可信源：云端按 30 字规则再闸一次）。
 * 返回 {ok:true, id, state:'pending'} —— 注意与端侧口径的差异：端侧 sendBlessingLocal
 * 的 1/2/3 是"敏感词/冷却/无效"本地码；云端这层只做长度与限额，敏感词审核留给人
 * （管理端审批），不跟端侧抢同一件事（端侧词表是占位的，见 danmaku.cpp 注释）。
 */
export function submitBlessing({ uid, classCode = '', text = '' }) {
  if (!db) return { ok: false, code: 'STORE_UNAVAILABLE', message: '互动数据层不可用' };
  const u = String(uid || '').trim();
  if (!u) return { ok: false, code: 'BAD_PAYLOAD', message: '缺 uid' };
  const t = clampText(text, LIMITS.blessing);
  if (!t) return { ok: false, code: 'BAD_PAYLOAD', message: '祝福不能为空' };
  if (hourlyCount('danmaku', u) >= INTERACT_DANMAKU_PER_HOUR) {
    return { ok: false, code: 'RATE_LIMITED', message: `祝福太频繁（每台每小时上限 ${INTERACT_DANMAKU_PER_HOUR} 条）` };
  }
  if (pendingCount('danmaku') >= PENDING_CAP) {
    return { ok: false, code: 'QUEUE_FULL', message: '待审祝福已满，请稍后再试' };
  }
  try {
    const info = db.prepare(
      'INSERT INTO danmaku (uid, class_code, text, state, created_at) VALUES (?, ?, ?, \'pending\', ?)',
    ).run(u, clampText(classCode, 32), t, Date.now());
    return { ok: true, id: Number(info.lastInsertRowid), state: 'pending' };
  } catch (e) {
    console.error(`[cloud] FAIL 祝福落盘失败 uid=${u}: ${e.message}`);
    return { ok: false, code: 'STORE_UNAVAILABLE', message: '落盘失败' };
  }
}

/* ---------- 管理端查询 / 审批（viewer → cloud） ---------- */

/** 点歌列表（默认待审优先，新的在前）。 */
export function listSongRequests({ state = '', limit = 100 } = {}) {
  if (!db) return [];
  const lim = Math.min(Math.max(Number(limit) || 100, 1), 500);
  try {
    const rows = state && SONG_STATES.has(state)
      ? db.prepare('SELECT * FROM song_requests WHERE state = ? ORDER BY id DESC LIMIT ?').all(state, lim)
      : db.prepare("SELECT * FROM song_requests ORDER BY CASE state WHEN 'pending' THEN 0 ELSE 1 END, id DESC LIMIT ?").all(lim);
    return rows.map(rowToItem);
  } catch (e) {
    console.error(`[cloud] FAIL 点歌查询失败: ${e.message}`);
    return [];
  }
}

/** 祝福列表（默认待审优先，新的在前）。 */
export function listBlessings({ state = '', limit = 100 } = {}) {
  if (!db) return [];
  const lim = Math.min(Math.max(Number(limit) || 100, 1), 500);
  try {
    const rows = state && BLESSING_STATES.has(state)
      ? db.prepare('SELECT * FROM danmaku WHERE state = ? ORDER BY id DESC LIMIT ?').all(state, lim)
      : db.prepare("SELECT * FROM danmaku ORDER BY CASE state WHEN 'pending' THEN 0 ELSE 1 END, id DESC LIMIT ?").all(lim);
    return rows.map(rowToItem);
  } catch (e) {
    console.error(`[cloud] FAIL 祝福查询失败: ${e.message}`);
    return [];
  }
}

/** 反馈汇总（管理端面板/站点看板用）：总数 + 今日 + 按班 Top。 */
export function feedbackSummary() {
  if (!db) return { ok: false, likeTotal: 0, likeToday: 0, feedbackTotal: 0, byClass: [] };
  try {
    const dayAgo = Date.now() - 24 * 60 * 60 * 1000;
    const likeTotal = Number(db.prepare("SELECT COUNT(*) AS n FROM feedback WHERE kind = 'like'").get()?.n || 0);
    const likeToday = Number(db.prepare(
      "SELECT COUNT(*) AS n FROM feedback WHERE kind = 'like' AND created_at >= ?").get(dayAgo)?.n || 0);
    const feedbackTotal = Number(db.prepare("SELECT COUNT(*) AS n FROM feedback WHERE kind = 'feedback'").get()?.n || 0);
    const byClass = db.prepare(
      "SELECT class_code AS classCode, COUNT(*) AS n FROM feedback WHERE kind = 'like' AND class_code <> ''"
      + ' GROUP BY class_code ORDER BY n DESC LIMIT 20',
    ).all();
    return { ok: true, likeTotal, likeToday, feedbackTotal, byClass };
  } catch (e) {
    console.error(`[cloud] FAIL 反馈汇总失败: ${e.message}`);
    return { ok: false, likeTotal: 0, likeToday: 0, feedbackTotal: 0, byClass: [] };
  }
}

/**
 * 点歌审批。action: approve | reject | play | cancel。
 * 返回 {ok:true, item}（item 为处理后的行）或 {ok:false, code, message}。
 */
export function handleSongRequest(id, action, by = '') {
  if (!db) return { ok: false, code: 'STORE_UNAVAILABLE', message: '互动数据层不可用' };
  const map = { approve: 'approved', reject: 'rejected', play: 'played', cancel: 'cancelled' };
  const next = map[action];
  if (!next) return { ok: false, code: 'BAD_PAYLOAD', message: 'action 只支持 approve/reject/play/cancel' };
  try {
    const row = db.prepare('SELECT * FROM song_requests WHERE id = ?').get(Number(id));
    if (!row) return { ok: false, code: 'NOT_FOUND', message: `点歌 #${id} 不存在` };
    if (row.state !== 'pending') {
      return { ok: false, code: 'BAD_STATE', message: `点歌 #${id} 已是 ${row.state}，不能重复处理` };
    }
    db.prepare('UPDATE song_requests SET state = ?, handled_at = ?, handled_by = ? WHERE id = ?')
      .run(next, Date.now(), String(by || '').slice(0, 64), Number(id));
    const after = db.prepare('SELECT * FROM song_requests WHERE id = ?').get(Number(id));
    return { ok: true, item: rowToItem(after) };
  } catch (e) {
    console.error(`[cloud] FAIL 点歌审批失败 id=${id}: ${e.message}`);
    return { ok: false, code: 'STORE_UNAVAILABLE', message: '落盘失败' };
  }
}

/**
 * 祝福审批。action: approve | reject（reject 可带 reason）。
 * 返回 {ok:true, item}；调用方（index.js）负责 approve 后向该班在线设备广播弹幕。
 */
export function handleBlessing(id, action, by = '', reason = '') {
  if (!db) return { ok: false, code: 'STORE_UNAVAILABLE', message: '互动数据层不可用' };
  const map = { approve: 'approved', reject: 'rejected' };
  const next = map[action];
  if (!next) return { ok: false, code: 'BAD_PAYLOAD', message: 'action 只支持 approve/reject' };
  try {
    const row = db.prepare('SELECT * FROM danmaku WHERE id = ?').get(Number(id));
    if (!row) return { ok: false, code: 'NOT_FOUND', message: `祝福 #${id} 不存在` };
    if (row.state !== 'pending') {
      return { ok: false, code: 'BAD_STATE', message: `祝福 #${id} 已是 ${row.state}，不能重复处理` };
    }
    db.prepare('UPDATE danmaku SET state = ?, reject_reason = ?, handled_at = ?, handled_by = ? WHERE id = ?')
      .run(next, clampText(reason, 120), Date.now(), String(by || '').slice(0, 64), Number(id));
    const after = db.prepare('SELECT * FROM danmaku WHERE id = ?').get(Number(id));
    return { ok: true, item: rowToItem(after) };
  } catch (e) {
    console.error(`[cloud] FAIL 祝福审批失败 id=${id}: ${e.message}`);
    return { ok: false, code: 'STORE_UNAVAILABLE', message: '落盘失败' };
  }
}

/**
 * 清扫：只扫**已处理**行（pending 是待审批内容，永不扫）。
 * 不扫的后果：互动库只增不减 —— 一个班一学期点歌+祝福+点赞是几万行的量级。
 * 返回各计数用于记流水。
 */
export function sweepInteract() {
  if (!db) return { songPurged: 0, danmakuPurged: 0, feedbackPurged: 0 };
  const now = Date.now();
  try {
    const r1 = db.prepare(
      "DELETE FROM song_requests WHERE state <> 'pending' AND handled_at IS NOT NULL AND handled_at < ?",
    ).run(now - INTERACT_RETENTION_MS);
    const r2 = db.prepare(
      "DELETE FROM danmaku WHERE state <> 'pending' AND handled_at IS NOT NULL AND handled_at < ?",
    ).run(now - INTERACT_RETENTION_MS);
    const r3 = db.prepare('DELETE FROM feedback WHERE created_at < ?').run(now - INTERACT_FEEDBACK_RETENTION_MS);
    return { songPurged: r1.changes, danmakuPurged: r2.changes, feedbackPurged: r3.changes };
  } catch (e) {
    console.error(`[cloud] FAIL 互动数据清扫异常: ${e.message}`);
    return { songPurged: 0, danmakuPurged: 0, feedbackPurged: 0 };
  }
}

export function closeInteract() {
  try { db?.close(); } catch { /* 关不上就算了，进程要退 */ }
  db = null;
}
