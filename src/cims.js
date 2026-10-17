/**
 * CIMS 课表/时间表供给模块（2026-10-17）
 *
 * 背景（架构裁决）：ClassIsland 插件 v1.5.0.0 起零云端直连，唯一对外通道是连被控端
 * 插件宿主（ws://127.0.0.1:26789），由被控端 executeAction → PluginHost dispatch 分发。
 * 原先由插件私拉的四类 CIMS 通信（资源同步/命令轮询/状态上报/消息拉取）整体拆除后，
 * 「课表/时间表/科目从哪来」这个问题移到了云端——**本模块就是答案**：
 *   云端按设备拉 CIMS → 以指令 classisland_apply_profile 经被控端下发 → 插件写回宿主档案。
 *
 * 为什么拉取逻辑用 node:http 而不是 fetch：
 *   CIMS 客户端 API 受 TenantMiddleware 保护，必须带 `Host: <slug>.<BaseDomain>` 识别租户；
 *   且资源端点 302 到 /get?token=...，自动跟随重定向会丢自定义 Host（旧插件
 *   StelarithSyncService 实测过同样的坑）。Node 18+ 的全局 fetch（undici）会把 host
 *   当禁头直接剥掉 → 租户识别失败；node:http 才能逐跳带 Host 手动跟随。
 *
 * 协议（与旧插件逐字对齐，资源发现是清单驱动的）：
 *   ① GET {base}/api/v1/client/{uid}/manifest        → {"ClassPlanSource":{"Value":"…?name=x","Version":n}, …}
 *   ② GET {base}/api/v1/client/{Type}?name={name}     → 302 → /get?token=...
 *   ③ GET {base}/get?token=...                        → 资源原文（ClassIsland 档案信封 JSON）
 *
 * 配置（.env，代码默认保守——不自动做任何事，直到显式开启）：
 *   CIMS_ENABLED=false          总开关（设备上线自动拉取/周期拉取才生效；管理 API 随时可用）
 *   CIMS_BASE_URL=http://127.0.0.1:8096
 *   CIMS_BASE_DOMAIN=localhost  租户基域（与 CIMS .env 的 CIMS_BASE_DOMAIN 一致）
 *   CIMS_SLUG=demo-class        租户 slug
 *   CIMS_UID_MAP=               Stelarith 设备 uid → CIMS client_uid 映射（JSON 对象）
 *   CIMS_PULL_INTERVAL_SEC=300  周期拉取间隔（0 = 不周期拉）
 *   CIMS_AUTO_APPLY=true        拉到变化就下发（false = 只拉缓存，等管理端手动 apply）
 *   CIMS_TIMEOUT_MS=8000        单请求超时
 *
 * 设备↔客户端映射（重要）：CIMS 的客户端是 ClassIsland 经 gRPC 用自己的 ClientGuid
 * 注册的，与星集控被控端 uid（主机名）是**两套身份**。缺省按恒等映射（CIMS 侧就用
 * 主机名注册时直接可用）；两套身份不同名时（eval 环境即是如此），用 CIMS_UID_MAP
 * 显式桥接，如 {"N7-20091211":"lab-pc-001"}。恒等映射不到资源时 CIMS 会回落租户
 * 默认资源（可能是空占位，如「未绑定班级」）——那不是故障，是没绑班。
 */
import http from 'node:http';
import https from 'node:https';
import { pushEvent, listDevices, sendInstruction } from './registry.js';

/** 与插件 StelarithAgentLink.ActionApplyProfile 一致（改这里必须同步插件）。 */
const ACTION_APPLY_PROFILE = 'classisland_apply_profile';

/** 只取这三类——插件 classisland_apply_profile 的入参形状就这三个键。 */
const WANTED_TYPES = ['ClassPlan', 'TimeLayout', 'Subjects'];

const CFG = {
  enabled: (process.env.CIMS_ENABLED || 'false').trim() === 'true',
  baseUrl: (process.env.CIMS_BASE_URL || 'http://127.0.0.1:8096').replace(/\/+$/, ''),
  baseDomain: (process.env.CIMS_BASE_DOMAIN || 'localhost').trim(),
  slug: (process.env.CIMS_SLUG || 'demo-class').trim(),
  timeoutMs: Math.max(1000, Number(process.env.CIMS_TIMEOUT_MS || 8000)),
  pullIntervalSec: Math.max(0, Number(process.env.CIMS_PULL_INTERVAL_SEC || 300)),
  autoApply: (process.env.CIMS_AUTO_APPLY || 'true').trim() !== 'false',
  uidMap: parseUidMap(process.env.CIMS_UID_MAP || ''),
};

/** CIMS_UID_MAP：JSON 对象字符串；坏 JSON 不致命——告警后按恒等映射跑（绝不静默）。 */
function parseUidMap(raw) {
  const text = String(raw || '').trim();
  if (!text) return {};
  try {
    const obj = JSON.parse(text);
    if (!obj || typeof obj !== 'object' || Array.isArray(obj)) throw new Error('不是 JSON 对象');
    const out = {};
    for (const [k, v] of Object.entries(obj)) {
      if (typeof v === 'string' && v.trim()) out[k] = v.trim();
    }
    return out;
  } catch (e) {
    console.warn(`[cloud] CIMS_UID_MAP 解析失败（按恒等映射继续）：${e.message}`);
    return {};
  }
}

/** Stelarith 设备 uid → CIMS client_uid（无映射则恒等）。 */
export function cimsClientUid(deviceUid) {
  return CFG.uidMap[deviceUid] || deviceUid;
}

/** uid -> { at, ok, error, versions:{Type:n}, resources:{Type:text}, appliedAt } */
const cache = new Map();
/** 同 uid 并发拉取护栏（管理端连点两次 / 周期 tick 撞上线钩子时不重入）。 */
const inflight = new Map();
/** syncAll 全局护栏。 */
let allRunning = false;
let lastSyncAllAt = 0;
let lastSyncAllSummary = null;

// ── HTTP 原语（逐跳保 Host + 手动跟随 302）─────────────────────────────────

function requestText(urlStr, hostHeader, redirectsLeft = 5) {
  return new Promise((resolve, reject) => {
    let u;
    try { u = new URL(urlStr); } catch (e) { reject(e); return; }
    const mod = u.protocol === 'https:' ? https : http;
    const req = mod.request({
      protocol: u.protocol,
      hostname: u.hostname,
      port: u.port || (u.protocol === 'https:' ? 443 : 80),
      path: u.pathname + u.search,
      headers: { host: hostHeader, accept: 'application/json, text/plain, */*' },
    }, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location && redirectsLeft > 0) {
        res.resume();   // 丢完 body 再跟随，别把连接吊着
        const next = new URL(res.headers.location, u).toString();
        resolve(requestText(next, hostHeader, redirectsLeft - 1));
        return;
      }
      let body = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { body += c; });
      res.on('end', () => resolve({ status: res.statusCode || 0, body }));
    });
    req.setTimeout(CFG.timeoutMs, () => req.destroy(new Error(`CIMS 请求超时（${CFG.timeoutMs}ms）`)));
    req.on('error', reject);
    req.end();
  });
}

// ── 清单解析（端口自旧插件 StelarithSyncService.ParseManifestSources）────────

/** manifest JSON → Map<资源类型, 资源名>（"*Source" 且 .Value 是可解析 URL）。 */
export function parseManifestSources(manifestJson) {
  const map = new Map();
  if (!manifestJson) return map;
  let doc;
  try { doc = JSON.parse(manifestJson); } catch { return map; }
  if (!doc || typeof doc !== 'object' || Array.isArray(doc)) return map;
  for (const [key, val] of Object.entries(doc)) {
    if (!key.endsWith('Source')) continue;
    const value = val && typeof val === 'object' ? val.Value : undefined;
    if (typeof value !== 'string' || !value) continue;
    let u;
    try { u = new URL(value); } catch { continue; }
    const segs = u.pathname.split('/').filter(Boolean);
    const type = segs.length ? segs[segs.length - 1] : '';
    if (!type) continue;
    map.set(type, u.searchParams.get('name') || 'default');
  }
  return map;
}

/** manifest JSON → { Type: Version }（仅观测用：变化判断以资源正文为准，见 syncDevice）。 */
function manifestVersions(manifestJson) {
  const out = {};
  if (!manifestJson) return out;
  let doc;
  try { doc = JSON.parse(manifestJson); } catch { return out; }
  if (!doc || typeof doc !== 'object') return out;
  for (const [key, val] of Object.entries(doc)) {
    if (!key.endsWith('Source') || !val || typeof val !== 'object') continue;
    let type = '';
    try {
      const u = new URL(String(val.Value || ''));
      const segs = u.pathname.split('/').filter(Boolean);   // pathname 不含查询串
      type = segs.length ? segs[segs.length - 1] : '';
    } catch { /* 坏 URL 跳过 */ }
    if (type && val.Version !== undefined) out[type] = String(val.Version);
  }
  return out;
}

// ── 拉取 ──────────────────────────────────────────────────────────────────

/**
 * 给一个被控端 uid 拉 CIMS 课表三件套（uid 经 CIMS_UID_MAP 桥接到 CIMS client_uid）。
 * @returns {Promise<{ok:boolean, error?:string, at:string, versions:object, resources:object}>}
 */
export async function pullForDevice(uid) {
  const clientUid = cimsClientUid(uid);
  const host = `${CFG.slug}.${CFG.baseDomain}`;
  const at = new Date().toISOString();
  try {
    const mres = await requestText(
      `${CFG.baseUrl}/api/v1/client/${encodeURIComponent(clientUid)}/manifest`, host);
    if (mres.status !== 200) {
      return { ok: false, error: `manifest HTTP ${mres.status}`, at, versions: {}, resources: {} };
    }
    const sources = parseManifestSources(mres.body);
    if (sources.size === 0) {
      return { ok: false, error: 'manifest 无可用资源源', at, versions: {}, resources: {} };
    }
    const resources = {};
    for (const [type, name] of sources) {
      if (!WANTED_TYPES.includes(type)) continue;
      const rres = await requestText(
        `${CFG.baseUrl}/api/v1/client/${encodeURIComponent(type)}?name=${encodeURIComponent(name)}`, host);
      // 资源 302→/get 由 requestText 内部跟随；非 200 或空正文都算没拿到
      if (rres.status === 200 && rres.body) resources[type] = rres.body;
    }
    if (Object.keys(resources).length === 0) {
      return { ok: false, error: '三类资源全部未拿到', at, versions: manifestVersions(mres.body), resources: {} };
    }
    return { ok: true, at, versions: manifestVersions(mres.body), resources };
  } catch (e) {
    return { ok: false, error: e.message, at, versions: {}, resources: {} };
  }
}

// ── 下发 ──────────────────────────────────────────────────────────────────

/** 把拉到的资源打包成插件认的 params（JSON 字符串，逐键可选）。 */
function buildApplyParams(resources) {
  const params = {};
  if (resources.ClassPlan) params.class_plan = resources.ClassPlan;
  if (resources.TimeLayout) params.time_layout = resources.TimeLayout;
  if (resources.Subjects) params.subjects = resources.Subjects;
  return params;
}

/** 经被控端下发 apply_profile 指令（云端只负责"发出去"，真执行看回执）。 */
function applyToDevice(uid, pulled) {
  const params = buildApplyParams(pulled.resources);
  if (Object.keys(params).length === 0) {
    return { ok: false, uid, error: '无可用资源可下发' };
  }
  const r = sendInstruction(uid, ACTION_APPLY_PROFILE, params);
  if (r.state !== 'sent') {
    pushEvent('warn', 'CIMS 课表下发失败', { uid, action: ACTION_APPLY_PROFILE, state: r.state, detail: r.detail || '' });
    return { ok: false, uid, error: `下发状态 ${r.state}`, detail: r.detail || '' };
  }
  pushEvent('info', 'CIMS 课表已下发设备', {
    uid, action: ACTION_APPLY_PROFILE, instructionId: r.id,
    keys: Object.keys(params).join(','),
  });
  return { ok: true, uid, instructionId: r.id, applied: true };
}

// ── 同步（拉取 + 变化才下发）───────────────────────────────────────────────

function bodiesDiffer(a, b) {
  const ka = Object.keys(a || {}), kb = Object.keys(b || {});
  if (ka.length !== kb.length) return true;
  for (const k of ka) if (a[k] !== b[k]) return true;
  return false;
}

/**
 * 单设备同步：拉取 → 与缓存比对 → 有变化（或 force）才下发。
 * @param {string} uid 被控端 uid（同时用作 CIMS client_uid）
 * @param {{force?:boolean, apply?:boolean}} opts force=无视缓存也下发；apply=false 只拉缓存
 */
export async function syncDevice(uid, opts = {}) {
  const { force = false, apply = true } = opts;
  if (inflight.has(uid)) return inflight.get(uid);
  const p = (async () => {
    const prev = cache.get(uid);
    const pulled = await pullForDevice(uid);
    if (!pulled.ok) {
      cache.set(uid, { at: pulled.at, ok: false, error: pulled.error, versions: {}, resources: {}, appliedAt: prev?.appliedAt ?? null });
      pushEvent('warn', 'CIMS 课表拉取失败', { uid, error: pulled.error });
      return { ok: false, uid, error: pulled.error, applied: false };
    }
    const changed = force || !prev || !prev.ok || bodiesDiffer(prev.resources, pulled.resources);
    const entry = { at: pulled.at, ok: true, error: null, versions: pulled.versions, resources: pulled.resources, appliedAt: prev?.appliedAt ?? null };
    cache.set(uid, entry);
    if (!changed) return { ok: true, uid, changed: false, applied: false };
    if (!apply) return { ok: true, uid, changed: true, applied: false };
    const r = applyToDevice(uid, pulled);
    if (r.ok) entry.appliedAt = new Date().toISOString();
    return { ok: r.ok, uid, changed: true, applied: r.ok, error: r.error };
  })().finally(() => inflight.delete(uid));
  inflight.set(uid, p);
  return p;
}

/** 全部在线设备同步（周期 tick / 管理端 "apply 全部" 用）。 */
export async function syncAll(opts = {}) {
  if (allRunning) return { ok: false, error: '上一轮全量同步仍在跑，跳过本轮' };
  allRunning = true;
  try {
    const uids = listDevices().filter((d) => d.online).map((d) => d.uid);
    const results = [];
    for (const uid of uids) {
      try { results.push(await syncDevice(uid, opts)); }
      catch (e) { results.push({ ok: false, uid, error: e.message, applied: false }); }
    }
    lastSyncAllAt = Date.now();
    lastSyncAllSummary = {
      at: new Date(lastSyncAllAt).toISOString(),
      total: uids.length,
      applied: results.filter((r) => r.applied).length,
      failed: results.filter((r) => !r.ok).length,
    };
    if (lastSyncAllSummary.applied > 0 || lastSyncAllSummary.failed > 0) {
      pushEvent('info', 'CIMS 课表全量同步完成', lastSyncAllSummary);
    }
    return { ok: true, ...lastSyncAllSummary, results };
  } finally {
    allRunning = false;
  }
}

// ── 设备上线钩子（register 成功后触发，绝不阻塞注册路径）────────────────────

/**
 * 设备刚连上云端：后台拉它的 CIMS 课表，按 CIMS_AUTO_APPLY 决定是否立即下发。
 * 调用方**不要 await**——注册路径上多做一次 CIMS 往返会把握手拖慢。
 */
export function onDeviceOnline(uid) {
  if (!CFG.enabled) return;
  syncDevice(uid, { apply: CFG.autoApply }).catch((e) => {
    pushEvent('error', 'CIMS 上线同步异常', { uid, error: e.message });
  });
}

// ── 状态 ──────────────────────────────────────────────────────────────────

export function cimsStatus() {
  return {
    enabled: CFG.enabled,
    autoApply: CFG.autoApply,
    baseUrl: CFG.baseUrl,
    slug: CFG.slug,
    baseDomain: CFG.baseDomain,
    uidMap: CFG.uidMap,
    pullIntervalSec: CFG.pullIntervalSec,
    timeoutMs: CFG.timeoutMs,
    lastSyncAllAt: lastSyncAllAt ? new Date(lastSyncAllAt).toISOString() : null,
    lastSyncAllSummary,
    devices: [...cache.entries()].map(([uid, v]) => ({
      uid,
      at: v.at,
      ok: v.ok,
      error: v.error,
      versions: v.versions,
      sizes: Object.fromEntries(Object.entries(v.resources).map(([k, s]) => [k, s.length])),
      appliedAt: v.appliedAt,
    })),
  };
}

/** 周期拉取定时器的启动入口（index.js 收尾调用；间隔 0 = 不启）。 */
export function startCimsScheduler() {
  if (!CFG.enabled || CFG.pullIntervalSec <= 0) return;
  setInterval(() => {
    syncAll().catch((e) => pushEvent('error', 'CIMS 周期同步异常', { error: e.message }));
  }, CFG.pullIntervalSec * 1000);
  console.log(`[cloud] CIMS 课表供给已启用：${CFG.slug}.${CFG.baseDomain} → ${CFG.baseUrl}，每 ${CFG.pullIntervalSec}s 同步，autoApply=${CFG.autoApply}` +
    (Object.keys(CFG.uidMap).length ? `，uid 映射 ${JSON.stringify(CFG.uidMap)}` : ''));
}
