// 云端权限消费层（2026-10-15 · 契约《星集控-多校多身份-现状勘误与scope契约草案》§4.3）
//
// 位置：云端是**唯一权限入口**（指令下发 / 设备列表 / 订阅），但**不是权限真源**。
// 真源在站点（permissions.ts + 用户档案），由站点签进 viewer 票的 claims；
// 云端只做一件事：**从票里读 scopes，按 scope 过滤一切「返回设备 / 下发指令」路径**。
//
// 硬规矩（契约 §4.5，必须写进代码注释）：
//   ① 权限真源只有站点；云端只消费、不创造。
//   ② schoolId 只能从票来，绝不接受客户端参数指定（这里根本不读参数）。
//   ③ 云端任何「返回设备 / 下发指令」的路径必须过 can()；WS 广播必须过 scope 过滤。
//   ④ 前端隐藏 ≠ 权限；后端校验是唯一安全边界。
//
// 向后兼容（契约 §4.4，取推荐项 A）：
//   · 旧票（第 3 段是纯 uid、无 claims）⇒ scopes=null ⇒ **全权**（生产现状就是全权，
//     收紧会立刻断现有老师机；等站点全面签发 claims 票后再收）。
//   · 长期静态令牌 CLOUD_VIEWER_TOKEN ⇒ **全权**（老机器/离线机房不能断），但会记审计。
//   · claims 票显式 scopes=[] ⇒ 无任何数据权限（与「无 claims」严格区分）。
//
// 租户（2026-11-02 · class_id 全链路多租户）：
//   · **云端仍然不是租户真源**。租户来自票里的 claims.tenantId（或由 schoolId 解析），
//     看不到就落默认组织（tenant.js 的 DEFAULT_TENANT）。
//   · 判权顺序：**先判租户，再判 scope**。租户不同直接拒 —— 只比 classCode 在多校时代
//     会把「隔壁学校的同码班」一起放行（两校都在用站点 class_0102 这类外部码）。
//   · 豁免只有三种：全权身份（scopes=null）/ 平台超管 / 调用方没给目标租户（无从判，向后兼容）。

import { DEFAULT_TENANT, deriveTenantId, normalizeTenantId } from './tenant.js';

/** 租户 id 格式（与 tenant.js 同一口径；这里是本地副本，避免为一条正则去改模块导出面）。 */
const TENANT_ID_RE = /^tn_[a-z0-9_-]{2,32}$/;
//
// scope 语义（契约 §4.2）：type ∈ {class, grade, school}，id 是班级码形态
// （class_xlzx_2028_08）；授权父节点覆盖子节点（school 覆盖所有 class_<校>_*，
// grade 覆盖 class_<校>_<届>_*）。目标设备先归约成 classCode 再比。

/** 从鉴权结果提取「权限身份」。auth 来自 authorizeViewer / verifyViewerTicket 的返回。 */
export function identityFromAuth(auth) {
  if (!auth || !auth.ok) return null;
  if (auth.mode === 'token') {
    // 长期静态令牌：全权（契约 §4.4）。uid=null（匿名），审计只靠连接/时间。
    // 租户维度也全权：运维视角要能跨校排障，但每次都会走 tenantBypass 留痕。
    return { uid: null, roles: [], scopes: null, tenantId: DEFAULT_TENANT, tenantBypass: true };
  }
  const claims = auth.claims ?? null;
  return {
    uid: auth.uid ?? claims?.uid ?? null,
    schoolId: claims?.schoolId ?? null,
    roles: Array.isArray(claims?.roles) ? claims.roles : [],
    scopes: Array.isArray(claims?.scopes) ? claims.scopes : null,
    // 2026-11-02 多租户：票里显式带 tenantId 就用它；只有 schoolId 就解析一次；
    // 都没有 ⇒ 默认组织。**绝不接受客户端参数指定租户**（本函数只读票）。
    tenantId: claimTenant(claims),
    tenantBypass: claims?.tenantBypass === true,
  };
}

/**
 * 票里能推出哪个租户。
 *
 * 顺序（从强到弱）：
 *   ① `claims.tenantId` —— 站点已经完成租户解析时给的，最权威
 *   ② `claims.schoolId` —— 老票只有学校 id；先看它本身是不是合法租户 id
 *      （`tn_xxx`），是就直接用；否则当作**学校名/旧 id**，去租户表里找同 id 的记录
 *   ③ 都没有 ⇒ 默认组织（存量数据落点）
 *
 * ⚠️ 这里**不创造租户**：查不到就是默认租户，绝不因为来了个陌生 schoolId 就自动建租户
 *    （那等于谁都能自封一所学校）。
 */
function claimTenant(claims) {
  if (!claims) return DEFAULT_TENANT;
  const explicit = String(claims.tenantId ?? '').trim();
  if (explicit) return normalizeTenantId(explicit);
  const sid = String(claims.schoolId ?? '').trim();
  if (sid) {
    if (TENANT_ID_RE.test(sid)) return normalizeTenantId(sid);
    return normalizeTenantId(deriveTenantId(sid));
  }
  return DEFAULT_TENANT;
}

/** 平台级超管（schoolId=null 且 roles 含 platform_admin）：跳过 scope 过滤。 */
function isPlatform(ident) {
  return ident.schoolId == null && Array.isArray(ident.roles) && ident.roles.includes('platform_admin');
}

/**
 * canAccess：这个连接能不能碰「classCode 归属的设备」。
 *
 * ⚠️ 2026-11-02 多租户：判 classCode 之前**先判租户**。只比 classCode 在单校时代够用，
 *    多校之后学校的「1班」与另一所学校的「1班」可能有同一个外部码（站点 class_0102 两校都在用），
 *    只比码就等于把隔壁学校的班也放行了。租户不同 → 直接拒，不再看 scope。
 *
 * @param {object|null} ident identityFromAuth 的结果（null = 未鉴权，一律拒）
 * @param {string|null} classCode 目标设备的班级码（registry 里没有 = null）
 * @param {object} [opts]
 * @param {string|null} [opts.tenantId] 目标设备/班级的租户（能拿到就一定要传）
 * @returns {boolean}
 */
export function canAccess(ident, classCode, opts = {}) {
  if (!ident) return false;

  // ── 租户闸（先于一切 scope 判定）──────────────────────────────────
  // 三条豁免，各自有明确理由：
  //   ① ident.scopes === null  → 静态令牌/旧票，契约 §4.4 规定全权（含跨校）
  //   ② isPlatform(ident)      → 平台级超管，本来就是跨校角色
  //   ③ 目标租户未知（没传）    → 无从判起，交回原有 scope 逻辑（向后兼容，
  //                              调用方逐步补上 tenantId 就能收紧）
  const tgtTenant = opts.tenantId == null ? null : normalizeTenantId(opts.tenantId);
  if (tgtTenant !== null && ident.scopes !== null && !isPlatform(ident)) {
    const mine = normalizeTenantId(ident.tenantId);
    if (mine !== tgtTenant) return false;   // 跨租户：一律拒，不看 scope
  }

  if (ident.scopes === null) return true;              // 全权：静态令牌 / 旧票（契约 §4.4-A）
  if (isPlatform(ident)) return true;                  // 平台级超管
  if (!classCode) return false;                        // 目标设备没有班级归属 → 谁都不该碰
  for (const s of ident.scopes) {
    if (!s || typeof s.type !== 'string' || typeof s.id !== 'string') continue;
    const sid = String(s.id);
    if (s.type === 'class' && sid === classCode) return true;
    // grade / school：前缀命中（class_xlzx_2028_08 前缀 class_xlzx_2028 = 届，class_xlzx = 校）
    if ((s.type === 'grade' || s.type === 'school') && classCode.startsWith(sid + '_')) return true;
  }
  return false;
}

/**
 * 列表过滤：全权身份原样返回；受限身份只留 scopes 内的设备。
 * @param {object|null} ident 权限身份
 * @param {Array} devices 设备数组
 * @param {(d:any)=>string|null} [getClass] 取班级码的回调（缺省读 d.classCode）
 * @param {(d:any)=>string|null} [getTenant] 取租户的回调（缺省读 d.tenantId；拿不到就不判租户）
 */
export function filterDevices(ident, devices, getClass, getTenant) {
  if (!ident || ident.scopes === null || isPlatform(ident)) return devices;
  const gc = getClass || ((d) => (d && typeof d.classCode === 'string' ? d.classCode : null));
  const gt = getTenant || ((d) => (d && typeof d.tenantId === 'string' ? d.tenantId : null));
  return devices.filter((d) => canAccess(ident, gc(d), { tenantId: gt(d) }));
}

/**
 * 字段脱敏（2026-10-20 · RBAC 第二批）：受限身份（老师/助教）剥离 ip/mac 系敏感字段。
 * 平台超管 / 全权（scopes===null）不脱敏。
 *
 * ⚠️ 现状勘误：listDevices() 当前根本不含这些字段（被控端注册不上报 ip/mac），
 * 所以这里的脱敏是**防御性守卫**——字段一旦将来进入设备模型，低权限连接自动剥掉，
 * 不会出现"某天注册带上 ip/mac 忘了加守卫"直接漏给老师的回归。
 * 与 viewer 端 PermissionManager::sensitiveDeviceFields() 字段表逐字一致。
 */
const SENSITIVE_DEVICE_FIELDS = ['ip', 'mac', 'lanIp', 'wanIp', 'localIp', 'macAddr', 'ipAddr', 'ipv4', 'hardwareAddr'];

/** 单台脱敏（返回新对象，不改原设备）。 */
export function maskDevice(ident, d) {
  if (!d || typeof d !== 'object' || Array.isArray(d)) return d;
  if (!ident || ident.scopes === null || isPlatform(ident)) return d;
  const out = { ...d };
  for (const k of SENSITIVE_DEVICE_FIELDS) if (k in out) delete out[k];
  return out;
}

/** 列表脱敏。 */
export function maskDevices(ident, devices) {
  if (!Array.isArray(devices)) return devices;
  return devices.map((d) => maskDevice(ident, d));
}
