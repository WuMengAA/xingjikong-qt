// class-admin 共享 helper（2026-11-02 · 屎山整理 #4，从 class-admin.js 逐字搬出）
//
// 为什么单独成文件：这些是**权限与租户判定**，被班级域、学生域、考勤域同时使用。
// 抄到各域里就等于把「谁能看这个班」这条规则写在三个地方 —— 改一处漏两处，
// 而漏掉的那处正是越权入口，且 review 时完全看不出来（三份长得一模一样）。
//
// 跨校纪律（多租户，2026-11-02）：租户只从 ident 来，绝不接受请求体指定。
// findClass 在"别校有这个码、本校没有"时一律表现成"班级不存在"——
// 否则等于告诉调用方隔壁学校开了哪些班。


import { getClass, tenantOfClass } from './class-dir.js';
import { roleOf } from './class-room.js';
import { canAccess } from './perms.js';
import { normalizeTenantId } from './tenant.js';

/**
 * 从鉴权身份推出「这次操作属于哪个租户」。
 *
 * 纪律（2026-11-02 多租户）：
 *   · **租户只从 ident 来，绝不接受请求体参数指定** —— 客户端能指定租户，
 *     就等于客户端能跨校，那这一整套隔离等于没做（与 perms.js §4.5 ② 同源）。
 *   · ident 为空（未鉴权）时回默认组织；此时后面的 canRead/canManage 会先拒掉，
 *     不会真的用这个默认值写数据。
 *   · 显式 `opts.tenantId` 只给**服务端自己已知**的场景用（如从票里的老班级码反查），
 *     不是给 HTTP 层传的。
 */
function tenantOf(ident) {
  return normalizeTenantId(ident && ident.tenantId);
}

/**
 * 班级 + 租户的联合查询：**多租户下 getClass 必须带租户**。
 *
 * 为什么包一层而不在 22 处各写一遍：漏一处就是一处跨校泄漏，而"漏不漏"在
 * review 时看不出来。统一走这里，调 `getClass(code, tenantOf(ident))` 就够。
 *
 * 返回 `{hit, tn}`：hit 为 null 表示本租户内没有这个班（**包括"别校有、本校没有"**，
 * 后者对外必须表现成"班级不存在"，否则等于告诉攻击者别校有哪些班）。
 */
function findClass(code, ident) {
  const tn = tenantOf(ident);
  return { hit: getClass(String(code ?? '').trim(), tn), tn };
}

/**
 * 一个班级码在本租户里**不存在**时，别校是否占着同一个码。
 *
 * 这个函数只用于**日志与排障**，绝不回给客户端 —— 「别校有这个码」本身就是
 * 一条跨校信息（能推出隔壁学校开了哪些班）。
 */
function crossTenantHint(code) {
  const c = String(code ?? '').trim();
  if (!c) return null;
  const owner = tenantOfClass(c);   // 不带租户提示：唯一确定的租户才回，跨校重码回 null
  return owner;
}

/**
 * 能否管理这个班的成员。
 *
 * 2026-11-02 多租户：`canAccess` 与 `roleOf` **都必须带目标租户**。
 *   · canAccess 不带 tenantId ⇒ 租户闸不生效，只剩 scope 比对 ⇒ 别校同码班放行；
 *   · roleOf 不带 tenantId ⇒ 查的是所有学校的成员表里这个班码，A 校 teacher
 *     在 B 校同码班自动成立。两条都是权限发放，不是显示问题。
 */
function canManageMembers(ident, code) {
  if (!ident) return false;
  const tn = tenantOf(ident);
  if (canAccess(ident, code, { tenantId: tn })) return true;
  const role = ident.uid ? roleOf(code, ident.uid, tn) : null;
  return role === 'owner' || role === 'teacher';
}

/** 能否读这个班的班级数据（读权限比写宽一档：viewer 也该看得见名单）。 */
function canReadClass(ident, code) {
  if (!ident) return false;
  const tn = tenantOf(ident);
  if (canAccess(ident, code, { tenantId: tn })) return true;
  const role = ident.uid ? roleOf(code, ident.uid, tn) : null;
  return !!role;
}

/** 谁能读这个班的考勤（读权限比写宽一档，与 canReadClass 同语义）。 */
function canReadAttendance(ident, code) {
  if (!ident) return false;
  const tn = tenantOf(ident);
  if (canAccess(ident, code, { tenantId: tn })) return true;
  const role = ident.uid ? roleOf(code, ident.uid, tn) : null;
  return !!role;
}

/** 读 JSON body（≤16KB，与 bind-class 同款上限）。坏 JSON → cb(null) 由调用方定夺。 */
function readJsonBody(req, cb) {
  let raw = '';
  // 2026-10-25：上限 10KB → 1MB。原值是按"成员/发布"这类小载荷定的，但同一条通道
  // 现在还要收**班级课表**（整份 profile：科目 × 时段 × 多周 = 几十 KB 很常见），
  // 10KB 会把正常保存直接 ECONNRESET 掐断。1MB 只对已鉴权的 /api/class/* 开放，
  // 真正的业务上限在 saveTimetable 里（512KB，超了回可读的 400 而不是断连接）。
  req.on('data', (c) => { raw += c; if (raw.length > 1e6) req.destroy(); });
  req.on('end', () => {
    let body;
    try { body = JSON.parse(raw || '{}'); } catch { return cb(null); }
    // 注意：只在「解析失败」时回 cb(null)；cb 自身执行抛错不应被这里吞掉并伪装成
    // 「请求体不是合法 JSON」——那会掩盖真实的服务端异常（如缺 import 的 ReferenceError）。
    // 这里直接 cb(body)，执行异常交给上层/全局兜底，保证错误真实可见。
    cb(body);
  });
}

// 汇聚导出：调用方只 `import { findClass } from './class-admin-shared.js'`，
// 不必关心它是本文件定义的还是将来下沉到更底层的。少一处 export 就是
// 一个域整块 import 失败（Node 的 ESM 静态解析，启动即抛，不进请求路径）。
export {
  tenantOf, findClass, crossTenantHint,
  canManageMembers, canReadClass, canReadAttendance,
  readJsonBody,
};
