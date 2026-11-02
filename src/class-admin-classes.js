// 班级身份管理（2026-11-02 · 屎山整理 #4，从 class-admin.js 逐字搬出）
//
// 「一个班的户口本」：建班 / 改名 / 删班 / 绑解绑设备 / 成员 / 我的班 / 接入码 / 发布。
// 设计纪律（原文照搬，未改）：
//   · canonical 码永不动（class-dir 铁律）——改名只动显示名，删班不留残留索引；
//   · 建班只建身份不绑设备（管理端要能"先把班建好，设备慢慢绑"）；
//   · 删班必须显式策略：默认 refuse（有设备就拒，防误删一整个班），release 才把设备
//     全部移出后删除 —— 不给"静默默认删设备"的选项；
//   · 改名/建班走 pushClassEvent 让该班订阅者实时可见（Timeline 一条）；
//   · 删班走 dropClassSubs：受影响订阅者收 class-unsubscribed，本地置未订，
//     不假装班还在（信任底线：状态不许造假）。


import { resolveClassCode, renameClass, deleteClass, getClass } from './class-dir.js';
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
import { dropClassData } from './class-data.js';
import { dropClassAttendance } from './class-attendance.js';
import { issueClassBind, getTicket, revokeTicket } from './bind-qr.js';
import { tenantOf, findClass, crossTenantHint, canManageMembers, canReadClass } from './class-admin-shared.js';

/* 发布卡片类型白名单（2026-11-02 拆分回归修复）。
 *
 * ⚠️ 这个常量原在 class-admin.js 顶部，拆文件时被漏掉 —— 拆分脚本只搬函数，
 * 没搬同文件里的顶层常量。后果：adminPublish 引用未定义标识符，接口 500，
 * 而模块 import 照样成功、导出面一个不少（ESM 不做标识符静态检查）。
 * 是 regression-class-publish 的 7 条失败把它揪出来的 —— 这正说明
 * 「拆完必须跑真回归」，审计再全也替代不了发一次真请求。
 *
 * 四类（原注释照搬）：student-notice 学生公告 / class-notice 班级公告 /
 * fullscreen-notice 全屏通知 / homework 课后作业（全屏文字窗 + Timeline 留痕）。
 */
const PUBLISH_TYPES = ['student-notice', 'class-notice', 'fullscreen-notice', 'homework'];

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
    // 2026-11-02：回执补 timetableDropped —— dropClassData 一直删了课表但没把计数透出，
    // regression-class-data ⑮「名单与课表随班销毁」无法从回执确认课表这条腿。
  });
  return {
    ok: true, code: r.code, name: r.name, releasedDevices: uidList.length,
    subsDropped: dropped, eventsDropped: ev.deleted, membersDropped: mb.deleted,
    studentsDropped: cd.students, attendanceDropped: at.removed,
    timetableDropped: cd.timetables,
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
