// 设备身份（名字）去重 + 改名（2026-11-05 · 用户点单「设备命名冲突去重」）
//
// ── 线上实证的问题 ────────────────────────────────────────────────────
// GET /api/devices 里同时存在
//     uid=DESKTOP-I15OM55        name=DESKTOP-I15OM55
//     uid=class_xlzx_2028_080    name=DESKTOP-I15OM55
// 两条同名。根因有两层，缺一不可：
//   ① 被控端 main.cpp:656 / :682-683 —— g_uid 与 name 缺省**都**取 QHostInfo::localHostName()
//      ⇒ 同一台母版机装出来的教室机天然同名（这一层只能在被控端改，本轮不碰被控端）；
//   ② 云端 registry.knownDevices 写入时**没有任何唯一性约束**，而且**全仓没有改名 API**
//      ⇒ 就算老师想改也没入口，重名只会越积越多（registry.setDeviceMeta 早就存在，
//        但没有任何路由调过它 —— 死代码）。
//
// 本文件补第 ② 层：受权的改名入口 + 一份"哪里重名"的判定 + **改名钉住**机制。
//
// ── 为什么还要"钉住" ──────────────────────────────────────────────────
// registry.markConnected 是 `name: meta?.name ?? known.name` —— **设备自报名优先**。
// 被控端每次重连都上报 QHostInfo::localHostName()，于是"管理端改的名字"下一次重连
// 就被旧主机名顶回去，功能看起来像没做。所以改名时同时登记一条 namePin，
// 并在 register 落 meta 前用 applyNamePin(uid, regMeta) 覆盖自报名。
// 钉住的表 2026-11-18 起**持久化**进设备账本（device-ledger.js 的 name_pin 列）：
// 改名是管理端的人工决定，云端重启（发版/watchdog 拉起）后不该失效。内存 Map 降级为
// 热缓存，读不到时回账本取 —— 账本不可用时退回纯内存（与改造前行为一致，不更坏）。
//
// ── 纪律 ──────────────────────────────────────────────────────────────
//   · 重名判定只在**同一个租户内**做：两所学校各有一台「1班讲台」是合理的；
//   · 改名是**写操作**：必须过按设备权限（canAccess）+ 重认证门（index.js 侧）；
//   · 不做"静默改名"：撞名时要么明确拒绝并给出建议名，要么显式 dedupe:true 自动加后缀。

import { canAccess } from './perms.js';
import {
  getDeviceClassCode, getDeviceTenant, listDevices, pushEvent, setDeviceMeta, broadcastDevices,
} from './registry.js';
import { normalizeTenantId } from './tenant.js';
import { getNamePin, setNamePin, ledgerStats } from './device-ledger.js';

/** 设备名上限（与 register 侧 slice(0,64) 同口径，避免"改了名但注册又被截断"的错位）。 */
export const DEVICE_NAME_MAX = 64;

/** uid -> 人工钉住的名字。**热缓存**：真源在 device-ledger 的 name_pin 列（跨重启）。 */
const namePins = new Map();

function normName(v) {
  return String(v ?? '').trim().toLowerCase().replace(/\s+/g, ' ');
}

/** 校验设备名：非空、≤64、不含控制字符（名字要进 QML 文本与日志，换行会被截断显示）。 */
function validateName(raw) {
  const name = String(raw ?? '').trim();
  if (!name) return { ok: false, code: 'EMPTY_NAME', error: '设备名不能为空' };
  if (name.length > DEVICE_NAME_MAX) {
    return { ok: false, code: 'NAME_TOO_LONG', error: `设备名最长 ${DEVICE_NAME_MAX} 个字符` };
  }
  if (/[\u0000-\u001f\u007f]/.test(name)) {
    return { ok: false, code: 'NAME_INVALID', error: '设备名不能包含换行或控制字符' };
  }
  return { ok: true, name };
}

/**
 * register 落 meta 前的钩子：该 uid 若有钉住的名字，**覆盖设备自报名**。
 * 不改 registry（零余量文件）：markConnected 里 `meta?.name ?? known.name` 本身
 * 就给了调用方"先改 meta 再落表"的位置，本函数就在那个位置上生效。
 * @returns {object} 同一个 regMeta 对象（就地改写 name，返回它便于链式调用）
 */
export function applyNamePin(uid, regMeta) {
  const u = String(uid ?? '').trim();
  // 先查热缓存，未命中再回账本（云端刚重启时缓存是空的，这里是唯一恢复点）。
  let pin = namePins.get(u);
  if (!pin) {
    pin = getNamePin(u);
    if (pin) namePins.set(u, pin);
  }
  if (!pin) return regMeta;
  if (regMeta && typeof regMeta === 'object') regMeta.name = pin;
  return regMeta;
}

/** 诊断：当前钉住多少台（运维/测试用）。以账本为准，内存缓存只作补充。 */
export function pinnedNameCount() {
  const inLedger = Number(ledgerStats().pinned) || 0;
  return Math.max(inLedger, namePins.size);
}

/**
 * 给设备清单标 `nameConflict`（同名多条时为 true）。
 *
 * 为什么在**输出时**算而不是写入时拦：重名是既有数据的现状（线上已经重了），
 * 写入时拦只能防新增，界面上那两条旧重名永远显示成"两台不同的机器"，老师分不清哪台是哪台。
 * 输出时标记 = 存量与新增一起可见，且不改任何历史数据。
 *
 * 分租户计：跨校同名不算冲突（两所学校各自的"1班讲台"互不相干）。
 * @param {Array} devices listDevices() 的结果
 * @returns {Array} 新数组（不改入参；每项多一个 nameConflict 布尔）
 */
export function markNameConflicts(devices) {
  if (!Array.isArray(devices)) return devices;
  const seen = new Map();   // "tn\u0000name" -> count
  const key = (d) => {
    const n = normName(d && d.name);
    if (!n) return null;    // 没名字不算冲突（管端本来就回退显示 uid）
    return `${normalizeTenantId(d && d.tenantId)}\u0000${n}`;
  };
  for (const d of devices) {
    const k = key(d);
    if (k) seen.set(k, (seen.get(k) || 0) + 1);
  }
  return devices.map((d) => {
    const k = key(d);
    return { ...d, nameConflict: !!(k && seen.get(k) > 1) };
  });
}

/** 同一租户内，某名字已被哪些设备占用（排除自己）。 */
export function nameHolders(name, tenantId, excludeUid = '') {
  const tn = normalizeTenantId(tenantId);
  const n = normName(name);
  if (!n) return [];
  return listDevices().filter((d) => normalizeTenantId(d.tenantId) === tn
    && normName(d.name) === n
    && String(d.uid) !== String(excludeUid || ''));
}

/** 生成一个在本租户内不重复的名字：`讲台` → `讲台-2` → `讲台-3` …（最多试 99 次）。 */
export function suggestUniqueName(base, tenantId, excludeUid = '') {
  const root = String(base ?? '').trim().slice(0, DEVICE_NAME_MAX - 4) || '设备';
  if (nameHolders(root, tenantId, excludeUid).length === 0) return root;
  for (let i = 2; i <= 99; i++) {
    const cand = `${root}-${i}`;
    if (nameHolders(cand, tenantId, excludeUid).length === 0) return cand;
  }
  // 极端情况：99 个同根名都占了 → 用 uid 尾段做后缀（确定、必然唯一）
  const tail = String(excludeUid || '').replace(/[^A-Za-z0-9]/g, '').slice(-6) || String(Date.now()).slice(-6);
  return `${root}-${tail}`.slice(0, DEVICE_NAME_MAX);
}

/**
 * 改设备名（唯一入口）。
 *
 * @param {object} o
 * @param {string} o.uid 设备 uid
 * @param {string} o.name 新名字
 * @param {object|null} o.ident identityFromAuth() 结果（未鉴权 = 拒）
 * @param {string} [o.tenantId] 服务端已知租户（缺省取设备归属）
 * @param {boolean} [o.dedupe] true = 撞名时自动加后缀（-2/-3…）；false（缺省）= 拒绝并给建议名
 * @param {boolean} [o.pin] true（缺省）= 钉住，设备重连不会用自报主机名覆盖
 * @returns {{ok:boolean, code?:string, error?:string, uid?:string, name?:string,
 *            previous?:string|null, suggested?:string, tenantId?:string, renamed?:boolean}}
 */
export function renameDevice({ uid = '', name = '', ident = null, tenantId = null,
                               dedupe = false, pin = true } = {}) {
  const u = String(uid ?? '').trim();
  if (!u) return { ok: false, code: 'NO_UID', error: '缺 uid' };
  const v = validateName(name);
  if (!v.ok) return v;
  if (!ident) return { ok: false, code: 'FORBIDDEN', error: '未授权：改名需要管理端身份' };
  const tn = normalizeTenantId(tenantId || getDeviceTenant(u));
  // 判权口径与远控/访问密码一致：以**设备当前班级**为对象做 canAccess（含租户闸）。
  // 设备还没绑班（classCode=null）时只有全权身份（静态令牌/平台超管）能改名 ——
  // 否则"没班的设备"就成了谁也管不了的野设备。
  if (!canAccess(ident, getDeviceClassCode(u), { tenantId: tn })) {
    return { ok: false, code: 'FORBIDDEN', error: '无权限修改该设备的名称' };
  }
  const holders = nameHolders(v.name, tn, u);
  let finalName = v.name;
  if (holders.length > 0) {
    const suggested = suggestUniqueName(v.name, tn, u);
    if (!dedupe) {
      return {
        ok: false, code: 'NAME_TAKEN', suggested,
        error: `名称「${v.name}」已被 ${holders.length} 台设备占用（${holders.slice(0, 3).map((d) => d.uid).join('、')}${holders.length > 3 ? '…' : ''}）`,
      };
    }
    finalName = suggested;   // 显式要求去重 → 自动加后缀（不静默：回执里带上 suggested）
  }
  const before = listDevices().find((d) => String(d.uid) === u);
  setDeviceMeta(u, { name: finalName });
  if (pin) {
    namePins.set(u, finalName);
    setNamePin(u, finalName);   // 落账本：云端重启后 applyNamePin 仍能取回
  } else {
    namePins.delete(u);
    setNamePin(u, '');          // 显式取消钉住
  }
  broadcastDevices();   // 改名后立刻推设备表，管端不用等下一轮轮询
  pushEvent('info', '管理端修改设备名称', {
    uid: u, name: finalName, previous: (before && before.name) || null,
    deduped: finalName !== v.name, pinned: !!pin,
    by: ident.uid || (ident.mode === 'token' ? 'system' : 'unknown'),
  });
  return {
    ok: true, uid: u, name: finalName, previous: (before && before.name) || null,
    tenantId: tn, renamed: finalName !== ((before && before.name) || null), pinned: !!pin,
    ...(finalName !== v.name ? { suggested: finalName, deduped: true } : {}),
  };
}

/**
 * HTTP 薄壳：POST /api/device/rename  { uid, name, dedupe?, pin? }
 * 重认证门在 index.js 侧（与 /api/class/* 同一口径）—— 本函数只管业务与回执。
 */
export function handleDeviceRename(req, res, u, send, ident) {
  let raw = '';
  req.on('data', (c) => { raw += c; if (raw.length > 1e4) req.destroy(); });
  req.on('end', () => {
    let body;
    try { body = JSON.parse(raw || '{}'); } catch { return send(400, { ok: false, error: '请求体不是合法 JSON' }); }
    const pick = (k) => String(body[k] ?? u.searchParams.get(k) ?? '').trim();
    const uid = pick('uid');
    const r = renameDevice({
      uid, name: pick('name'), ident,
      dedupe: body.dedupe === true || u.searchParams.get('dedupe') === '1',
      pin: body.pin !== false,
      tenantId: uid ? getDeviceTenant(uid) : null,
    });
    if (r.ok) return send(200, { ok: true, ...r });
    const status = r.code === 'FORBIDDEN' ? 403 : (r.code === 'NAME_TAKEN' ? 409 : 400);
    return send(status, { ok: false, ...r });
  });
  return undefined;
}
