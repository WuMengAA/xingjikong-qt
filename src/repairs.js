// 星集控云端 · 设备报修（2026-10-07，《设备报修-第一版设计》落地）
//
// 数据模型（设计文档 §二）：
//   RepairTicket { id, uid, reporter, symptom, severity, status, at, note?, closedAt? }
//   status: open | processing | fixed | closed
//
// 存储：内存表 + repairs.json 落盘（云端根目录）。量小（学校几十台），
//   每次变更全量写文件即可；每日备份由 scripts/backup.mjs 带 repairs.json（已加）。
//
// 接口语义（设计文档 §三）：
//   GET    /api/repairs?status=open   → 列表（可按状态过滤）
//   POST   /api/repairs  {uid,symptom,severity} → 新建（报修人 = 令牌 uid 或 'admin'）
//   PATCH  /api/repairs/{id} {status,note} → 更新（处理中/已修复）
//   DELETE /api/repairs/{id}          → 关闭/删除
//
// 报修是资源类操作，不触碰设备控制权限；创建/处理都进 events.log（审计）。

import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const CLOUD_ROOT = join(__dirname, '..');
let REPAIRS_FILE = join(CLOUD_ROOT, 'repairs.json');

/** @type {Map<string, object>} id → ticket */
const repairs = new Map();
let seq = 0;   // 票号序号（tick-YYYYMMDD-### 用，每日归零由日期前缀天然区分）

// 从落盘文件加载（启动时；文件不存在 = 全新安装，空表起步）
export function loadRepairs() {
  try {
    if (!existsSync(REPAIRS_FILE)) return;
    const arr = JSON.parse(readFileSync(REPAIRS_FILE, 'utf8'));
    if (!Array.isArray(arr)) return;
    for (const t of arr) if (t && t.id) {
      repairs.set(String(t.id), t);
      // ⚠️ 2026-10-13 修：原来只把票灌回 Map、**从不回填 seq**。seq 是进程内变量，
      //    重启后从 0 开始 ⇒ 当天第一张新票又叫 tick-YYYYMMDD-001，与已存在的 id 相撞，
      //    repairs.set() 直接**覆盖旧票**（数据丢失且无任何报错）。这里把已有票号的最大值续上。
      const m = /-(\d+)$/.exec(String(t.id));
      if (m) seq = Math.max(seq, Number(m[1]));
    }
  } catch (e) {
    // 落盘损坏不致命：内存表照常跑，下次写入覆盖（审计留一行）
    console.error('[repairs] 加载 repairs.json 失败（继续以空表运行）：', e.message);
  }
}

// 全量落盘（每次变更后调用；量小，全量写最稳）
function saveRepairs() {
  try {
    const arr = [...repairs.values()].sort((a, b) => (a.id < b.id ? 1 : -1));
    writeFileSync(REPAIRS_FILE, JSON.stringify(arr, null, 2), 'utf8');
  } catch (e) {
    console.error('[repairs] 落盘失败：', e.message);
  }
}

function newId(now) {
  // tick-YYYYMMDD-###：同一天的票连号；日期前缀变了自然从头数（不冲突）
  const d = now.toISOString().slice(0, 10).replace(/-/g, '');
  return `tick-${d}-${String(++seq).padStart(3, '0')}`;
}

// ── CRUD（index.js 调）────────────────────────────────────────────

/** GET：列表。status 可过滤（open/processing/fixed/closed/全部）。按时间倒序。 */
export function listRepairs(status) {
  let arr = [...repairs.values()];
  if (status) arr = arr.filter((t) => t.status === status);
  return arr.sort((a, b) => (a.at < b.at ? 1 : -1));
}

/** POST：新建。校验 uid/symptom；severity 白名单。返回新票。 */
export function createRepair({ uid, symptom, severity, reporter }) {
  if (!uid || !String(uid).trim()) return { error: '缺设备 uid', http: 400 };
  const sym = String(symptom || '').trim();
  if (!sym) return { error: '缺症状描述', http: 400 };
  if (sym.length > 200) return { error: '症状描述过长（≤200 字）', http: 400 };
  const sev = ['low', 'normal', 'urgent'].includes(severity) ? severity : 'normal';
  const now = new Date();
  const t = {
    id: newId(now),
    uid: String(uid).trim(),
    reporter: reporter || 'admin',
    symptom: sym,
    severity: sev,
    status: 'open',
    at: now.toISOString(),
    note: null,
    closedAt: null,
  };
  repairs.set(t.id, t);
  saveRepairs();
  return { ticket: t };
}

/** PATCH：更新 status/note。返回更新后的票；id 不存在 → {error,http:404}。
 *  先全量校验再改字段：备注超长时不能留下"状态已改但整体被拒"的半套变更。 */
export function updateRepair(id, { status, note }) {
  const t = repairs.get(String(id));
  if (!t) return { error: `报修单不存在：${id}`, http: 404 };
  // 先校验再改字段：无效输入不能留下半套变更或静默成功。
  const hasStatus = status !== undefined && status !== null && status !== '';
  const hasNote = note !== undefined && note !== null;
  const st = hasStatus ? (['open', 'processing', 'fixed', 'closed'].includes(status) ? status : null) : null;
  if (hasStatus && !st) return { error: `无效状态：${status}（可选 open/processing/fixed/closed）`, http: 400 };
  if (hasNote && String(note).trim().length > 500) return { error: '备注过长（≤500 字）', http: 400 };
  if (!hasStatus && !hasNote) return { error: '至少要提供 status 或 note', http: 400 };
  if (st) t.status = st;
  if (hasNote) t.note = String(note).trim();
  if (t.status === 'closed' && !t.closedAt) t.closedAt = new Date().toISOString();
  if (t.status !== 'closed') t.closedAt = null;
  repairs.set(t.id, t);
  saveRepairs();
  return { ticket: t };
}

/** DELETE：删除（关闭）。id 不存在 → {error,http:404}。 */
export function deleteRepair(id) {
  const existed = repairs.delete(String(id));
  if (!existed) return { error: `报修单不存在：${id}`, http: 404 };
  saveRepairs();
  return { ok: true };
}

// 单测辅助：清空内存表与序号（仅测试用）
export function _resetRepairs() { repairs.clear(); seq = 0; }

// 单测辅助：把落盘目标指向临时文件（避免污染云端根目录的真实 repairs.json）。
// 传 null 恢复默认路径。
export function useRepairFile(path) { REPAIRS_FILE = path || join(CLOUD_ROOT, 'repairs.json'); }