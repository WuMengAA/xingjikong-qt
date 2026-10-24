// 班级房间（Class Room）—— 每班一个**持久房间**（2026-10-24）
//
// ── 为什么要这个文件 ────────────────────────────────────────────────────
// 用户诉求（2026-10-24）：「每创建一个班级就建立一个永不停息的子服务器，就像微信
// 聊天一样，数据保存在云端，人可以被添加」。
//
// 物理上做不到 3000 个班 3000 个进程（2GB 云机会被 fork 打死），但**行为**可以完全
// 等价：每班在云端是一个持久房间 —— 固定地址（班级码）、成员表、消息记录、在线状态，
// 不因"没人订阅"而销毁，重启后原样还在。微信群也不是每群一台服务器。
//
// 改造前的三个缺口（都在代码里，不是推测）：
//   · `registry.js:32 knownDevices` 是纯内存 Map ⇒ 云端一重启，设备在哪个班**全丢**；
//   · `class-events.js:51 classEventLog` 是内存环形缓冲 ⇒ 重启后"聊天记录"清空；
//   · 权限只来自票里的 scopes（`perms.js:50`）⇒ **没有成员表，加不进人**。
//
// 本文件补的是后两个（设备归属落盘在 registry 侧另做）：
//   · `class_events`  —— 消息历史（每班环形裁剪 CLASS_EVENT_LOG_KEEP 条）
//   · `class_members` —— 班级成员（人可以被添加；角色 owner/teacher/viewer）
//
// ── 设计纪律（改之前先读） ──────────────────────────────────────────────
// ① **落盘是真相，内存只是热缓存**。任何"内存里有、库里没有"的状态都视为不存在。
// ② 历史采用**懒加载 + LRU 上限**：3000 班 × 200 条全塞内存 ≈ 120MB，2GB 云机扛不住。
//    只有被订阅/被读过的班才进内存，超出 CLASS_ROOM_CACHE_CLASSES 就淘汰最久未用的。
// ③ **设备自报一律不可信**：本文件只认调用方传进来的 canonical classCode，
//    编码/归属校验在调用方（class-dir / registry）完成，这里不重复也不放宽。
// ④ 写库失败**必须抛出/回 false**，不允许"内存写了就当成功"——重启即丢的状态是假的。
// ⑤ 高频帧（thumbnail）不进库：15s/台 × 3000 台会把库和磁盘写穿，且 Timeline 本就不显示。

import { DatabaseSync } from 'node:sqlite';
import { CLASS_ROOM_DB_FILE, CLASS_EVENT_LOG_KEEP, CLASS_ROOM_CACHE_CLASSES } from './config.js';

let db = null;

/** 热缓存：classCode -> { events: Array<ev>, loaded: boolean, seq: number }。
 *  只缓存被读/写过的班；超出上限按"最久未用"淘汰（Map 保序，重插即移到末尾）。 */
const hot = new Map();

/** 合法角色（由强到弱）。owner 只有建班者/管理员可给。 */
export const CLASS_ROLES = ['owner', 'teacher', 'viewer'];

/** 角色强弱（数字大的权限高），用于"加成员不降权"判定。 */
const ROLE_RANK = { owner: 3, teacher: 2, viewer: 1 };

function touch(code, rec) {
  // 重新插入 = 移到 Map 末尾（LRU 语义）
  hot.delete(code);
  hot.set(code, rec);
  while (hot.size > CLASS_ROOM_CACHE_CLASSES) {
    const oldest = hot.keys().next().value;
    if (oldest === undefined) break;
    hot.delete(oldest);
  }
  return rec;
}

/**
 * 打开房间库。**幂等**（index.js 启动调一次）。
 * @param {string} [dbFile] 覆盖路径（单测用临时文件，不碰生产）
 */
export function initClassRoom(dbFile = CLASS_ROOM_DB_FILE) {
  if (db) return { ok: true, file: dbFile, members: 0 };
  db = new DatabaseSync(dbFile);
  db.exec(`
    CREATE TABLE IF NOT EXISTS class_events (
      seq        INTEGER PRIMARY KEY AUTOINCREMENT,
      class_code TEXT    NOT NULL,
      at         TEXT    NOT NULL,
      at_ms      INTEGER NOT NULL,
      kind       TEXT    NOT NULL,
      uid        TEXT    NOT NULL DEFAULT '',
      data       TEXT    NOT NULL DEFAULT '{}'
    );
    CREATE INDEX IF NOT EXISTS idx_cev_class ON class_events(class_code, seq);
    CREATE INDEX IF NOT EXISTS idx_cev_time  ON class_events(class_code, at_ms);

    CREATE TABLE IF NOT EXISTS class_members (
      class_code   TEXT    NOT NULL,
      subject_type TEXT    NOT NULL DEFAULT 'user',
      subject_id   TEXT    NOT NULL,
      role         TEXT    NOT NULL DEFAULT 'viewer',
      added_by     TEXT    NOT NULL DEFAULT '',
      added_at     INTEGER NOT NULL,
      PRIMARY KEY (class_code, subject_type, subject_id)
    );
    CREATE INDEX IF NOT EXISTS idx_cmem_subject ON class_members(subject_type, subject_id);
  `);
  const n = db.prepare('SELECT COUNT(*) AS c FROM class_members').get();
  return { ok: true, file: dbFile, members: Number(n?.c || 0) };
}

/** 单测用：关实例（生产不要调）。 */
export function closeClassRoom() {
  if (db) db.close();
  db = null;
  hot.clear();
}

function needDb() {
  if (!db) throw new Error('class-room 未初始化（先在 index.js 调 initClassRoom）');
  return db;
}

function rowToEvent(r) {
  let data = {};
  try { data = JSON.parse(r.data || '{}'); } catch { data = { raw: String(r.data || '') }; }
  return { id: Number(r.seq), at: r.at, kind: r.kind, uid: r.uid, data };
}

/* ───────────────────────── 事件历史 ───────────────────────── */

/**
 * 追加一条事件（写库 + 更新热缓存）。
 * @param {string} code canonical 班级码
 * @param {{at?:string, kind:string, uid?:string, data?:object}} ev
 * @returns {{ok:true, id:number} | {ok:false, error:string}}
 */
export function appendEvent(code, ev) {
  const c = String(code ?? '').trim();
  if (!c) return { ok: false, error: '缺班级码' };
  const kind = String(ev?.kind ?? '').trim();
  if (!kind) return { ok: false, error: '缺事件 kind' };
  const at = ev?.at || new Date().toISOString();
  const atMs = Date.parse(at) || Date.now();
  let dataRaw = '{}';
  try { dataRaw = JSON.stringify(ev?.data ?? {}); }
  catch { return { ok: false, error: '事件数据无法序列化' }; }

  try {
    const r = needDb().prepare(
      'INSERT INTO class_events (class_code, at, at_ms, kind, uid, data) VALUES (?, ?, ?, ?, ?, ?)'
    ).run(c, at, atMs, kind, String(ev?.uid ?? ''), dataRaw);
    const id = Number(r.lastInsertRowid || 0);

    // 热缓存同步（只对已加载的班；没加载的班等它被读时从库读，不做无谓的内存占用）
    const rec = hot.get(c);
    if (rec) {
      rec.events.push({ id, at, kind, uid: String(ev?.uid ?? ''), data: JSON.parse(dataRaw) });
      rec.seq = id;
      trimHot(c, rec);
      touch(c, rec);
    }
    return { ok: true, id };
  } catch (e) {
    return { ok: false, error: `写库失败：${e?.message || e}` };
  }
}

/** 环形裁剪：内存缓存超出 KEEP 条就丢最旧。 */
function trimHot(code, rec) {
  if (rec.events.length > CLASS_EVENT_LOG_KEEP) {
    rec.events.splice(0, rec.events.length - CLASS_EVENT_LOG_KEEP);
  }
}

/** 库内环形裁剪（**低频**调用：每班每 CLASS_ROOM_PRUNE_EVERY 条写一次即可，
 *  每条都删一遍等于把 sqlite 当垃圾场刷）。 */
const pruneCounter = new Map();
export function maybePrune(code, every = 50) {
  const c = String(code ?? '').trim();
  const n = (pruneCounter.get(c) || 0) + 1;
  if (n < every) { pruneCounter.set(c, n); return { pruned: 0 }; }
  pruneCounter.set(c, 0);
  try {
    const r = needDb().prepare(
      `DELETE FROM class_events
        WHERE class_code = ?
          AND seq NOT IN (SELECT seq FROM class_events WHERE class_code = ? ORDER BY seq DESC LIMIT ?)`
    ).run(c, c, CLASS_EVENT_LOG_KEEP);
    return { pruned: Number(r.changes || 0) };
  } catch { return { pruned: 0 }; }
}

/** 懒加载一个班的历史进热缓存。 */
function ensureLoaded(code) {
  let rec = hot.get(code);
  if (rec && rec.loaded) return touch(code, rec);
  const rows = needDb().prepare(
    'SELECT seq, at, kind, uid, data FROM class_events WHERE class_code = ? ORDER BY seq DESC LIMIT ?'
  ).all(code, CLASS_EVENT_LOG_KEEP);
  const events = rows.reverse().map(rowToEvent);
  rec = rec || { events: [], loaded: false, seq: 0 };
  rec.events = events;
  rec.loaded = true;
  rec.seq = events.length ? events[events.length - 1].id : 0;
  return touch(code, rec);
}

/**
 * 取一个班最近 N 条事件（新→旧 取尾，再正序返回）。
 * 与旧内存实现的 `recentEvents` 完全同语义，但**重启不丢**。
 */
export function recentEvents(code, limit = 20) {
  const c = String(code ?? '').trim();
  if (!c) return [];
  try {
    const rec = ensureLoaded(c);
    return rec.events.slice(Math.max(0, rec.events.length - limit)).map((e) => ({ ...e }));
  } catch { return []; }
}

/** 一个班的事件总条数（诊断用，不加载全量）。 */
export function countEvents(code) {
  const c = String(code ?? '').trim();
  if (!c) return 0;
  try {
    const r = needDb().prepare('SELECT COUNT(*) AS c FROM class_events WHERE class_code = ?').get(c);
    return Number(r?.c || 0);
  } catch { return 0; }
}

/** 删班时清历史（房间销毁）。返回删除条数。 */
export function dropRoomEvents(code) {
  const c = String(code ?? '').trim();
  if (!c) return { deleted: 0 };
  try {
    const r = needDb().prepare('DELETE FROM class_events WHERE class_code = ?').run(c);
    hot.delete(c);
    pruneCounter.delete(c);
    return { deleted: Number(r.changes || 0) };
  } catch { return { deleted: 0 }; }
}

/* ───────────────────────── 成员 ───────────────────────── */

function normRole(role) {
  const r = String(role ?? '').trim().toLowerCase();
  return CLASS_ROLES.includes(r) ? r : 'viewer';
}

/**
 * 加/改一个成员（人可以被添加）。
 *
 * 幂等 + **不降权**：同一个人重复加，角色只会往高处走不会往低处掉
 * （防止"管理员加错了再补加一次"把 owner 降成 viewer 导致自己失权）。
 * 要真降权必须显式调 `setMemberRole`。
 *
 * @param {string} code canonical 班级码
 * @param {{id:string, type?:'user'|'device', role?:string, by?:string}} m
 * @returns {{ok:true, added:boolean, role:string} | {ok:false, error:string}}
 */
export function addMember(code, m = {}) {
  const c = String(code ?? '').trim();
  const id = String(m.id ?? '').trim();
  if (!c) return { ok: false, error: '缺班级码' };
  if (!id) return { ok: false, error: '缺成员标识' };
  const type = m.type === 'device' ? 'device' : 'user';
  const role = normRole(m.role);
  const by = String(m.by ?? '').slice(0, 128);
  try {
    const cur = needDb().prepare(
      'SELECT role FROM class_members WHERE class_code = ? AND subject_type = ? AND subject_id = ?'
    ).get(c, type, id);
    if (cur) {
      const next = (ROLE_RANK[role] || 0) > (ROLE_RANK[cur.role] || 0) ? role : cur.role;
      if (next !== cur.role) {
        needDb().prepare(
          'UPDATE class_members SET role = ? WHERE class_code = ? AND subject_type = ? AND subject_id = ?'
        ).run(next, c, type, id);
      }
      return { ok: true, added: false, role: next };
    }
    needDb().prepare(
      'INSERT INTO class_members (class_code, subject_type, subject_id, role, added_by, added_at) VALUES (?, ?, ?, ?, ?, ?)'
    ).run(c, type, id, role, by, Date.now());
    return { ok: true, added: true, role };
  } catch (e) {
    return { ok: false, error: `写库失败：${e?.message || e}` };
  }
}

/** 显式改角色（降权只走这里）。 */
export function setMemberRole(code, id, role, type = 'user') {
  const c = String(code ?? '').trim();
  const sid = String(id ?? '').trim();
  if (!c || !sid) return { ok: false, error: '缺班级码或成员标识' };
  try {
    const r = needDb().prepare(
      'UPDATE class_members SET role = ? WHERE class_code = ? AND subject_type = ? AND subject_id = ?'
    ).run(normRole(role), c, type, sid);
    return Number(r.changes || 0) > 0
      ? { ok: true }
      : { ok: false, error: '成员不存在' };
  } catch (e) { return { ok: false, error: `写库失败：${e?.message || e}` }; }
}

/** 移出成员（幂等）。 */
export function removeMember(code, id, type = 'user') {
  const c = String(code ?? '').trim();
  const sid = String(id ?? '').trim();
  if (!c || !sid) return { ok: false, error: '缺班级码或成员标识' };
  try {
    const r = needDb().prepare(
      'DELETE FROM class_members WHERE class_code = ? AND subject_type = ? AND subject_id = ?'
    ).run(c, type, sid);
    return { ok: true, removed: Number(r.changes || 0) > 0 };
  } catch (e) { return { ok: false, error: `写库失败：${e?.message || e}` }; }
}

/** 一个班的成员清单（按角色强弱降序，owner 在前）。 */
export function listMembers(code, type = 'user') {
  const c = String(code ?? '').trim();
  if (!c) return [];
  try {
    const rows = needDb().prepare(
      'SELECT subject_type, subject_id, role, added_by, added_at FROM class_members WHERE class_code = ? AND subject_type = ?'
    ).all(c, type);
    return rows
      .map((r) => ({
        id: r.subject_id, type: r.subject_type, role: r.role,
        addedBy: r.added_by, addedAt: Number(r.added_at || 0),
      }))
      .sort((a, b) => (ROLE_RANK[b.role] || 0) - (ROLE_RANK[a.role] || 0) || a.id.localeCompare(b.id));
  } catch { return []; }
}

/** 这个 uid 在某班的角色；不是成员回 null。**权限判定的唯一入口**。 */
export function roleOf(code, uid) {
  const c = String(code ?? '').trim();
  const u = String(uid ?? '').trim();
  if (!c || !u) return null;
  try {
    const r = needDb().prepare(
      "SELECT role FROM class_members WHERE class_code = ? AND subject_type = 'user' AND subject_id = ?"
    ).get(c, u);
    return r ? r.role : null;
  } catch { return null; }
}

/** 是不是成员（含任意角色）。 */
export function isMemberOf(code, uid) {
  return roleOf(code, uid) !== null;
}

/** 这个 uid 在哪些班里有角色（管理端"我的班级"用，避免全量拉目录）。 */
export function classesOf(uid) {
  const u = String(uid ?? '').trim();
  if (!u) return [];
  try {
    const rows = needDb().prepare(
      "SELECT class_code, role FROM class_members WHERE subject_type = 'user' AND subject_id = ?"
    ).all(u);
    return rows.map((r) => ({ classCode: r.class_code, role: r.role }));
  } catch { return []; }
}

/** 删班时清成员。 */
export function dropRoomMembers(code) {
  const c = String(code ?? '').trim();
  if (!c) return { deleted: 0 };
  try {
    const r = needDb().prepare('DELETE FROM class_members WHERE class_code = ?').run(c);
    return { deleted: Number(r.changes || 0) };
  } catch { return { deleted: 0 }; }
}

/** 房间是否"存在"（有人建过班或有历史或有成员）。诊断用。 */
export function roomStats(code) {
  const c = String(code ?? '').trim();
  if (!c) return { events: 0, members: 0, cached: false };
  return {
    events: countEvents(c),
    members: listMembers(c).length,
    cached: hot.has(c),
  };
}
