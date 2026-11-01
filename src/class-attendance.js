// 班级考勤（Class Attendance）—— 每班一份考勤记录（2026-10-31）
//
// 用户诉求（2026-10-31）：建立考勤能力，云端数据与 API 先行。
//   实盘核实：云端 grep attendance / 考勤 = 0 命中 —— 连 API 都没有，
//   旧文档写「考勤 API 已就位」是错的，这里从零建。
//
// 数据模型（与 class-data 共用 class-room.db 的同一个 sqlite 连接，不另开第二个）：
//   class_attendance —— 每班一份考勤记录：
//     class_code   班级码（canonical）
//     student_id   学生内部 id（对应 class_students.student_id，落点必须来自本班名单）
//     student_no   学号（冗余存一份，便于按学号查 / 名单变动后仍能溯源）
//     student_name 姓名快照（记录时从名单取，名单删除后记录仍可读）
//     date         日期 YYYY-MM-DD
//     session      节次 / 时段（空串 = 全天未分节次）
//     status       出勤 present / 迟到 late / 请假 leave / 缺勤 absent
//     recorder     记录人 uid
//     recorded_at  首次记录时间戳（ms）
//     updated_at   最近更新时间戳（ms）
//     tenant_id    哪所学校（2026-11-02 多租户）
//   自然键 (tenant_id, class_code, student_id, date, session) —— 同一学生同一天同一节次只一行，
//   重复记录 = upsert（老师改判迟到→缺勤，反复点不长出多行）。
//
// ── 多租户（2026-11-02 追加，改这个文件前必读）────────────────────────────
// 老自然键只有 (class_code, student_id, date, session)。多校之后两所学校都用站点
// 老外部码 `class_0102` 时，两校考勤**写进同一批行**：A 校老师点「全部出勤」，
// B 校当天的缺勤记录被 upsert 覆盖掉。考勤是会被拿去对账、写进家长通知的东西，
// 覆盖掉不会报错，只会静默错。
//
// ⇒ 因此：**每张表、每个查询、包括解析学生名单，都必须带 tenant_id**。
//   注意 `resolveStudent` 查的是 `class_students` —— 那张表也已经有 tenant_id 了，
//   但那是 class-data.js 的地盘；这里要**按同一个租户去查**，否则会出现
//   「A 校的学号在 B 校名单里解析成功」（学号两校都会从 01 开始）。
//
// 设计纪律（沿用 class-data.js）：
//   ① 落盘是真相：写失败必须回 false/抛，绝不允许"内存改了就当成功"。
//   ② 考勤必须绑定本班名单里的学生：用 studentId 或 studentNo 解析，解析不到即拒
//      （不允许凭空记一个不存在的人，否则统计会混入幽灵行）。
//   ③ 状态只允许四个枚举值；非法值一律拒。
//   ④ 删班要清干净（dropClassAttendance）：不留孤儿行，与 class-data 的 dropClassData 同步。
//   ⑤ 所有入口过长度上限：date 必须 YYYY-MM-DD，session 短，status 短，recorder 短。

import { sharedDb } from './class-room.js';
import { DEFAULT_TENANT, normalizeTenantId } from './tenant.js';

/** 考勤状态枚举（与前端 UI 约定一致）。 */
export const ATTENDANCE_STATUSES = ['present', 'late', 'leave', 'absent'];
/** 状态中文标签（落库存英文枚举，展示用标签在前端映射；这里也备一份便于审计/返回）。 */
export const ATTENDANCE_STATUS_LABELS = {
  present: '出勤',
  late: '迟到',
  leave: '请假',
  absent: '缺勤',
};

/** 单日考勤上限（够 60 人 × 8 节次）。 */
const RECORDS_MAX_PER_DAY = 8000;
const FIELD_MAX = { studentName: 40, studentNo: 32, session: 16, recorder: 64 };
/** 日期格式 YYYY-MM-DD（朴素校验，不引第三方）。 */
const DATE_RE = /^\d{4}-\d{2}-\d{2}$/;

/**
 * 建表（幂等）。必须在 initClassRoom 之后调用（借它的连接）。
 * @returns {{ok:boolean, records:number}}
 */
export function initClassAttendance() {
  const db = sharedDb();
  db.exec(`
    CREATE TABLE IF NOT EXISTS class_attendance (
      class_code    TEXT    NOT NULL,
      student_id    TEXT    NOT NULL,
      student_no    TEXT    NOT NULL DEFAULT '',
      student_name  TEXT    NOT NULL DEFAULT '',
      date          TEXT    NOT NULL,
      session       TEXT    NOT NULL DEFAULT '',
      status        TEXT    NOT NULL,
      recorder      TEXT    NOT NULL DEFAULT '',
      recorded_at   INTEGER NOT NULL,
      updated_at    INTEGER NOT NULL,
      tenant_id     TEXT    NOT NULL DEFAULT '${DEFAULT_TENANT}',
      PRIMARY KEY (tenant_id, class_code, student_id, date, session)
    );
    CREATE INDEX IF NOT EXISTS idx_catt_day    ON class_attendance(tenant_id, class_code, date);
    CREATE INDEX IF NOT EXISTS idx_catt_session ON class_attendance(tenant_id, class_code, date, session);
    CREATE INDEX IF NOT EXISTS idx_catt_no     ON class_attendance(tenant_id, class_code, student_no, date);
  `);
  migrateAttendanceToTenant(db);
  const r = db.prepare('SELECT COUNT(*) AS c FROM class_attendance').get();
  return { ok: true, records: Number(r?.c || 0) };
}

/**
 * 存量库迁移（2026-11-02 · 多租户）。
 *
 * 与 class-dir / class-data / class-room 同一套三步（那三处已经踩过顺序坑）：
 *   ① 补列 `tenant_id`，存量行落默认组织（原地不动，不丢历史考勤）
 *   ② 主键不含租户 ⇒ **重建表**（SQLite 没有 ALTER PRIMARY KEY）
 *   ③ 索引必须建在重建**之后**，否则老库执行到 `(tenant_id, class_code, date)`
 *      当场 `no such column: tenant_id`
 *   ④ 判据看 PRAGMA table_info 的主键列，不是「有没有 tenant_id 列」——
 *      只补列不改主键等于白改：还能继续写重复行。
 */
function migrateAttendanceToTenant(db) {
  const info = db.prepare('PRAGMA table_info(class_attendance)').all();
  if (!info.length) return;
  const cols = info.map((c) => String(c.name));
  const pk = info.filter((c) => Number(c.pk) > 0)
    .sort((a, b) => Number(a.pk) - Number(b.pk))
    .map((c) => String(c.name));
  const wantPk = ['tenant_id', 'class_code', 'student_id', 'date', 'session'];
  const pkOk = pk.length === wantPk.length && wantPk.every((c, i) => pk[i] === c);

  if (!cols.includes('tenant_id')) {
    db.exec(`ALTER TABLE class_attendance ADD COLUMN tenant_id TEXT NOT NULL DEFAULT '${DEFAULT_TENANT}'`);
  }
  if (!pkOk) {
    db.exec('BEGIN');
    try {
      db.exec('DROP TABLE IF EXISTS class_attendance__new');
      db.exec(`CREATE TABLE class_attendance__new (
        class_code    TEXT    NOT NULL,
        student_id    TEXT    NOT NULL,
        student_no    TEXT    NOT NULL DEFAULT '',
        student_name  TEXT    NOT NULL DEFAULT '',
        date          TEXT    NOT NULL,
        session       TEXT    NOT NULL DEFAULT '',
        status        TEXT    NOT NULL,
        recorder      TEXT    NOT NULL DEFAULT '',
        recorded_at   INTEGER NOT NULL,
        updated_at    INTEGER NOT NULL,
        tenant_id     TEXT    NOT NULL DEFAULT '${DEFAULT_TENANT}',
        PRIMARY KEY (tenant_id, class_code, student_id, date, session)
      )`);
      db.exec(`INSERT OR IGNORE INTO class_attendance__new
                 (class_code, student_id, student_no, student_name, date, session,
                  status, recorder, recorded_at, updated_at, tenant_id)
               SELECT class_code, student_id, student_no, student_name, date, session,
                      status, recorder, recorded_at, updated_at,
                      COALESCE(NULLIF(tenant_id,''),'${DEFAULT_TENANT}')
                 FROM class_attendance`);
      db.exec('DROP TABLE class_attendance');
      db.exec('ALTER TABLE class_attendance__new RENAME TO class_attendance');
      db.exec('COMMIT');
    } catch (e) {
      try { db.exec('ROLLBACK'); } catch { /* 已回滚 */ }
      throw e;
    }
  }
  db.exec('DROP INDEX IF EXISTS idx_catt_day');
  db.exec('DROP INDEX IF EXISTS idx_catt_session');
  db.exec('DROP INDEX IF EXISTS idx_catt_no');
  db.exec('CREATE INDEX IF NOT EXISTS idx_catt_day     ON class_attendance(tenant_id, class_code, date)');
  db.exec('CREATE INDEX IF NOT EXISTS idx_catt_session ON class_attendance(tenant_id, class_code, date, session)');
  db.exec('CREATE INDEX IF NOT EXISTS idx_catt_no      ON class_attendance(tenant_id, class_code, student_no, date)');
}

/* ───────────────────────── 小工具 ───────────────────────── */

function clip(v, max) {
  const s = String(v ?? '').trim();
  return s.length > max ? s.slice(0, max) : s;
}

function rowToAttendance(r) {
  return {
    classCode: r.class_code,
    studentId: r.student_id,
    studentNo: r.student_no,
    studentName: r.student_name,
    date: r.date,
    session: r.session,
    status: r.status,
    statusLabel: ATTENDANCE_STATUS_LABELS[r.status] || r.status,
    recorder: r.recorder,
    recordedAt: Number(r.recorded_at),
    updatedAt: Number(r.updated_at),
  };
}

/** 校验状态枚举。 */
function normStatus(status) {
  const s = String(status ?? '').trim().toLowerCase();
  return ATTENDANCE_STATUSES.includes(s) ? s : null;
}

/**
 * 解析考勤要绑定的学生：优先 studentId，否则用 studentNo 去本班名单查。
 * 解析不到（不在本班名单）一律回 false —— 不允许凭空考勤。
 *
 * ⚠️ 名单是 `class_students`，**按租户查**：学号两校都从 01 起，不按租户查就会
 *    「B 校的学号 01 在 A 校名单里解析成功」，把考勤记到隔壁学校的学生头上。
 *
 * @returns {{ok:true, studentId:string, studentNo:string, studentName:string}
 *          | {ok:false, error:string}}
 */
function resolveStudent(classCode, input = {}, tenantId = DEFAULT_TENANT) {
  const db = sharedDb();
  const tn = normalizeTenantId(tenantId);
  if (input.studentId) {
    const sid = clip(input.studentId, 64);
    const r = db.prepare(
      'SELECT student_id, student_no, name FROM class_students WHERE tenant_id = ? AND class_code = ? AND student_id = ?'
    ).get(tn, classCode, sid);
    if (!r) return { ok: false, error: '该学生不在本班名单（studentId 不匹配）' };
    return { ok: true, studentId: r.student_id, studentNo: r.student_no, studentName: r.name };
  }
  if (input.studentNo) {
    const no = clip(input.studentNo, FIELD_MAX.studentNo);
    const r = db.prepare(
      'SELECT student_id, student_no, name FROM class_students WHERE tenant_id = ? AND class_code = ? AND student_no = ?'
    ).get(tn, classCode, no);
    if (!r) return { ok: false, error: '该学号不在本班名单，请先导入名单或改用 studentId' };
    return { ok: true, studentId: r.student_id, studentNo: r.student_no, studentName: r.name };
  }
  return { ok: false, error: '考勤记录必须指定 studentId 或 studentNo' };
}

/* ───────────────────────── 单条记录 ───────────────────────── */

/**
 * 记录 / 改判一条考勤（upsert）。
 * @param {string} classCode
 * @param {{studentId?:string, studentNo?:string, date:string, session?:string, status:string}} input
 * @param {string} [by] 记录人 uid（缺省 system）
 * @param {string} [tenantId] 租户（缺省 = 默认组织）
 * @returns {{ok:true, record:object, replaced:boolean} | {ok:false, error:string}}
 */
export function recordAttendance(classCode, input = {}, by = 'system', tenantId = DEFAULT_TENANT) {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  const tn = normalizeTenantId(tenantId);
  const date = clip(input.date, 10);
  if (!DATE_RE.test(date)) return { ok: false, error: '日期格式必须是 YYYY-MM-DD' };
  const session = clip(input.session, FIELD_MAX.session);
  const status = normStatus(input.status);
  if (!status) return { ok: false, error: `考勤状态必须是 ${ATTENDANCE_STATUSES.join(' / ')} 之一` };
  const rec = resolveStudent(code, input, tn);
  if (!rec.ok) return rec;
  const recorder = clip(by, FIELD_MAX.recorder);
  const now = Date.now();
  const db = sharedDb();
  const existing = db.prepare(
    'SELECT student_id FROM class_attendance WHERE tenant_id = ? AND class_code = ? AND student_id = ? AND date = ? AND session = ?'
  ).get(tn, code, rec.studentId, date, session);
  db.prepare(`
    INSERT INTO class_attendance
      (class_code, student_id, student_no, student_name, date, session, status, recorder, recorded_at, updated_at, tenant_id)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(tenant_id, class_code, student_id, date, session) DO UPDATE SET
      student_no   = excluded.student_no,
      student_name = excluded.student_name,
      status       = excluded.status,
      recorder     = excluded.recorder,
      updated_at   = excluded.updated_at
  `).run(code, rec.studentId, rec.studentNo, rec.studentName, date, session, status, recorder, now, now, tn);
  const row = db.prepare(
    'SELECT * FROM class_attendance WHERE tenant_id = ? AND class_code = ? AND student_id = ? AND date = ? AND session = ?'
  ).get(tn, code, rec.studentId, date, session);
  return { ok: true, record: rowToAttendance(row), replaced: !!existing };
}

/** 把一行输入规整成记录结果（批量用）。 */
function recordOne(classCode, row, by, tenantId) {
  const r = recordAttendance(classCode, row, by, tenantId);
  return r.ok
    ? { ok: true, record: r.record, replaced: r.replaced }
    : { ok: false, error: r.error };
}

/**
 * 批量记录（老师一次性提交全班）。逐行 upsert，互不阻断；
 * 单行失败只记 error，不影响其余行。
 * @param {string} classCode
 * @param {Array} rows 每行同 recordAttendance 的 input
 * @param {string} [by]
 * @param {string} [tenantId]
 */
export function batchRecord(classCode, rows = [], by = 'system', tenantId = DEFAULT_TENANT) {
  if (!Array.isArray(rows)) return { ok: false, error: '批量记录必须是数组' };
  if (rows.length === 0) return { ok: false, error: '批量记录不能为空' };
  if (rows.length > RECORDS_MAX_PER_DAY) {
    return { ok: false, error: `一次最多记录 ${RECORDS_MAX_PER_DAY} 条` };
  }
  const code = clip(classCode, 64);
  const tn = normalizeTenantId(tenantId);
  const results = [];
  let succeeded = 0;
  let failed = 0;
  for (let i = 0; i < rows.length; i++) {
    const r = recordOne(code, rows[i], by, tn);
    if (r.ok) { succeeded++; results.push({ index: i, ok: true, record: r.record }); }
    else { failed++; results.push({ index: i, ok: false, error: r.error }); }
  }
  return {
    ok: true, classCode: code, tenantId: tn, total: rows.length, succeeded, failed, results,
  };
}

/* ───────────────────────── 查询 ───────────────────────── */

/** 按班按日列出考勤（可进一步按 session 过滤）。按学号→姓名排序。 */
export function listByClassDate(classCode, date, session = '', tenantId = DEFAULT_TENANT) {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  if (!DATE_RE.test(clip(date, 10))) return { ok: false, error: '日期格式必须是 YYYY-MM-DD' };
  const tn = normalizeTenantId(tenantId);
  const d = clip(date, 10);
  const s = clip(session, FIELD_MAX.session);
  const db = sharedDb();
  const rows = s
    ? db.prepare(`SELECT * FROM class_attendance WHERE tenant_id = ? AND class_code = ? AND date = ? AND session = ?
                   ORDER BY (student_no = '') ASC, student_no ASC, student_name ASC`)
        .all(tn, code, d, s)
    : db.prepare(`SELECT * FROM class_attendance WHERE tenant_id = ? AND class_code = ? AND date = ?
                   ORDER BY (student_no = '') ASC, student_no ASC, student_name ASC`)
        .all(tn, code, d);
  return { ok: true, classCode: code, tenantId: tn, date: d, session: s, records: rows.map(rowToAttendance), count: rows.length };
}

/** 按班按日期区间列出（fromDate..toDate 含端点，空端点放开）。 */
export function listByClassRange(classCode, fromDate = '', toDate = '', tenantId = DEFAULT_TENANT) {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  const tn = normalizeTenantId(tenantId);
  const db = sharedDb();
  let sql = 'SELECT * FROM class_attendance WHERE tenant_id = ? AND class_code = ?';
  const args = [tn, code];
  if (fromDate && DATE_RE.test(clip(fromDate, 10))) { sql += ' AND date >= ?'; args.push(clip(fromDate, 10)); }
  if (toDate && DATE_RE.test(clip(toDate, 10))) { sql += ' AND date <= ?'; args.push(clip(toDate, 10)); }
  sql += " ORDER BY date ASC, (student_no = '') ASC, student_no ASC, student_name ASC";
  const rows = db.prepare(sql).all(...args);
  return { ok: true, classCode: code, tenantId: tn, fromDate: clip(fromDate, 10), toDate: clip(toDate, 10), records: rows.map(rowToAttendance), count: rows.length };
}

/** 取单条（按自然键）。 */
export function getAttendance(classCode, studentId, date, session = '', tenantId = DEFAULT_TENANT) {
  const code = clip(classCode, 64);
  const sid = clip(studentId, 64);
  const d = clip(date, 10);
  if (!code || !sid || !DATE_RE.test(d)) return { ok: false, error: '缺班级码 / 学生 / 合法日期' };
  const s = clip(session, FIELD_MAX.session);
  const tn = normalizeTenantId(tenantId);
  const row = sharedDb().prepare(
    'SELECT * FROM class_attendance WHERE tenant_id = ? AND class_code = ? AND student_id = ? AND date = ? AND session = ?'
  ).get(tn, code, sid, d, s);
  if (!row) return { ok: false, error: '考勤记录不存在' };
  return { ok: true, record: rowToAttendance(row) };
}

/* ───────────────────────── 改 / 删 ───────────────────────── */

/** 改判一条（只动 status，必要带 recorder）。 */
export function updateAttendance(classCode, studentId, date, session, patch = {}, by = 'system', tenantId = DEFAULT_TENANT) {
  const tn = normalizeTenantId(tenantId);
  const got = getAttendance(classCode, studentId, date, session, tn);
  if (!got.ok) return got;
  const status = normStatus(patch.status);
  if (!status) return { ok: false, error: `考勤状态必须是 ${ATTENDANCE_STATUSES.join(' / ')} 之一` };
  const recorder = patch.recorder !== undefined ? clip(patch.recorder, FIELD_MAX.recorder) : got.record.recorder;
  const now = Date.now();
  sharedDb().prepare(
    'UPDATE class_attendance SET status = ?, recorder = ?, updated_at = ? WHERE tenant_id = ? AND class_code = ? AND student_id = ? AND date = ? AND session = ?'
  ).run(status, recorder || clip(by, FIELD_MAX.recorder), now, tn,
    clip(classCode, 64), clip(studentId, 64), clip(date, 10), clip(session, FIELD_MAX.session));
  return getAttendance(classCode, studentId, date, session, tn);
}

/** 删一条（幂等：不存在也回 ok）。 */
export function removeAttendance(classCode, studentId, date, session = '', tenantId = DEFAULT_TENANT) {
  const code = clip(classCode, 64);
  const sid = clip(studentId, 64);
  const d = clip(date, 10);
  if (!code || !sid || !DATE_RE.test(d)) return { ok: false, error: '缺班级码 / 学生 / 合法日期' };
  const s = clip(session, FIELD_MAX.session);
  const tn = normalizeTenantId(tenantId);
  const r = sharedDb().prepare(
    'DELETE FROM class_attendance WHERE tenant_id = ? AND class_code = ? AND student_id = ? AND date = ? AND session = ?'
  ).run(tn, code, sid, d, s);
  return { ok: true, classCode: code, removed: Number(r?.changes || 0) };
}

/* ───────────────────────── 统计 ───────────────────────── */

/**
 * 某日考勤统计：四个状态计数 + 名单总人数 + 已记录人数 + 未记录人数。
 * 未记录 = 名单总人数 − 已记录人数（按学生去重，同一学生多节次只算一次已到）。
 */
export function attendanceStats(classCode, date, tenantId = DEFAULT_TENANT) {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  if (!DATE_RE.test(clip(date, 10))) return { ok: false, error: '日期格式必须是 YYYY-MM-DD' };
  const tn = normalizeTenantId(tenantId);
  const d = clip(date, 10);
  const db = sharedDb();
  const rows = db.prepare(
    'SELECT status, COUNT(*) AS c FROM class_attendance WHERE tenant_id = ? AND class_code = ? AND date = ? GROUP BY status'
  ).all(tn, code, d);
  const counts = { present: 0, late: 0, leave: 0, absent: 0 };
  for (const r of rows) if (r.status in counts) counts[r.status] = Number(r.c || 0);
  const distinctStudents = db.prepare(
    'SELECT COUNT(DISTINCT student_id) AS c FROM class_attendance WHERE tenant_id = ? AND class_code = ? AND date = ?'
  ).get(tn, code, d);
  const rosterSize = db.prepare(
    'SELECT COUNT(*) AS c FROM class_students WHERE tenant_id = ? AND class_code = ?'
  ).get(tn, code);
  const recorded = Number(distinctStudents?.c || 0);
  const total = Number(rosterSize?.c || 0);
  return {
    ok: true, classCode: code, tenantId: tn, date: d,
    counts,
    recorded,
    unrecorded: Math.max(0, total - recorded),
    rosterSize: total,
  };
}

/** 删班时清干净（与 dropClassData 同步调用）。 */
export function dropClassAttendance(classCode, tenantId = DEFAULT_TENANT) {
  const code = clip(classCode, 64);
  if (!code) return { ok: false, error: '缺班级码' };
  const tn = normalizeTenantId(tenantId);
  const r = sharedDb().prepare(
    'DELETE FROM class_attendance WHERE tenant_id = ? AND class_code = ?'
  ).run(tn, code);
  return { ok: true, classCode: code, removed: Number(r?.changes || 0) };
}

/** 统计（排障用：某班到底有没有考勤记录）。 */
export function attendanceCount(classCode, tenantId = DEFAULT_TENANT) {
  const code = clip(classCode, 64);
  const tn = normalizeTenantId(tenantId);
  const r = sharedDb().prepare(
    'SELECT COUNT(*) AS c FROM class_attendance WHERE tenant_id = ? AND class_code = ?'
  ).get(tn, code);
  return { ok: true, classCode: code, count: Number(r?.c || 0) };
}
