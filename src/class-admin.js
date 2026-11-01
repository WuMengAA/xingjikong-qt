// 班级管理操作（2026-10-24 · 管理端「班级管理」面板的云端侧）
//
// 四个动作：建班（只建身份）/ 改名 / 删班（策略显式）/ 移出设备。
// 为什么单独开文件而不长在 index.js：index.js 已是 oversize 白名单住户
// （P1 拆 router 计划不变），班级管理是自洽的一整块，进独立 TU。
// 路由分发只在 index.js 留一个点（/api/class/* → handleClassAdminApi）。
//
// 设计纪律：
//   · canonical 码永不动（class-dir 铁律）——改名只动显示名，删班不留残留索引；
//   · 建班只建身份不绑设备（管理端要能"先把班建好，设备慢慢绑"）；
//   · 删班必须显式策略：默认 refuse（有设备就拒，防误删一整个班），
//     release 才把设备全部移出后删除 —— 不给"静默默认删设备"的选项；
//   · 改名/建班走 pushClassEvent 让该班订阅者实时可见（Timeline 一条）；
//   · 删班走 dropClassSubs：受影响订阅者收 class-unsubscribed，本地置未订，
//     不假装班还在（信任底线：状态不许造假）。

import {
  resolveClassCode, renameClass, deleteClass, getClass, tenantOfClass,
} from './class-dir.js';
import {
  bindDeviceToClass, unbindDeviceFromClass, listClasses, notifyClassBoundIfOnline, pushEvent,
  dropClassIndex, sendToClassDevices,
} from './registry.js';
import { dropClassSubs, pushClassEvent } from './class-events.js';
import {
  addMember, removeMember, listMembers, roleOf, classesOf,
  dropRoomEvents, dropRoomMembers, CLASS_ROLES,
} from './class-room.js';
import { canAccess } from './perms.js';
import {
  listStudents, addStudent, updateStudent, removeStudent, importStudents,
  getTimetable, saveTimetable, dropClassData,
} from './class-data.js';
// 2026-10-31：班级考勤（云端数据与 API 先行）。纯数据层在此，权限封装在下方 admin*。
import {
  recordAttendance, batchRecord, listByClassDate, listByClassRange,
  getAttendance, updateAttendance, removeAttendance, attendanceStats,
  dropClassAttendance,
} from './class-attendance.js';
// 2026-10-25：ClassIsland 档案 → 归一化课表信封（CIMS 退役后课表回落到我们自己的库，
// 但格式保持与 ClassIsland 互通：老师手里的档案能直接导进来）。
// classIslandGroupMeta：群顺序 + 「几班」标签（12 个群同名时靠首次出现序推断，不猜名字）
import { normalizeClassIslandProfile, classIslandGroupMeta, TIMETABLE_SCHEMA } from './class-timetable.js';
// 2026-10-25：班级接入码收归我方 —— 票据系统本就在 bind-qr.js（与设备自助码同一套码表、
// 同一套 normalizeCode/过期清理），这里只做「按班签发」这一层，不另造码表。
import { issueClassBind, getTicket, revokeTicket } from './bind-qr.js';
import { DEFAULT_TENANT, normalizeTenantId } from './tenant.js';

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

/** 建班：只建班级身份，不碰任何设备。已存在（同码/同名）→ 幂等返回既有班。 */
export function adminCreateClass({ code = '', name = '' } = {}, ident = null) {
  const tn = tenantOf(ident);
  const r = resolveClassCode({ code, name, source: 'admin', tenantId: tn });
  if (!r.ok) return { ok: false, error: r.error };
  pushEvent('info', '管理端创建班级', { classCode: r.code, className: r.name, tenantId: tn });
  return { ok: true, classCode: r.code, name: r.name, aliases: r.aliases };
}

/**
 * 改名：只动显示名（canonical 码永不动）。
 * 刻意**不发**班级域事件：class-event 限流表键是 uid×kind（不含班码），
 * 同一管理员连改两个班第二个会被吞；且 Timeline 是设备动态流，班级元数据
 * 变更混进去会让前端多一种要处理的 kind。新鲜度由管理端「写操作成功后
 * 立即重拉 /api/classes」保证（不等 15s 轮询），运维审计走 pushEvent。
 */
export function adminRenameClass(code, name, ident = null) {
  const c = String(code ?? '').trim();
  const tn = tenantOf(ident);
  if (!canManageMembers(ident, c)) return { ok: false, error: '无权限管理该班' };
  const r = renameClass(code, name, tn);
  if (!r.ok) return r;
  pushEvent('info', '管理端修改班级名', { classCode: r.class.code, className: r.class.name });
  return { ok: true, classCode: r.class.code, name: r.class.name };
}

/**
 * 删班。strategy：
 *   · 'refuse'（默认）—— 班内还有设备时拒绝，返回设备数让管理端知道规模；
 *   · 'release'       —— 先把设备全部移出班级（knownDevices.classCode 清空），再删。
 * 两种情况都会清掉该班全部订阅并通知（dropClassSubs）。
 */
export function adminDeleteClass(code, strategy = 'refuse', ident = null) {
  const c = String(code ?? '').trim();
  const tn = tenantOf(ident);
  if (!canManageMembers(ident, c)) return { ok: false, error: '无权限管理该班' };
  const { hit } = findClass(c, ident);
  if (!hit) {
    // 别校有同码班时，对外仍是"班级不存在"（不泄漏别校班级清单），只落排障日志。
    const other = crossTenantHint(c);
    if (other) console.warn('[class-admin] 删班命中跨租户同码班，已拒', { code: c, mine: tn, other });
    return { ok: false, error: '班级不存在' };
  }
  const devs = listClasses(tn).find((x) => x.code === hit.code);
  const uidList = devs ? devs.devices : [];
  if (uidList.length > 0 && strategy !== 'release') {
    return { ok: false, error: `班内还有 ${uidList.length} 台设备，确认移出后重试（strategy=release）`, deviceCount: uidList.length };
  }
  for (const uid of uidList) unbindDeviceFromClass(hit.code, uid, tn);
  // 运行时索引条目也必须清：只删 class-dir 的话，listClasses 的兜底分支会把
  // 目录已删的班又吐出来（幽灵班：devices 空、source 空）。回归见
  // tests/regression-class-admin.mjs ⑩。
  dropClassIndex(hit.code, tn);
  const dropped = dropClassSubs(hit.code);
  const r = deleteClass(hit.code, tn);
  if (!r.ok) return r;
  // 房间内容随班一起销毁（成员表 + 消息历史）。顺序刻意放在身份删成功**之后**：
  // 反过来先清内容再删身份，一旦身份删失败就留下"空壳但有成员"的幽灵班。
  const ev = dropRoomEvents(r.code, tn);
  const mb = dropRoomMembers(r.code, tn);
  // 班级数据（学生名单 + 课表）随班销毁：不留孤儿行（与身份删成功之后同序）。
  const cd = dropClassData(r.code, tn);
  // 考勤记录同样随班销毁（2026-10-31）：留着会成为查不到班的幽灵行。
  const at = dropClassAttendance(r.code, tn);
  pushEvent('warn', '管理端删除班级', {
    classCode: r.code, className: r.name, released: uidList.length, subsDropped: dropped,
    eventsDropped: ev.deleted, membersDropped: mb.deleted, studentsDropped: cd.students,
    attendanceDropped: at.removed,
  });
  return {
    ok: true, code: r.code, name: r.name, releasedDevices: uidList.length,
    subsDropped: dropped, eventsDropped: ev.deleted, membersDropped: mb.deleted,
    studentsDropped: cd.students, attendanceDropped: at.removed,
  };
}

/** 移出一台设备（退班）。在线设备同步收到 class-bound{code:''} 清端侧状态。 */
export function adminUnbindDevice(code, uid, ident = null) {
  const c = String(code ?? '').trim();
  const u = String(uid ?? '').trim();
  if (!c || !u) return { ok: false, error: '缺班级码或 uid' };
  const tn = tenantOf(ident);
  if (!canManageMembers(ident, c)) return { ok: false, error: '无权限管理该班' };
  const devs = listClasses(tn).find((x) => x.code === c);
  if (!devs || !devs.devices.includes(u)) return { ok: false, error: '该设备不在此班' };
  unbindDeviceFromClass(c, u, tn);
  notifyClassBoundIfOnline(u, '', '');
  pushEvent('info', '管理端将设备移出班级', { classCode: c, uid: u });
  return { ok: true, classCode: c, uid: u };
}

/**
 * 入班/换班：把一台设备绑到某班（在线即下发 class-bound，G4）。
 * 与 index.js 的 /api/device/bind-class 同一实现；给 /api/class/bind 是为了
 * 让管理端四个班级写操作同走 /api/class/ 前缀（QML 侧一个 ClassAdmin 打全部）。
 */
export function adminBindDevice(code, uid, name = '', ident = null) {
  const c = String(code ?? '').trim();
  const u = String(uid ?? '').trim();
  if (!c || !u) return { ok: false, error: '缺班级码或 uid' };
  const tn = tenantOf(ident);
  if (!canManageMembers(ident, c)) return { ok: false, error: '无权限管理该班' };
  const r = bindDeviceToClass(c, u, name, 'cloud', tn);
  if (!r.ok) return r;
  notifyClassBoundIfOnline(u, r.classCode, r.class.name);
  pushEvent('info', '管理端将设备加入班级', { classCode: r.classCode, uid: u });
  return { ok: true, classCode: r.classCode, name: r.class.name };
}

/* ───────────────────── 班级成员（人可以被添加） ─────────────────────
 * 2026-10-24 用户诉求：「数据保存在云端，人可以被添加」。成员表是这条的落地：
 * 把一个人的账号挂到班上，他下次连上来就能订这个班、看这个班的设备。
 *
 * 权限模型（两道）：
 *   ① 谁能管成员 —— 票里 scopes 覆盖这个班（canAccess）**或** 本班 owner/teacher；
 *   ② 能给出多高的角色 —— 只有拿到 ① 的 canAccess 那一路才能设 owner
 *      （防"助教把自己提成 owner"的提权链：成员表是权限真源，能写它就等于能提权）。
 */

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

/**
 * 把一个人加进班（可指定 owner/teacher/viewer，缺省 teacher）。
 * 幂等：重复加不报错、不降权（见 class-room.addMember）。
 */
export function adminAddMember(code, uid, role = 'teacher', ident = null) {
  const c = String(code ?? '').trim();
  const u = String(uid ?? '').trim();
  const want = String(role ?? '').trim().toLowerCase();
  if (!c || !u) return { ok: false, error: '缺班级码或成员账号' };
  if (!CLASS_ROLES.includes(want)) return { ok: false, error: `角色不合法（可选 ${CLASS_ROLES.join('/')}）` };
  const { hit, tn } = findClass(c, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班成员' };
  if (want === 'owner' && !canAccess(ident, hit.code, { tenantId: tn })) {
    return { ok: false, error: '只有管理员可指派 owner' };
  }
  const r = addMember(hit.code, { id: u, type: 'user', role: want, by: ident?.uid ?? 'system' }, tn);
  if (!r.ok) return r;
  pushEvent('info', r.added ? '管理端添加班级成员' : '管理端更新班级成员', {
    classCode: hit.code, memberUid: u, role: r.role, by: ident?.uid ?? 'system',
  });
  // 若该成员此刻正在线且已订阅别处，前端会在他下次 subscribe_class 时拿到 myRole；
  // 这里不主动推（他不知道有这个班，主动推等于给未知对象发通知）。
  return { ok: true, classCode: hit.code, uid: u, role: r.role, added: r.added };
}

/** 把一个人移出班（幂等）。不给"移出最后一个 owner"——那个班就没人管了。 */
export function adminRemoveMember(code, uid, ident = null) {
  const c = String(code ?? '').trim();
  const u = String(uid ?? '').trim();
  if (!c || !u) return { ok: false, error: '缺班级码或成员账号' };
  const { hit, tn } = findClass(c, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班成员' };
  const cur = roleOf(hit.code, u, tn);
  if (!cur) return { ok: true, removed: false, classCode: hit.code, uid: u };   // 幂等
  if (cur === 'owner' && listMembers(hit.code, 'user', tn).filter((m) => m.role === 'owner').length <= 1) {
    return { ok: false, error: '这是该班唯一的 owner，请先指派另一位 owner' };
  }
  const r = removeMember(hit.code, u, 'user', tn);
  if (!r.ok) return r;
  pushEvent('info', '管理端移除班级成员', { classCode: hit.code, memberUid: u, by: ident?.uid ?? 'system' });
  return { ok: true, removed: r.removed, classCode: hit.code, uid: u };
}

/** 成员清单（含角色）。
 *  ⚠️ 2026-10-26：这里原本**完全没有鉴权**（签名里连 ident 都没有），
 *  而成员清单带 uid + 角色，等于"任何已鉴权连接都能把全校每个班的负责人名单拉走"。
 *  与 adminListStudents / adminGetTimetable 同档：读权限走 canReadClass
 *  （本班任一角色、或 scopes 覆盖、或全权/平台超管）。
 *  全权身份（静态令牌 / 无 scopes 旧票）在 canAccess 里恒真 ⇒ 运维视角不受影响。 */
export function adminListMembers(code, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadClass(ident, hit.code)) return { ok: false, error: '无权限查看该班成员' };
  return { ok: true, classCode: hit.code, name: hit.name, members: listMembers(hit.code, 'user', tn) };
}

/**
 * 我的班级（管理端启动时用它决定"默认打开哪个班"，不用全量拉目录）。
 *
 * 2026-11-02 多租户：**只列当前租户内的班**。同一个人可能同时挂在两所学校，
 * 但一次登录只代表一个租户身份 —— 把别校的班混进来，界面会显示成"我的班"，
 * 点进去再被 canAccess 拒，等于自造一条错误路径。
 */
export function adminMyClasses(ident) {
  const uid = ident?.uid ? String(ident.uid) : '';
  if (!uid) return { ok: true, classes: [] };
  const tn = tenantOf(ident);
  const mine = classesOf(uid, tn).map((x) => {
    const hit = getClass(x.classCode, tn);
    return { classCode: x.classCode, role: x.role, name: hit ? hit.name : x.classCode };
  });
  return { ok: true, uid, tenantId: tn, classes: mine };
}

/* ───────────────── 班级接入码（2026-10-25 · 收归我方管理）─────────────────
 * 用户裁决：「班级接入码收归我方管理，作为班级管理面板的入口码，所以去掉 CIMS
 * 时期的旧方式」。
 *
 * 语义：**这台设备凭它进入这个班**。一次性、绑班级、带 TTL（24h，够贴一天课）。
 * 与「设备自助码」的分工（见 bind-qr.js 头注释，同一个码表两种来路）：
 *   · 本码 kind=class，**码里带班级** ⇒ 设备输码直接绑（POST /api/device/activate）
 *   · 自助码 kind=device，码里无班级 ⇒ 手机扫码后选班 + 确认
 *
 * ⚠️ 为什么删掉 CIMS 那条（XJK-XXXX-XXX → 云端代兑换站点 /api/device/activate）：
 *   旧路要跨一个服务、要配 STE_SITE_URL、还要靠 class-dir 把站点 class_id 归一成
 *   云端 cls-xxxx —— 一条链上两个班级真源，出问题时分不清是谁的码不对。现在班级
 *   真源就在云端，设备端只认识云端一个地址，没必要再绕一圈。
 *
 * 权限：能管成员 = 能发码（能把设备领进这个班的人，就是能管这个班的人）——
 * 与 member-add / member-remove 同口径，不另发明一套。
 * 不复用 /api/admin/bindcode：那是管理面路由、全项目零调用者，已随本批删除；
 * 班级域的事归 /api/class/*，鉴权口径也才和面板其他地方一致。
 */
export function adminIssueBindCode(code, ident = null) {
  const c = String(code ?? '').trim();
  if (!c) return { ok: false, error: '缺班级码' };
  const { hit, tn } = findClass(c, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限为该班生成接入码' };
  const issued = issueClassBind(hit.code, hit.name || '', tn);
  if (!issued.ok) return issued;
  const t = issued.ticket;
  pushEvent('info', '已生成班级接入码', {
    classCode: hit.code, code: t.code, by: ident?.uid ?? 'system',
  });
  // 不回 matrix：接入码是给**设备输**的（6 位无歧义字符），手机扫它也没有设备身份
  // 可用（bind.html 对 kind=class 只把码显示出来）。少算一次 QR = 少一处能错的地方。
  return {
    ok: true,
    classCode: hit.code,
    className: hit.name || '',
    code: t.code,
    expiresAt: t.expireAt,
    // 供 UI 显示"还剩多久"：秒数比时间戳好渲染，且不受两端时钟漂移影响太大。
    ttlSec: Math.max(0, Math.round((t.expireAt - Date.now()) / 1000)),
  };
}

/** 作废一张接入码（发错班了就撤回；TTL 内也立刻失效）。 */
export function adminRevokeBindCode(code, ident = null) {
  const c = String(code ?? '').trim();
  if (!c) return { ok: false, error: '缺接入码' };
  const t = getTicket(c);
  if (!t) return { ok: false, error: '接入码无效或已过期' };
  // 票里带着班级 ⇒ 用班级校验权限（拿不到班级的码不许人乱撤）。
  if (t.classCode) {
    const { hit } = findClass(t.classCode, ident);
    if (hit && !canManageMembers(ident, hit.code)) {
      return { ok: false, error: '无权限作废该班的接入码' };
    }
  }
  const r = revokeTicket(c);
  if (r.ok) pushEvent('info', '已作废班级接入码', { classCode: t.classCode || '', code: t.code });
  return r;
}

/* ───────────────────── 发布中心（P1，2026-10-24）─────────────────────
 * 把一张卡片推到全班教室黑板（被控端）。权限走 canManageMembers（admin/owner/teacher）；
 * 重认证门由 index.js 的 /api/class/* POST 统一守（过期的票据者已被 403 拦在门外）。
 * 推送同时落一条 class-event(kind=publish)，让管理端 Timeline 也能看到"发了什么"。
 *
 * 类型（type）—— 与参考 App「掌上看班」发布弹窗对齐：
 *   student-notice     学生通知（顶部跑马灯）
 *   class-notice      班级通知（顶部跑马灯）
 *   fullscreen-notice 全屏通知（被控端全屏文字窗）
 *   homework          课后作业（全屏文字窗 + Timeline 留痕）
 */
const PUBLISH_TYPES = ['student-notice', 'class-notice', 'fullscreen-notice', 'homework'];

export function adminPublish(code, card = {}, ident = null) {
  const c = String(code ?? '').trim();
  const { hit, tn } = findClass(c, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限向该班发布' };
  const t = String(card?.type ?? '').trim();
  if (!PUBLISH_TYPES.includes(t)) return { ok: false, error: `不支持的发布类型：${t || '（空）'}` };
  const title = String(card?.title ?? '').trim().slice(0, 200);
  const body = String(card?.body ?? '').trim().slice(0, 4000);
  if (!title && !body) return { ok: false, error: '发布内容不能为空' };
  const from = ident?.uid ? String(ident.uid) : 'system';
  const payload = {
    id: `${hit.code}-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`,
    type: t, title, body, from,
    at: new Date().toISOString(),
  };
  // 推全班在线被控端（黑板渲染由被控端按 type 分流：全屏窗 / 顶部跑马灯）
  const r = sendToClassDevices(hit.code, 'blackboard-card', payload, tn);
  // 落 Timeline（与管理端订阅者共享同一 class-event 通道，老师端实时可见"发了什么"）。
  // distinctContent：限流键并入内容指纹 ⇒ 1s 内连发两条**不同**内容都留痕；
  // 只有**内容相同**（老师手抖连点两下）才被限流吞掉。不加这个，第二条会被
  // uid×kind 粗粒度闸门静默吞掉 —— 设备收到了、Timeline 没痕迹（查不出发过什么）。
  pushClassEvent(hit.code, 'publish', { type: t, title, body, from }, from,
                 { distinctContent: true });
  pushEvent('info', '管理端向班级发布卡片', {
    classCode: hit.code, type: t, delivered: r.delivered, online: r.online, from,
  });
  return { ok: true, classCode: hit.code, type: t, delivered: r.delivered, online: r.online };
}

/* ───────────────── 学生名单 + 班级课表（2026-10-25） ─────────────────
 * 用户裁决「云端数据库存储，不要本机了」：这两样都跟班走，不跟机器走。
 *
 * 权限（与成员管理同一套思路，但读写分档）：
 *   · 读 —— 本班任一角色（owner/teacher/viewer）或票里 scopes 覆盖；
 *   · 写 —— canManageMembers（owner/teacher 或管理员）：
 *           名单和课表是「全班都要照着做」的东西，不能谁路过了都能改。
 */

/** 能否读这个班的班级数据（读权限比写宽一档：viewer 也该看得见名单）。 */
function canReadClass(ident, code) {
  if (!ident) return false;
  const tn = tenantOf(ident);
  if (canAccess(ident, code, { tenantId: tn })) return true;
  const role = ident.uid ? roleOf(code, ident.uid, tn) : null;
  return !!role;
}

/** 学生名单（按座位/学号排）。 */
export function adminListStudents(code, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadClass(ident, hit.code)) return { ok: false, error: '无权限查看该班学生名单' };
  const r = listStudents(hit.code, tn);
  return { ...r, name: hit.name };
}

export function adminAddStudent(code, student = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班学生名单' };
  const r = addStudent(hit.code, student, ident?.uid ?? 'system', tn);
  if (!r.ok) return r;
  pushEvent('info', r.replaced ? '管理端更新学生信息' : '管理端新增学生', {
    classCode: hit.code, studentId: r.student.id, name: r.student.name, by: ident?.uid ?? 'system',
  });
  return r;
}

export function adminUpdateStudent(code, studentId, patch = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班学生名单' };
  const r = updateStudent(hit.code, studentId, patch, ident?.uid ?? 'system', tn);
  if (r.ok) pushEvent('info', '管理端修改学生信息', { classCode: hit.code, studentId, by: ident?.uid ?? 'system' });
  return r;
}

export function adminRemoveStudent(code, studentId, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班学生名单' };
  const r = removeStudent(hit.code, studentId, tn);
  if (r.ok && r.removed) pushEvent('info', '管理端移除学生', { classCode: hit.code, studentId, by: ident?.uid ?? 'system' });
  return r;
}

export function adminImportStudents(code, rows = [], ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限管理该班学生名单' };
  const r = importStudents(hit.code, rows, ident?.uid ?? 'system', tn);
  if (r.ok) pushEvent('info', '管理端批量导入学生', {
    classCode: hit.code, added: r.added, updated: r.updated, bad: r.bad, by: ident?.uid ?? 'system',
  });
  return r;
}

/** 班级课表（没存过 → profile:null，前端必须显示「还没排课」而不是空白表）。 */
export function adminGetTimetable(code, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadClass(ident, hit.code)) return { ok: false, error: '无权限查看该班课表' };
  const r = getTimetable(hit.code, tn);
  return { ...r, name: hit.name };
}

export function adminSaveTimetable(code, profile, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限修改该班课表' };
  const r = saveTimetable(hit.code, profile, ident?.uid ?? 'system', tn);
  if (r.ok) pushEvent('info', '管理端保存班级课表', { classCode: hit.code, bytes: r.bytes, by: ident?.uid ?? 'system' });
  return r;
}

/* ───────────── 课表导入：ClassIsland 档案（2026-10-25）─────────────
 * 为什么要在云端做归一化（而不是让每个客户端各写一份解析）：
 *   · 老师的档案来自 ClassIsland，桌面端 / 平板端 / 以后的小程序都要能吃；
 *   · 解析规则（第 N 节课 ↔ 作息第 N 个上课时段）一旦两端各写，必然漂；
 *   · 归一化是纯函数，单测好写、出错能回人话。
 * 多课表群（一份档案含多个班）时必须显式指定 —— 猜错就是把 8 班课表挂到 1 班。
 */
export function adminImportTimetable(code, rawProfile, groupKey = '', ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限修改该班课表' };
  const norm = normalizeClassIslandProfile(rawProfile, groupKey);
  if (!norm.ok) {
    // groups 回给 UI：让老师自己选，而不是我们猜
    return { ok: false, error: norm.error, groups: norm.groups || [] };
  }
  const r = saveTimetable(hit.code, norm.normalized, ident?.uid ?? 'system', tn);
  if (r.ok) {
    // 审计口径统一用「X班」标签（群名可能 12 个都叫「新课表群」，写进日志等于没写）
    const g = norm.normalized.group;
    pushEvent('info', '管理端导入 ClassIsland 课表', {
      classCode: hit.code, group: g.label || g.name,
      days: norm.normalized.days.length, bytes: r.bytes, by: ident?.uid ?? 'system',
    });
    pushClassEvent(hit.code, 'timetable', {
      group: g.label || g.name, days: norm.normalized.days.length,
      schema: TIMETABLE_SCHEMA,
    }, ident?.uid ?? 'system');
  }
  return { ...r, group: norm.normalized.group, days: norm.normalized.days.length,
           lessons: norm.normalized.days.reduce((n, d) => n + d.lessons.length, 0) };
}

/**
 * 列一份档案里的课表群（UI 在导入前预览用；不落库）。
 * nameSource='order' 表示档案里 12 个群同名，班名是按出现顺序推的 ⇒ 界面必须提示核对。
 */
export function adminListTimetableGroups(rawProfile) {
  const meta = classIslandGroupMeta(rawProfile);
  return meta.groups.length
    ? { ok: true, groups: meta.groups, nameSource: meta.nameSource, conflict: meta.conflict }
    : { ok: false, error: '档案里没有任何日课表（ClassPlan 为空或格式不对）', groups: [] };
}

/* ───────────── 班级考勤（2026-10-31 · 云端数据与 API 先行）─────────────
 * 数据层在 class-attendance.js（纯 CRUD + 查询，不碰权限）；这里只做权限封装与 HTTP 分发。
 *
 * 权限口径（与班级数据同一套思路，读写分档）：
 *   · 读（按班按日列表 / 统计 / 区间）—— canReadClass：本班任一角色（owner/teacher/viewer）
 *     或票里 scopes 覆盖（与名单/课表同宽，班主任·老师·广播室之外的本班成员也看得见）；
 *   · 写（记录 / 批量 / 改判 / 删除）—— canManageMembers：owner/teacher 或管理员
 *     （考勤是「全班都要照着做」的判定，不能谁路过都能改）。
 * 记录人（recorder）一律用当前登录身份 uid，不从客户端取（防伪造记录人）。
 *
 * ⚠️ 角色模型沿用现有人三档（管理员 / 班主任·老师 / 广播室），不另造考勤专属角色；
 *   广播室在本班若有 owner/teacher 角色也可记录，否则只能看 —— 口径与其他班级数据一致。
 */

/** 谁能读这个班的考勤（读权限比写宽一档，与 canReadClass 同语义）。 */
function canReadAttendance(ident, code) {
  if (!ident) return false;
  const tn = tenantOf(ident);
  if (canAccess(ident, code, { tenantId: tn })) return true;
  const role = ident.uid ? roleOf(code, ident.uid, tn) : null;
  return !!role;
}

/** 单条记录 / 改判。 */
export function adminRecordAttendance(code, input = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限记录该班考勤' };
  const r = recordAttendance(hit.code, input, ident?.uid ?? 'system', tn);
  if (!r.ok) return r;
  pushEvent('info', r.replaced ? '管理端改判考勤' : '管理端记录考勤', {
    classCode: hit.code, studentId: r.record.studentId, status: r.record.status, by: ident?.uid ?? 'system',
  });
  return r;
}

/** 批量记录（全班一次性提交）。 */
export function adminBatchAttendance(code, rows = [], ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限记录该班考勤' };
  const r = batchRecord(hit.code, rows, ident?.uid ?? 'system', tn);
  if (r.ok) pushEvent('info', '管理端批量记录考勤', {
    classCode: hit.code, total: r.total, succeeded: r.succeeded, failed: r.failed, by: ident?.uid ?? 'system',
  });
  return r;
}

/** 按班按日列出（可带 session 过滤）。 */
export function adminListAttendance(code, q = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadAttendance(ident, hit.code)) return { ok: false, error: '无权限查看该班考勤' };
  return listByClassDate(hit.code, String(q.date ?? ''), String(q.session ?? ''), tn);
}

/** 按班按日期区间列出。 */
export function adminListAttendanceRange(code, q = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadAttendance(ident, hit.code)) return { ok: false, error: '无权限查看该班考勤' };
  return listByClassRange(hit.code, String(q.fromDate ?? ''), String(q.toDate ?? ''), tn);
}

/** 某日考勤统计（含未记录人数）。 */
export function adminAttendanceStats(code, q = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canReadAttendance(ident, hit.code)) return { ok: false, error: '无权限查看该班考勤统计' };
  return attendanceStats(hit.code, String(q.date ?? ''), tn);
}

/** 删一条考勤（幂等）。 */
export function adminRemoveAttendance(code, q = {}, ident = null) {
  const { hit, tn } = findClass(code, ident);
  if (!hit) return { ok: false, error: '班级不存在' };
  if (!canManageMembers(ident, hit.code)) return { ok: false, error: '无权限修改该班考勤' };
  const r = removeAttendance(hit.code, String(q.studentId ?? ''), String(q.date ?? ''), String(q.session ?? ''), tn);
  if (r.ok && r.removed) pushEvent('info', '管理端删除考勤记录', { classCode: hit.code, studentId: q.studentId, by: ident?.uid ?? 'system' });
  return r;
}

/**
 * HTTP 路由：POST /api/class/{create,rename,delete,unbind,bind,member-add,member-remove,members,my-classes,publish,
 *    students,student-add,student-update,student-remove,students-import,
 *    timetable,timetable-save,timetable-import,timetable-groups,
 *    attendance-record,attendance-batch,attendance-list,attendance-range,attendance-stats,attendance-remove}
 * （鉴权在 index.js 的 isViewerApi 组，与 /api/device/bind-class 同一凭据口径；写操作过重认证门）。
 * timetable-import 收 **ClassIsland 原始档案** + 可选 group（多群时必填），云端归一化后落库；
 * timetable-groups 只预览档案里的课表群，不落库（导入前的选择器用）。
 */
export function handleClassAdminApi(req, res, u, send, ident = null) {
  const action = u.pathname.replace('/api/class/', '');
  readJsonBody(req, (body) => {
    if (body === null) return send(400, { ok: false, error: '请求体不是合法 JSON' });
    try {
      let r;
      if (action === 'create') {
        // ⚠️ ident 必须传：建班要落在**本租户**，否则两校都会往默认组织建班。
        r = adminCreateClass({ code: String(body.classCode ?? ''), name: String(body.className ?? '') }, ident);
      } else if (action === 'rename') {
        r = adminRenameClass(String(body.classCode ?? ''), String(body.className ?? ''), ident);
      } else if (action === 'delete') {
        r = adminDeleteClass(String(body.classCode ?? ''), String(body.strategy ?? 'refuse'), ident);
      } else if (action === 'unbind') {
        r = adminUnbindDevice(String(body.classCode ?? ''), String(body.uid ?? ''), ident);
      } else if (action === 'bind') {
        r = adminBindDevice(String(body.classCode ?? ''), String(body.uid ?? ''),
                            String(body.className ?? ''), ident);
      } else if (action === 'member-add') {
        r = adminAddMember(String(body.classCode ?? ''), String(body.uid ?? ''),
                           String(body.role ?? 'teacher'), ident);
      } else if (action === 'member-remove') {
        r = adminRemoveMember(String(body.classCode ?? ''), String(body.uid ?? ''), ident);
      } else if (action === 'members') {
        // ident 必须传：成员清单带 uid+角色，不能无鉴权可读（见 adminListMembers 注释）
        r = adminListMembers(String(body.classCode ?? ''), ident);
      } else if (action === 'my-classes') {
        r = adminMyClasses(ident);
      } else if (action === 'bindcode') {
        // 班级接入码（2026-10-25 收归我方）：发码 / 作废。权限同 member-*。
        r = adminIssueBindCode(String(body.classCode ?? ''), ident);
      } else if (action === 'bindcode-revoke') {
        r = adminRevokeBindCode(String(body.code ?? ''), ident);
      } else if (action === 'publish') {
        r = adminPublish(String(body.classCode ?? ''), body.card ?? {}, ident);
      } else if (action === 'students') {
        r = adminListStudents(String(body.classCode ?? ''), ident);
      } else if (action === 'student-add') {
        r = adminAddStudent(String(body.classCode ?? ''), body.student ?? {}, ident);
      } else if (action === 'student-update') {
        r = adminUpdateStudent(String(body.classCode ?? ''), String(body.studentId ?? ''),
                               body.patch ?? {}, ident);
      } else if (action === 'student-remove') {
        r = adminRemoveStudent(String(body.classCode ?? ''), String(body.studentId ?? ''), ident);
      } else if (action === 'students-import') {
        r = adminImportStudents(String(body.classCode ?? ''), body.rows ?? [], ident);
      } else if (action === 'timetable') {
        r = adminGetTimetable(String(body.classCode ?? ''), ident);
      } else if (action === 'timetable-save') {
        r = adminSaveTimetable(String(body.classCode ?? ''), body.profile ?? null, ident);
      } else if (action === 'timetable-import') {
        r = adminImportTimetable(String(body.classCode ?? ''), body.profile ?? null,
                                 String(body.group ?? ''), ident);
      } else if (action === 'timetable-groups') {
        r = adminListTimetableGroups(body.profile ?? null);
      } else if (action === 'attendance-record') {
        // 2026-10-31 考勤：单条记录 / 改判（upsert）。写权限 = canManageMembers。
        r = adminRecordAttendance(String(body.classCode ?? ''), body.record ?? {}, ident);
      } else if (action === 'attendance-batch') {
        // 全班一次性提交。写权限同 attendance-record。
        r = adminBatchAttendance(String(body.classCode ?? ''), body.records ?? [], ident);
      } else if (action === 'attendance-list') {
        // 按班按日列出（可带 session）。读权限 = canReadClass（本班任一角色 + scopes）。
        r = adminListAttendance(String(body.classCode ?? ''), body, ident);
      } else if (action === 'attendance-range') {
        // 按班按日期区间列出。读权限同 attendance-list。
        r = adminListAttendanceRange(String(body.classCode ?? ''), body, ident);
      } else if (action === 'attendance-stats') {
        // 某日考勤统计（四状态计数 + 未记录人数）。读权限同 attendance-list。
        r = adminAttendanceStats(String(body.classCode ?? ''), body, ident);
      } else if (action === 'attendance-remove') {
        // 删一条（幂等）。写权限 = canManageMembers。
        r = adminRemoveAttendance(String(body.classCode ?? ''), body, ident);
      } else {
        return send(404, { ok: false, error: `未知班级管理操作：${action}` });
      }
      send(r.ok ? 200 : 400, { ok: r.ok, ...r });
    } catch (e) {
      // 业务执行异常（如缺依赖的 ReferenceError）必须如实暴露，绝不可伪装成
      // 「请求体不是合法 JSON」——否则真实 bug 会被静默吞掉（本项目铁律：不许 fail-silent）。
      // 这里落日志并回 500，而不是让异常冒泡成未捕获异常把进程带崩。
      console.error('[class-admin] 处理异常：', e && e.stack || e);
      if (!res.headersSent) send(500, { ok: false, error: '服务器内部错误' });
    }
  });
}
