// 租户（Tenant）真源 —— class_id 全链路多租户隔离的地基（2026-11-02）
//
// ── 为什么要有这个文件 ────────────────────────────────────────────────
// 在此之前，「一个学校」在云端**没有身份**：
//   · 班级码 `cls-<10hex>` 由**班名**派生（class-dir.js deriveClassCode），
//     而班名在全校范围内唯一、**在两所学校之间必然重复**（都有「1班」）。
//   · registry.knownDevices 是全局 Map，classCode 只是其中一个字段，没有学校维度。
//   · perms.canAccess 只看 classCode，看不到「这台设备属于哪所学校」。
// ⇒ 两所学校各有一个「1班」时，云端会把它们判成**同一个班**：名单、课表、考勤、
//    设备归属全部串台。这不是理论风险，是 deriveClassCode 的必然结果。
//
// 本文件做的事只有一件：**给班级与设备补上「哪所学校」这一维**，并提供一个
// 不许绕过的判据 `sameTenant()`。它不碰鉴权策略（那在 perms.js），也不碰班级目录
// 存储（那在 class-dir.js），只提供租户身份本身 —— 单一职责，可单测。
//
// 纪律（改这个文件前先读）：
//   · **默认租户** `DEFAULT_TENANT` 必须永远存在且不可删除 —— 存量数据（无 tenant
//     字段的班级/设备/claims）全部落到它名下，否则升级当刻就全量越权。
//   · 租户 id 一旦签发就不再改（同 class-dir 的 canonical 铁律）：改 id = 该租户下
//     所有班级与设备当场变成孤儿。
//   · **绝不接受客户端参数指定租户**（与 perms.js「schoolId 只能从票来」同口径）：
//     本模块只负责「票里的 schoolId → 租户记录」的解析，不做任何信任放行。

import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

/** 存量数据的归属：任何没有 tenant 信息的对象都落这里。**不可删除**。 */
export const DEFAULT_TENANT = 'tn_default';

/** 租户 id 格式：tn_ + [a-z0-9_-]{2,32}。与班级码同一「小写、稳定、可 grep」口径。 */
const TENANT_ID_RE = /^tn_[a-z0-9_-]{2,32}$/;

const DEFAULT_FILE = join(dirname(fileURLToPath(import.meta.url)), '..', 'tenants.json');

/** id -> { id, name, slug, disabled, createdAt, updatedAt } */
const tenants = new Map();
let file = DEFAULT_FILE;

function nowMs() { return Date.now(); }

/** slug 化：中文名也给出稳定可读的 id（无可用字符时退回哈希）。 */
export function tenantSlug(name) {
  const raw = String(name ?? '').trim().toLowerCase();
  if (!raw) return '';
  const ascii = raw.replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
  if (ascii.length >= 2) return ascii.slice(0, 32);
  // 纯中文名等 → 用哈希（稳定、无碰撞风险的实用近似）
  return createHash('sha256').update(raw, 'utf8').digest('hex').slice(0, 10);
}

/** 由显示名派生租户 id（同一名字永远同一 id，便于对账）。 */
export function deriveTenantId(name) {
  const slug = tenantSlug(name);
  return slug ? `tn_${slug}`.slice(0, 35) : '';
}

function normalizeRecord(r) {
  return {
    id: r.id,
    name: r.name || '',
    slug: r.slug || '',
    disabled: r.disabled === true,
    createdAt: Number(r.createdAt || 0),
    updatedAt: Number(r.updatedAt || 0),
  };
}

function persist() {
  const snapshot = {
    v: 1,
    updatedAt: new Date().toISOString(),
    tenants: [...tenants.values()].map(normalizeRecord),
  };
  const tmp = `${file}.tmp`;
  try {
    mkdirSync(dirname(file), { recursive: true });
    writeFileSync(tmp, JSON.stringify(snapshot, null, 2), 'utf8');
    renameSync(tmp, file);
    return true;
  } catch {
    return false; // 写盘失败不崩服务；调用方按返回值决定是否告警
  }
}

/**
 * 打开租户表（幂等）。**总是**保证默认租户存在 —— 这是存量数据的落点，
 * 缺了它升级当刻所有旧班级/旧设备都会变成"无租户"，判据只能一律拒绝（全线不可用）。
 */
export function initTenants(filePath = DEFAULT_FILE) {
  file = filePath;
  tenants.clear();
  let loaded = 0;
  try {
    if (existsSync(file)) {
      const raw = JSON.parse(readFileSync(file, 'utf8'));
      const list = Array.isArray(raw?.tenants) ? raw.tenants : [];
      for (const r of list) {
        if (!r || typeof r.id !== 'string' || !TENANT_ID_RE.test(r.id)) continue;
        tenants.set(r.id, normalizeRecord(r));
        loaded++;
      }
    }
  } catch {
    // 损坏不崩：退化为「只有默认租户」，原文件保留待人工处置
    tenants.clear();
    loaded = 0;
  }
  if (!tenants.has(DEFAULT_TENANT)) {
    tenants.set(DEFAULT_TENANT, {
      id: DEFAULT_TENANT,
      name: '默认组织（存量数据）',
      slug: 'default',
      disabled: false,
      createdAt: 0,
      updatedAt: 0,
    });
    persist();
  }
  return { ok: true, tenants: tenants.size, loaded, file };
}

/** 关掉当前实例（单测用；生产不要调）。 */
export function closeTenants() {
  tenants.clear();
}

export function getTenant(id) {
  const k = String(id ?? '').trim();
  return k ? (tenants.get(k) ?? null) : null;
}

export function listTenants() {
  return [...tenants.values()]
    .map(normalizeRecord)
    .sort((a, b) => a.id.localeCompare(b.id));
}

export function tenantCount() { return tenants.size; }

/**
 * 登记（或取回）一个租户。同名 → 同 id，幂等（照 class-dir 的「同名归一」范式）。
 * @param {{name:string, id?:string}} input
 * @returns {{ok:true, tenant:object, created:boolean} | {ok:false, error:string}}
 */
export function registerTenant({ name, id } = {}) {
  const nm = String(name ?? '').trim();
  const wanted = String(id ?? '').trim() || deriveTenantId(nm);
  if (!wanted) return { ok: false, error: '缺少租户名（无法派生 id）' };
  if (!TENANT_ID_RE.test(wanted)) {
    return { ok: false, error: `租户 id 不合法：${wanted}（要求 tn_ + 2~32 位小写字母/数字/_/-）` };
  }
  const existed = tenants.get(wanted);
  if (existed) {
    if (nm && !existed.name) { existed.name = nm; existed.updatedAt = nowMs(); persist(); }
    return { ok: true, tenant: normalizeRecord(existed), created: false };
  }
  const rec = {
    id: wanted,
    name: nm,
    slug: tenantSlug(nm),
    disabled: false,
    createdAt: nowMs(),
    updatedAt: nowMs(),
  };
  tenants.set(wanted, rec);
  persist();
  return { ok: true, tenant: normalizeRecord(rec), created: true };
}

/** 停用/启用（停用后该租户下所有访问一律拒；数据保留，可恢复）。 */
export function setTenantDisabled(id, disabled) {
  const k = String(id ?? '').trim();
  if (k === DEFAULT_TENANT && disabled) {
    return { ok: false, error: '默认组织不可停用（存量数据落点）' };
  }
  const rec = tenants.get(k);
  if (!rec) return { ok: false, error: '租户不存在' };
  rec.disabled = disabled === true;
  rec.updatedAt = nowMs();
  persist();
  return { ok: true, tenant: normalizeRecord(rec) };
}

/**
 * 归一化「这次请求属于哪个租户」。
 * **信任边界**：只接受来自 claims（票）或显式内部调用的值；
 * 缺省/非法/未登记 → 一律落到默认租户（**不是**"放行一切"）。
 * @param {unknown} value
 * @returns {string} 合法的租户 id
 */
export function normalizeTenantId(value) {
  const k = String(value ?? '').trim();
  if (!k) return DEFAULT_TENANT;
  if (!TENANT_ID_RE.test(k)) return DEFAULT_TENANT;
  return tenants.has(k) ? k : DEFAULT_TENANT;
}

/**
 * **核心判据**：两个租户 id 是否同一租户。
 * 这是全链路隔离的唯一比较入口 —— 任何地方想"跨租户放行"，都必须显式调用
 * `tenantBypass()` 并写清理由，不许自己写 `a === b`。
 */
export function sameTenant(a, b) {
  return normalizeTenantId(a) === normalizeTenantId(b);
}

/**
 * 平台级跨租户操作（只有 platform_admin 该走）。
 * 这里**不做身份判断**（那在 perms.js），只把"有意跨租户"这件事变成一个具名调用，
 * 好让 grep `tenantBypass` 就能列出所有跨租户点。
 */
export function tenantBypass(reason) {
  return { bypass: true, reason: String(reason ?? 'unspecified') };
}

/** 租户是否可用（存在且未停用）。停用的租户 = 全线拒绝，但数据保留。 */
export function isTenantUsable(id) {
  const rec = tenants.get(normalizeTenantId(id));
  return !!rec && rec.disabled !== true;
}
