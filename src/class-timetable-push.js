// 课表下发（2026-11-02 · 「课表相关同步」）
//
// ── 为什么单独一个模块 ────────────────────────────────────────────────────
// 改造前这条链路**整体不存在**：`class-admin-students.js:adminImportTimetable` 落库后
// 只做两件事 —— 写一条审计事件、`pushClassEvent(..., 'timetable', ...)` 推给
// **管理端浏览器**。全程没有一个字节发往教室机。管理端界面显示「导入成功」，
// 而教室机读的是它本机的 ClassIsland 档案（`%LOCALAPPDATA%\ClassIsland\...`），
// 两边各自正确、中间从未接线 —— 这类缺陷不会有任何报错，只能靠读链路发现。
//
// 本模块负责三件事：
//   ① `pushTimetableToClass` 把一个班的课表信封下发给该班**所有在线设备**；
//   ② 记录每台设备的落地回执（`timetable-applied`）—— 没有回执，下发就永远只是
//      「发出去了」，老师无法知道哪台机器没收到；
//   ③ 给管理端一个可查的送达视图（哪些已落盘、哪些离线、哪些失败）。
//
// ── 设计取舍 ──────────────────────────────────────────────────────────────
// · **只发信封，不发整份 ClassIsland 档案**：信封是我们自己的归一化契约
//   （`classisland/v1`），端侧负责翻译成真机结构。云端一旦开始拼 ClassIsland 结构，
//   就等于把 ClassIsland 的内部格式变成我们的 ABI，将来它改版我们全线崩。
// · **回执按 uid 存、带时间戳、有上限**：设备可能重连重发；无上限的 Map 是内存泄漏。
// · **离线设备只在结果里如实计数**，不排队重发 —— 重发策略一旦在这里做，
//   就会与 `store.js` 的补发队列形成第二套真源，两边迟早打架。

import { getTimetable } from './class-data.js';
import { getDevicesOfClass, sendToClassDevices, getDeviceClassCode } from './registry.js';
import { pushEvent } from './registry.js';
import { DEFAULT_TENANT, normalizeTenantId } from './tenant.js';

/** 每台设备只留最近一条回执；上限防止长期运行堆积。 */
const MAX_RECEIPTS = 2000;
const receipts = new Map();   // uid -> { ok, changed, days, lessons, error, at, classCode }

/**
 * 把某班课表下发给全班在线设备。
 * @param {string} classCode 班级码（canonical 或别名，内部过 registry 的归一口径）
 * @param {string} tenantId  学校
 * @returns {{ok:boolean, error?:string, days?:number, lessons?:number, online?:number, delivered?:number}}
 */
export function pushTimetableToClass(classCode, tenantId = DEFAULT_TENANT) {
  const tn = normalizeTenantId(tenantId);
  const code = String(classCode ?? '').trim();
  if (!code) return { ok: false, error: '缺班级码' };

  const got = getTimetable(code, tn);
  if (!got.ok) return { ok: false, error: got.error || '读课表失败' };
  if (!got.profile) {
    // 没存过课表 ≠ 下发一张空课表：空课表会让教室机把当天课全清掉，属于数据事故。
    return { ok: false, error: '这个班还没有课表，先导入或保存一份再下发' };
  }

  const days = Array.isArray(got.profile.days) ? got.profile.days.length : 0;
  const lessons = (got.profile.days || []).reduce(
    (n, d) => n + (Array.isArray(d?.lessons) ? d.lessons.length : 0), 0);

  // sendToClassDevices 内部走 getDevicesOfClass（已过 canonicalCode 归一），
  // 所以这里用别名调用也能命中 —— 与「绑班过解析器、查班也必须过」是同一口径。
  const sent = sendToClassDevices(code, 'timetable-apply', {
    profile: got.profile,
    updatedAt: got.updatedAt,
  }, tn);

  pushEvent('info', '课表已下发到班级设备', {
    classCode: code, tenantId: tn, days, lessons,
    online: sent.online, delivered: sent.delivered,
  });

  return { ok: true, classCode: code, days, lessons,
           online: sent.online, delivered: sent.delivered };
}

/**
 * 记录设备落地回执（由 index.js 在收到 `timetable-applied` 时调用）。
 * 回执里带 classCode 更好（能核对归属），设备不报时按已知归属补。
 */
export function handleTimetableReceipt(uid, payload = {}) {
  const key = String(uid || '').trim();
  if (!key) return { ok: false, error: '缺 uid' };
  const p = payload && typeof payload === 'object' ? payload : {};
  const rec = {
    ok: p.ok === true,
    changed: p.changed === true,
    days: Number(p.days) || 0,
    lessons: Number(p.lessons) || 0,
    error: String(p.error || '').slice(0, 200),
    at: Date.now(),
    classCode: String(p.classCode || getDeviceClassCode(key) || ''),
  };
  receipts.delete(key);          // 重新插入 → Map 迭代序即「最近更新在后」
  receipts.set(key, rec);
  if (receipts.size > MAX_RECEIPTS) {
    for (const k of receipts.keys()) {
      receipts.delete(k);
      if (receipts.size <= MAX_RECEIPTS) break;
    }
  }
  if (!rec.ok) {
    pushEvent('warn', '设备课表落地失败', { uid: key, error: rec.error || '（设备未说明原因）' });
  }
  return { ok: true };
}

/**
 * 某班设备的送达视图：把「该班所有设备」与「最近回执」并起来。
 * 没回执的设备单独列在 pending 里 —— 「没收到」和「收到但失败」是两种不同的运维动作。
 */
export function timetableDelivery(classCode, tenantId = DEFAULT_TENANT) {
  const tn = normalizeTenantId(tenantId);
  const code = String(classCode ?? '').trim();
  const uids = getDevicesOfClass(code, tn);
  const delivered = [];
  const pending = [];
  for (const uid of uids) {
    const r = receipts.get(uid);
    if (r) delivered.push({ uid, ...r });
    else pending.push(uid);
  }
  return { ok: true, classCode: code, total: uids.length, delivered, pending };
}

/** 单台设备最近一条回执（诊断用；没回过就是 null，别编造）。 */
export function timetableReceiptOf(uid) {
  return receipts.get(String(uid || '').trim()) || null;
}
