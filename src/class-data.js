// 班级数据（Class Data）—— 学生名单 + 班级课表（2026-10-25）
//
// 用户裁决：**「云端数据库存储，不要本机了」**。
//   原来课表是每台机器各存一份本地档案（ScheduleEditor → profile_repository 落盘），
//   学生名单则根本不存在（"班级人员"只是老师/助教账号）。这两样都是**班级的事**，
//   必须跟班级走：换个老师端、换台设备、重装系统，数据都在。
//
// ── 存储（与 class-room 共用一个 sqlite 连接，不另开第二个）───────────────
//   为什么不独立开库：node:sqlite 默认回滚日志，同进程两连接写同一个文件会撞
//   SQLITE_BUSY；class-room 已经持有连接，这里直接借（sharedDb）。
//
//   class_students  —— 每班一份学生名录：姓名 / 学号 / 座位 / 备注
//   class_timetable —— 每班一份课表档案（原 ScheduleEditor 的 profile JSON 原样存）
//
// ── 设计纪律 ────────────────────────────────────────────────────────────
// ① 落库是真相：写失败必须回 false/抛，绝不允许"内存改了就当成功"。
// ② 学号是幂等键（非空时）：导入 Excel 反复跑不会长出重复学生。
// ③ 课表原样存（不做字段级解释）：profile 结构归课表编辑器所有，云端只负责
//    完整保存 + 版本时间 + 大小上限，避免两边结构定义分叉。
// ④ 所有入口都过长度/数量上限：这是唯一会被管理端批量灌数据的地方。
// ⑤ 删班要清干净（dropClassData）：不留孤儿行，与 class-room 的 drop 同步。

import { randomUUID } from 'node:crypto';
import { sharedDb } from './class-room.js';

/** 每班学生上限（够 60 人班级 × 8 个班还绰绰有余，防误导入整张表）。 */
export const STUDENT_MAX_PER_CLASS = 500;
/** 课表档案上限 512KB：正常课表 profile 几 KB，超了基本是误传大文件。 */
export const TIMETABLE_MAX_BYTES = 512 * 1024;

const FIELD_MAX = { name: 40, studentNo: 32, seat: 16, note: 200 };

/**
 * 建表（幂等）。必须在 initClassRoom 之后调用（借它的连接）。
 * @returns {{ok:boolean, students:number, timetables:number}}
 */
export function initClassData() {
  const db = sharedDb();
  db.exec(`
    CREATE TABLE IF NOT EXISTS class_students (
      class_code TEXT    NOT NULL,
      student_id TEXT    NOT NULL,
      name       TEXT    NOT NULL,
      student_no TEXT    NOT NULL DEFAULT '',
      seat       TEXT    NOT NULL DEFAULT '',
      note       TEXT    NOT NULL DEFAULT '',
      created_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL,
      PRIMARY KEY (class_code, student_id)
    );
    CREATE INDEX IF NOT EXISTS idx_cstu_class ON class_students(class_code);
    CREATE INDEX IF NOT EXISTS idx_cstu_no ON class_students(class_code, student_no);

    CREATE TABLE IF NOT EXISTS class_timetable (
      class_code TEXT    PRIMARY KEY,
      profile    TEXT    NOT NULL,
      updated_at INTEGER NOT NULL,
      updated_by TEXT    NOT NULL DEFAULT ''
    );
  `);
  const s = db.prepare('SELECT COUNT(*) AS c FROM class_students').get();
  const t = db.prepare('SELECT COUNT(*) AS c FROM class_timetable').get();
  return { ok: true, students: Number(s?.c || 0), timetables: Number(t?.c || 0) };
}

/* ───────────────────────── 小工具 ───────────────────────── */

function clip(v, max) {
  const s = String(v ?? '').trim();
  return s.length > max ? s.slice(0, max) : s;
}

function rowToStudent(r) {
  return {
    id: r.student_id,
    name: r.name,
    studentNo: r.student_no,
    seat: r.seat,
    note: r.note,
    updatedAt: Number(r.updated_at),
  };
}

/** 学生字段清洗：返回 {ok, patch} 或 {ok:false,error}。空姓名一律拒（名录里没有无名氏）。 */
function cleanStudent(input = {}, forUpdate = false) {
  const out = {};
  if (input.name !== undefined || !forUpdate) {
    const name = clip(input.name, FIELD_MAX.name);
    if (!name) return { ok: false, error: '学生姓名不能为空' };
    out.name = name;
  }
  if (input.studentNo !== undefined) out.studentNo = clip(input.studentNo, FIELD_MAX.studentNo);
  if (input.seat !== undefined) out.seat = clip(input.seat, FIELD_MAX.seat);
  if (input.note !== undefined) out.note = clip(input.note, FIELD_MAX.note);
  if (!forUpdate) {
    out.studentNo = out.studentNo || '';
    out.seat = out.seat || '';
    out.note = out.note || '';
  }
  return { ok: true, patch: out };
}

/* ───────────────────────── 学生名单 ───────────────────────── */

/** 全班学生（按座位号自然序，空座位排最后；同座位按学号）。 */
export function listStudents(classCode) {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  const rows = sharedDb()
    .prepare(`SELECT * FROM class_students WHERE class_code = ?
              ORDER BY (seat = '') ASC, seat ASC, student_no ASC, name ASC`)
    .all(code);
  return { ok: true, classCode: code, students: rows.map(rowToStudent), count: rows.length };
}

/** 加一个学生。带学号且该学号已存在 → 视为更新（幂等，导入不会长重复）。 */
export function addStudent(classCode, input = {}, by = '') {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  const c = cleanStudent(input, false);
  if (!c.ok) return c;
  const db = sharedDb();
  const n = db.prepare('SELECT COUNT(*) AS c FROM class_students WHERE class_code = ?').get(code);
  if (Number(n?.c || 0) >= STUDENT_MAX_PER_CLASS) {
    return { ok: false, error: `每班学生上限 ${STUDENT_MAX_PER_CLASS} 人` };
  }
  const now = Date.now();
  const no = c.patch.studentNo;
  if (no) {
    const dup = db.prepare('SELECT * FROM class_students WHERE class_code = ? AND student_no = ?').get(code, no);
    if (dup) {
      db.prepare(`UPDATE class_students SET name = ?, seat = ?, note = ?, updated_at = ?
                  WHERE class_code = ? AND student_id = ?`)
        .run(c.patch.name, c.patch.seat, c.patch.note, now, code, dup.student_id);
      return { ok: true, classCode: code, student: rowToStudent({ ...dup, name: c.patch.name, seat: c.patch.seat, note: c.patch.note, updated_at: now }), replaced: true };
    }
  }
  const id = `stu-${randomUUID().slice(0, 8)}`;
  db.prepare(`INSERT INTO class_students
              (class_code, student_id, name, student_no, seat, note, created_at, updated_at)
              VALUES (?, ?, ?, ?, ?, ?, ?, ?)`)
    .run(code, id, c.patch.name, no, c.patch.seat, c.patch.note, now, now);
  return {
    ok: true, classCode: code, replaced: false,
    student: { id, name: c.patch.name, studentNo: no, seat: c.patch.seat, note: c.patch.note, updatedAt: now },
  };
}

/** 改一个学生（按 id）。patch 里没给的字段保持原样。 */
export function updateStudent(classCode, studentId, patch = {}, by = '') {
  const code = clip(classCode, 64);
  const id = clip(studentId, 64);
  if (!code || !id) return { ok: false, error: '缺班级码或学生 id' };
  const db = sharedDb();
  const cur = db.prepare('SELECT * FROM class_students WHERE class_code = ? AND student_id = ?').get(code, id);
  if (!cur) return { ok: false, error: '学生不存在（可能已被移除）' };
  const c = cleanStudent(patch, true);
  if (!c.ok) return c;
  const next = {
    name: c.patch.name ?? cur.name,
    student_no: c.patch.studentNo ?? cur.student_no,
    seat: c.patch.seat ?? cur.seat,
    note: c.patch.note ?? cur.note,
  };
  // 学号改成别人已在用的 → 拒（否则幂等键打架）
  if (next.student_no) {
    const dup = db.prepare('SELECT student_id FROM class_students WHERE class_code = ? AND student_no = ?')
      .get(code, next.student_no);
    if (dup && dup.student_id !== id) return { ok: false, error: '该学号已被本班另一名学生占用' };
  }
  const now = Date.now();
  db.prepare(`UPDATE class_students SET name = ?, student_no = ?, seat = ?, note = ?, updated_at = ?
              WHERE class_code = ? AND student_id = ?`)
    .run(next.name, next.student_no, next.seat, next.note, now, code, id);
  return { ok: true, classCode: code, student: { id, name: next.name, studentNo: next.student_no, seat: next.seat, note: next.note, updatedAt: now } };
}

/** 移出一个学生（幂等：不存在也回 ok）。 */
export function removeStudent(classCode, studentId) {
  const code = clip(classCode, 64);
  const id = clip(studentId, 64);
  if (!code || !id) return { ok: false, error: '缺班级码或学生 id' };
  const r = sharedDb().prepare('DELETE FROM class_students WHERE class_code = ? AND student_id = ?').run(code, id);
  return { ok: true, classCode: code, removed: Number(r?.changes || 0) };
}

/**
 * 批量导入（Excel 粘贴/上传都走这里）。
 * 幂等规则：有学号 → 同学号更新；没学号 → 同名同座位视为已存在（跳过），否则新增。
 * @param {Array<{name,studentNo,seat,note}>} rows
 */
export function importStudents(classCode, rows = [], by = '') {
  if (!Array.isArray(rows)) return { ok: false, error: '导入内容必须是数组' };
  if (rows.length > STUDENT_MAX_PER_CLASS) {
    return { ok: false, error: `一次最多导入 ${STUDENT_MAX_PER_CLASS} 行` };
  }
  let added = 0, updated = 0, skipped = 0, bad = 0;
  const errors = [];
  for (const row of rows) {
    if (!row || typeof row !== 'object') { bad++; continue; }
    const c = cleanStudent(row, false);
    if (!c.ok) { bad++; if (errors.length < 5) errors.push(c.error); continue; }
    const db = sharedDb();
    const code = clip(classCode, 64);
    const no = c.patch.studentNo;
    const now = Date.now();
    let hit = null;
    if (no) hit = db.prepare('SELECT * FROM class_students WHERE class_code = ? AND student_no = ?').get(code, no);
    if (!hit) {
      hit = db.prepare(`SELECT * FROM class_students
                        WHERE class_code = ? AND name = ? AND seat = ? AND student_no = ''`)
        .get(code, c.patch.name, c.patch.seat);
    }
    if (hit) {
      db.prepare(`UPDATE class_students SET name = ?, student_no = ?, seat = ?, note = ?, updated_at = ?
                  WHERE class_code = ? AND student_id = ?`)
        .run(c.patch.name, no || hit.student_no, c.patch.seat, c.patch.note, now, code, hit.student_id);
      updated++;
    } else {
      const id = `stu-${randomUUID().slice(0, 8)}`;
      db.prepare(`INSERT INTO class_students
                  (class_code, student_id, name, student_no, seat, note, created_at, updated_at)
                  VALUES (?, ?, ?, ?, ?, ?, ?, ?)`)
        .run(code, id, c.patch.name, no, c.patch.seat, c.patch.note, now, now);
      added++;
    }
    if (row.__skipped) skipped++;
  }
  return { ok: true, classCode: clip(classCode, 64), added, updated, skipped, bad, errors,
           count: Number(sharedDb().prepare('SELECT COUNT(*) AS c FROM class_students WHERE class_code = ?')
             .get(clip(classCode, 64))?.c || 0) };
}

/* ───────────────────────── 班级课表 ───────────────────────── */

/** 取本班课表档案（没存过 → {ok:true, profile:null}，不编造默认课表）。 */
export function getTimetable(classCode) {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  const r = sharedDb().prepare('SELECT * FROM class_timetable WHERE class_code = ?').get(code);
  if (!r) return { ok: true, classCode: code, profile: null, updatedAt: 0, updatedBy: '' };
  let profile = null;
  try { profile = JSON.parse(r.profile); } catch { profile = null; }
  return { ok: true, classCode: code, profile, updatedAt: Number(r.updated_at), updatedBy: r.updated_by };
}

/**
 * 保存本班课表（整份覆盖，原样存）。
 * 校验只做「必须是对象/数组 + 不超 512KB」—— 字段语义归课表编辑器，云端不解释。
 */
export function saveTimetable(classCode, profile, by = '') {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  if (profile === null || typeof profile !== 'object') {
    return { ok: false, error: '课表内容必须是对象（课表编辑器的整份档案）' };
  }
  let raw;
  try { raw = JSON.stringify(profile); } catch { return { ok: false, error: '课表内容无法序列化' }; }
  const bytes = Buffer.byteLength(raw, 'utf8');
  if (bytes > TIMETABLE_MAX_BYTES) {
    return { ok: false, error: `课表超过 ${Math.round(TIMETABLE_MAX_BYTES / 1024)}KB 上限（${Math.round(bytes / 1024)}KB）` };
  }
  const now = Date.now();
  sharedDb().prepare(`INSERT INTO class_timetable (class_code, profile, updated_at, updated_by)
                      VALUES (?, ?, ?, ?)
                      ON CONFLICT(class_code) DO UPDATE SET
                        profile = excluded.profile,
                        updated_at = excluded.updated_at,
                        updated_by = excluded.updated_by`)
    .run(code, raw, now, clip(by, 64));
  return { ok: true, classCode: code, updatedAt: now, bytes };
}

/** 删班时清干净（与 dropRoomMembers/dropRoomEvents 同步调用）。 */
export function dropClassData(classCode) {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  const db = sharedDb();
  const s = db.prepare('DELETE FROM class_students WHERE class_code = ?').run(code);
  const t = db.prepare('DELETE FROM class_timetable WHERE class_code = ?').run(code);
  return { ok: true, classCode: code, students: Number(s?.changes || 0), timetables: Number(t?.changes || 0) };
}

/** 统计（排障用：某班到底有没有名单/课表）。 */
export function classDataStats(classCode) {
  const code = clip(classCode, 64);
  const db = sharedDb();
  const s = db.prepare('SELECT COUNT(*) AS c FROM class_students WHERE class_code = ?').get(code);
  const t = db.prepare('SELECT updated_at FROM class_timetable WHERE class_code = ?').get(code);
  return { ok: true, classCode: code, students: Number(s?.c || 0), hasTimetable: !!t,
           timetableUpdatedAt: t ? Number(t.updated_at) : 0 };
}
