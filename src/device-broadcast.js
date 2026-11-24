// 设备侧广播排期只读端点（2026-11-24 · 方案 C 的 T2）
// ───────────────────────────────────────────────────────────────────────────
// 权威边界（他线裁决，见 ai-docs/docs/广播站平台/06-可开工清单与验收判据.md）：
//   排期真源在**广播端本地** schedule.db；云端只是**可选镜像**，单向（广播端 → 云）。
//   教室机只读自己班，不接受 uid 换班、不接受云端编辑 —— 不做"谁赢"的冲突逻辑。
//
// 为什么照 device-timetable.js 的范式：同一个 /api/device/ 分派入口、同一套设备
// 令牌鉴权、同一种 reason 枚举语义（200 + reason，让设备区分"没排期"与"网络坏"）。
// 不另开路由、不另造鉴权 —— 两份必然漂移。

import { sharedDb } from './class-room.js';
import { DEFAULT_TENANT, normalizeTenantId } from './tenant.js';

/** 排期镜像超过 24h 未更新即视为 stale（与他线 T2 契约一致）。 */
export const STALE_AFTER_MS = 24 * 60 * 60 * 1000;
/** 单次响应最多 500 条排期（够一天广播，防误灌整月）。 */
export const MAX_ITEMS = 500;

/** 初始化镜像表（幂等）。字段按他线 T2 契约的 items 结构存。 */
export function initBroadcastMirror() {
  sharedDb().exec(`
    CREATE TABLE IF NOT EXISTS broadcast_schedule_mirror (
      class_code   TEXT NOT NULL,
      tenant_id    TEXT NOT NULL,
      play_date    TEXT NOT NULL,
      rev          INTEGER NOT NULL DEFAULT 0,
      updated_at   INTEGER NOT NULL DEFAULT 0,
      items_json   TEXT NOT NULL DEFAULT '[]',
      PRIMARY KEY (class_code, tenant_id, play_date)
    );
  `);
}

function emptyReply(uid, classCode, reason, date) {
  return {
    ok: true,
    uid,
    classCode: classCode || '',
    className: '',
    date: date || '',
    rev: 0,
    items: [],
    stale: true,
    updatedAt: 0,
    reason,
  };
}

/**
 * 处理 GET /api/device/broadcast/schedule 的业务部分（鉴权由 deviceGet 完成）。
 * @param {object} deps { uid, classCodeOf, tenantOf, classNameOf, date, read }
 * @returns {{status:number, body:object, headers?:object}}
 */
export function deviceBroadcastSchedule({ uid, classCodeOf, tenantOf, classNameOf, date, read }) {
  const u = String(uid || '').trim();
  if (!u) return { status: 400, body: { ok: false, error: '缺少设备标识' } };

  const day = String(date || '').trim();
  if (!/^\d{4}-\d{2}-\d{2}$/.test(day)) {
    // 没给日期 = 今天（本机时区）；日期格式错 → NO_DATE，仍 200
    if (date === undefined || date === null || date === '') {
      // 调用方通常已填；这里不二次猜时区，交给 read 的默认参数
    } else {
      return { status: 200, body: emptyReply(u, '', 'NO_DATE', day) };
    }
  }

  let classCode = String(classCodeOf ? classCodeOf(u) : '').trim();
  // registry 对未绑定设备可能返回字符串 "null"（不是真 null）——不能当成班级码。
  if (classCode === 'null' || classCode === 'undefined') classCode = '';
  if (!classCode) return { status: 200, body: emptyReply(u, '', 'NO_CLASS', day) };

  const tenant = normalizeTenantId(tenantOf ? tenantOf(u) : DEFAULT_TENANT);
  const name = classNameOf ? String(classNameOf(classCode, tenant) || '') : '';

  let row;
  try {
    row = (read || readMirror)(classCode, tenant, day);
  } catch (e) {
    return { status: 500, body: { ok: false, error: `读排期失败：${e && e.message ? e.message : e}` } };
  }
  if (!row) return { status: 200, body: { ...emptyReply(u, classCode, 'NO_SCHEDULE', day), className: name } };

  let items;
  try {
    items = JSON.parse(row.items_json || '[]');
  } catch {
    return { status: 200, body: { ...emptyReply(u, classCode, 'NO_SCHEDULE', day), className: name } };
  }
  if (!Array.isArray(items)) items = [];
  // 按 seq 升序（契约验收点）
  items = items.slice(0, MAX_ITEMS).sort((a, b) => Number(a.seq) - Number(b.seq));
  const updatedAt = Number(row.updated_at) || 0;
  const stale = !updatedAt || (Date.now() - updatedAt) > STALE_AFTER_MS;

  return {
    status: 200,
    headers: {
      'cache-control': 'private, max-age=300',
      etag: String(row.rev),
    },
    body: {
      ok: true,
      uid: u,
      classCode,
      className: name,
      date: row.play_date,
      rev: Number(row.rev) || 0,
      items,
      stale,
      updatedAt,
      reason: '',
    },
  };
}

/** 默认读法：从镜像表按 (班, 租户, 日期) 取一行。 */
export function readMirror(classCode, tenantId, playDate) {
  try {
    return sharedDb().prepare(
      'SELECT class_code, play_date, rev, updated_at, items_json FROM broadcast_schedule_mirror ' +
      'WHERE class_code=? AND tenant_id=? AND play_date=? LIMIT 1'
    ).get(classCode, tenantId, playDate) || null;
  } catch {
    return null;
  }
}

/** 写镜像（广播端 → 云单向；管理端不调用，留作测试/运维注入）。 */
export function writeMirror({ classCode, tenantId, playDate, rev, items, updatedAt }) {
  const cc = String(classCode || '').trim();
  const pd = String(playDate || '').trim();
  if (!cc || !/^\d{4}-\d{2}-\d{2}$/.test(pd)) return false;
  if (!Array.isArray(items) || items.length > MAX_ITEMS) return false;
  try {
    sharedDb().prepare(
      'INSERT INTO broadcast_schedule_mirror (class_code, tenant_id, play_date, rev, updated_at, items_json) ' +
      'VALUES (?,?,?,?,?,?) ON CONFLICT(class_code,tenant_id,play_date) DO UPDATE SET ' +
      'rev=excluded.rev, updated_at=excluded.updated_at, items_json=excluded.items_json'
    ).run(cc, normalizeTenantId(tenantId), pd, Number(rev) || 0,
          Number(updatedAt) || Date.now(), JSON.stringify(items));
    return true;
  } catch {
    return false;
  }
}

/** 便捷默认依赖（index.js/device-api.js 只传 uid/date）。 */
export function deviceBroadcastDefault(deps) {
  return deviceBroadcastSchedule({
    classCodeOf: deps.classCodeOf,
    tenantOf: deps.tenantOf,
    classNameOf: deps.classNameOf,
    date: deps.date,
    read: deps.read || readMirror,
    ...deps,
  });
}
