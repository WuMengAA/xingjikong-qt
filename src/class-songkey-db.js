// 星集控 · 班级点歌凭据的 SQLite 持久化（2026-10-18）
// ─────────────────────────────────────────────────────────────────────────────
// 为什么不直接写进 store.js：那个文件是**指令队列**的真源，且近期有并行改动；
// 点歌凭据是独立域，用独立表 + 独立文件更清楚，也避免两处抢同一个 db 句柄。
//
// 表结构：
//   song_keys(className TEXT PRIMARY KEY, enc TEXT NOT NULL, tail TEXT NOT NULL, updatedAt INTEGER NOT NULL)
//   · enc  = AES-256-GCM 密文（base64），**永远不存明文**
//   · tail = Key 末 4 位，仅供人工核对
//
// ⚠️ 与 store.js 同样的降级约定：**打开失败不让云端整体死掉**，
//    但必须大声报出来 —— 退化成"不持久化"意味着重启后各班要重新配 Key。
import path from 'node:path';
import fs from 'node:fs';
import { DatabaseSync } from 'node:sqlite';

const DB_FILE = process.env.STE_SONGKEY_DB
  || path.join(process.cwd(), 'songkey.db');

let db = null;

export function initSongKeyStore(log = console.log) {
  try {
    const dir = path.dirname(path.resolve(DB_FILE));
    if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
    db = new DatabaseSync(DB_FILE);
    db.exec('PRAGMA journal_mode = DELETE');
    db.exec('PRAGMA synchronous = FULL');
    db.exec(`CREATE TABLE IF NOT EXISTS song_keys (
      className TEXT PRIMARY KEY,
      enc       TEXT    NOT NULL,
      tail      TEXT    NOT NULL,
      updatedAt INTEGER NOT NULL
    )`);
    log(`[songkey] 凭据库就绪：${DB_FILE}`);
    return true;
  } catch (e) {
    // 不抛：云端不能因为点歌凭据打不开而整体不可用（其他功能还在服务教室）。
    db = null;
    log(`[songkey] FAIL 凭据库打开失败（本功能将不持久化，重启需重配）：${e.message}`);
    return false;
  }
}

/** 读全表 → Map<className, {enc,tail,updatedAt}>（与 class-songkey.js 的 read() 契约一致） */
export function readSongKeys() {
  const m = new Map();
  if (!db) return m;
  try {
    const rows = db.prepare('SELECT className, enc, tail, updatedAt FROM song_keys').all();
    for (const r of rows) m.set(r.className, { enc: r.enc, tail: r.tail, updatedAt: r.updatedAt });
  } catch (e) {
    console.error(`[songkey] 读取失败：${e.message}`);
  }
  return m;
}

/**
 * 整表覆盖写（与 class-songkey.js 的 write(Map) 契约一致）。
 * 为什么整表覆盖而不是逐条增量：班级数量是几十级、写入极低频（配置时一次），
 * 用事务整表替换最简单且不会出现"删了半截"的中间态。
 */
export function writeSongKeys(map) {
  if (!db) return;
  try {
    db.exec('BEGIN');
    db.exec('DELETE FROM song_keys');
    const ins = db.prepare('INSERT INTO song_keys (className, enc, tail, updatedAt) VALUES (?, ?, ?, ?)');
    for (const [cls, rec] of map) ins.run(cls, rec.enc, rec.tail, rec.updatedAt);
    db.exec('COMMIT');
  } catch (e) {
    try { db.exec('ROLLBACK'); } catch { /* 已回滚或未开启 */ }
    console.error(`[songkey] 写入失败：${e.message}`);
  }
}
