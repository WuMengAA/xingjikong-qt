// 班级域事件通道（2026-10-23 · #458 班级工作台地基）
//
// ── 为什么单独开一条通道 ────────────────────────────────────────────────
// 云端现有三条广播路径，没有一条能承担"按班"这个维度：
//   ① devices 广播 = 全局设备表。多班老师会被推别班机器（设备表靠 viewer 侧 scopes
//      过滤，但"事件/内容"类数据没有班维度就没有过滤依据）；
//   ② pushEvent 事件流水 = 运维审计流（最近 50 条）。高频业务数据塞进去会把运维
//      事件挤爆——噪音样本 2s/一条已经踩过一遍（见 index.js noise 分支的注释）；
//   ③ relayToSubscribers(uid) = 按**设备**订阅中继。班级工作台要的是"整个班"的
//      内容流（成员上下线、缩略图、审批动态），不是"某一台"的帧。
// 所以班级域三条不混：**订阅按班**（subscribe_class）、**推送按班**
// （class-event：agent → cloud → 该班订阅者）、**历史按班**（每班一条环形缓冲，
// 后订者先拿快照再收实时）。
//
// 信任模型（与 interact.js 同口径）：设备自报的 classCode 是**不可信输入**，
// 班级归属一律以云端 knownDevices 表（getDeviceClassCode，含离线设备）为准 ——
// 载荷里带什么班级码都忽略。否则一台机器能往别班的事件流里注入内容，
// 老师在看的是"自己班"、收到的却是别人班的东西。
//
// 权限口径（契约 §4.3）：订阅班级必须过 canAccess(ident, classCode)，
// 与 subscribe / instruction / interact 完全同一套。订阅成功即代表该连接
// 对这个班有权限，后续 fanout 只投 classSubs 里的连接，不再逐个判断。

import { wrapOutgoing } from './protocol.js';
import {
  pushEvent, getDeviceClassCode, getDevicesOfClass, listClasses, listDevices, isClassCodeValid,
} from './registry.js';
import { getClass } from './class-dir.js';
import { filterDevices, maskDevices, canAccess } from './perms.js';
import {
  CLASS_EVENT_LOG_KEEP, CLASS_EVENT_SNAPSHOT, CLASS_EVENT_MIN_MS,
  CLASS_EVENT_MAX_BYTES, CLASS_RELAY_BUFFER_MAX,
} from './config.js';

/** 事件类型（kind）格式：小写字母/数字开头，后跟 ≤31 位 [a-z0-9_.-]，总长 ≤32。
 *  为什么限得这么死：kind 会进 Timeline 标签与前端 switch，放行任意字符串
 *  等于让设备端决定 UI 词汇表（与"学生是不可信输入源"同一纪律）。 */
const KIND_RE = /^[a-z0-9][a-z0-9_.-]{0,31}$/;

/** 缩略图事件 kind（与 control-qt `ThumbnailReporter.cpp` 上报值一致）。
 *  只走实时 fanout、**不进历史环**：15s/台 的高频帧，进环会把每班 200 条历史挤满图片
 *  （量大时 GB 级内存，而 Timeline 本就不展示缩略图）。 */
const THUMB_KIND = 'thumbnail';

/** 管理端连接 → 订阅的班级码集合。只在订阅过至少一个班时存在。 */
const classSubs = new Map();           // ws -> Set<classCode>

/** 每班一条环形事件缓冲（含成员上下线）：后订者先拿最近 N 条快照，再收实时。
 *  上限 CLASS_EVENT_LOG_KEEP（默认 200）—— Timeline 只看最近几十条，
 *  无限增长会把云端内存吃穿（噪音教训同款）。 */
const classEventLog = new Map();       // classCode -> Array<ev>

/** 限流表：`${uid}|${kind}` -> 上次投递时间戳。每 uid×kind 最小间隔
 *  CLASS_EVENT_MIN_MS（默认 1s）——为 #459 缩略图（每台每 1s+ 一张）预置的闸门，
 *  现在没有高频发送方也开着：先立规矩，等负载真来了不用改协议。 */
const lastDeliverAt = new Map();       // `${uid}|${kind}` -> ts

/** 事件自增 id（班级域内单调即可，用于端侧去重/回执对账）。 */
let evSeq = 0;

/** 发一条协议消息（按对端版本自动选信封格式，与 index.js 的 snd 同口径）。 */
function send(ws, type, payload) {
  return ws.send(wrapOutgoing(ws.__v1 === true, type, payload));
}

/**
 * 把一个班的事件投给该班的全部订阅者。
 * 背压/死连接的处理与 registry 的 relayToSubscribers 同口径：
 *   · readyState !== 1  → 摘掉这个连接（它已经死了，留着只是漏内存）
 *   · bufferedAmount 超限 → 跳过（管理端消费不过来时不硬塞，事件类数据
 *     丢一条远好过把心跳/指令一起堵死）
 * @returns {{hits:number, skipped:number}} hits=真送达数，skipped=背压跳过数
 */
function fanoutClass(code, type, payload) {
  let hits = 0;
  let skipped = 0;
  for (const [ws, codes] of classSubs) {
    if (!codes.has(code)) continue;
    if (ws.readyState !== 1) { classSubs.delete(ws); continue; }
    if (ws.bufferedAmount > CLASS_RELAY_BUFFER_MAX) { skipped++; continue; }
    try { send(ws, type, payload); hits++; }
    catch { classSubs.delete(ws); }
  }
  return { hits, skipped };
}

/** 入环形缓冲（超上限丢最旧的）。 */
function logEvent(code, ev) {
  let arr = classEventLog.get(code);
  if (!arr) { arr = []; classEventLog.set(code, arr); }
  arr.push(ev);
  if (arr.length > CLASS_EVENT_LOG_KEEP) arr.splice(0, arr.length - CLASS_EVENT_LOG_KEEP);
}

/** 取一个班最近的 N 条事件（ Timeline 快照用，默认 CLASS_EVENT_SNAPSHOT 条）。 */
function recentEvents(code, limit = CLASS_EVENT_SNAPSHOT) {
  const arr = classEventLog.get(code) || [];
  return arr.slice(Math.max(0, arr.length - limit)).map((e) => ({ ...e }));
}

/** 这个班当前有几个管理端在订（诊断/回执对账用）。 */
export function countClassSubs(code) {
  let n = 0;
  for (const [, codes] of classSubs) if (codes.has(code)) n++;
  return n;
}

/**
 * 班级快照：设备清单（按订阅者身份过滤+脱敏）+ 在线数 + 最近事件。
 * 订阅成功时随 class-subscribed 一次性推过去，管理端不用再轮询。
 */
function classSnapshot(code, ident) {
  const cls = listClasses().find((c) => c.code === code) || null;
  const byUid = new Map(listDevices().map((d) => [d.uid, d]));
  // getDevicesOfClass 给 uid 列表（含离线），listDevices 给完整设备对象；
  // 交集 = 这个班的设备全集。过滤/脱敏与 devices 消息同一口径（防御性守卫：
  // 订阅时已过 canAccess，这里再过滤一遍是纵深，不是主判）。
  const devs = getDevicesOfClass(code)
    .map((u) => byUid.get(u))
    .filter((d) => !!d);
  const visible = maskDevices(ident, filterDevices(ident, devs));
  return {
    classCode: code,
    name: (cls && cls.name) || (getClass(code) && getClass(code).name) || code,
    devices: visible,
    onlineCount: visible.filter((d) => d.online).length,
    events: recentEvents(code),
    at: new Date().toISOString(),
  };
}

/**
 * 订阅一个班。校验三段：码格式 → 权限 → 存在性。
 * @returns {{ok:true}} | {{ok:false, code:string, detail:string}} code 用于 sndErr
 */
export function subscribeClass(ws, code, ident) {
  const c = String(code ?? '').trim();
  if (!isClassCodeValid(c)) {
    return { ok: false, code: 'BAD_PAYLOAD', detail: '班级码格式不合法（需 6-16 位字母数字或 -_）' };
  }
  // 权限：订别班的班直接拒，并留审计（与 subscribe/instruction 同口径）
  if (!canAccess(ident, c)) {
    pushEvent('warn', '管理端越权：订阅班级被云端拒绝（目标不在其 scopes 内）',
      { classCode: c, who: ident?.uid ?? 'anon' });
    return { ok: false, code: 'NO_DEVICE_PERMISSION', detail: `无权限订阅班级 ${c}` };
  }
  // 存在性：班级目录（class-dir）或运行时索引（registry.classes）里有一处算数。
  // 两处都没有 = 这个班从来没存在过 —— 明确拒，不静默建个空班挂着。
  const exists = !!getClass(c) || getDevicesOfClass(c).length > 0
    || listClasses().some((x) => x.code === c);
  if (!exists) {
    return { ok: false, code: 'BAD_PAYLOAD', detail: `班级不存在：${c}` };
  }
  let codes = classSubs.get(ws);
  if (!codes) { codes = new Set(); classSubs.set(ws, codes); }
  codes.add(c);
  return { ok: true };
}

/** 退订（幂等：没订过也回 ok，管理端重发/刷新时不报错）。 */
export function unsubscribeClass(ws, code) {
  const c = String(code ?? '').trim();
  const codes = classSubs.get(ws);
  if (codes) {
    codes.delete(c);
    if (codes.size === 0) classSubs.delete(ws);
  }
  return { ok: true };
}

/** 连接断开：摘掉它的全部班级订阅（防 classSubs 漏内存 + 往死连接投递）。 */
export function removeViewerClasses(ws) {
  classSubs.delete(ws);
}

/**
 * 设备上报一条班级事件（agent → cloud → 该班订阅者）。
 *
 * 班级归属**只认云端表**：getDeviceClassCode(uid)，载荷里的 classCode 一律忽略
 * （设备自报不可信，见文件头）。未绑班的设备拒收并如实回错。
 *
 * 四道闸门（为 #459 缩略图负载预置，现在无高频发送方也开着）：
 *   ① kind 格式（KIND_RE）——放行任意 kind 等于让设备端决定 UI 词汇表；
 *   ② data 必须是对象、JSON 序列化后 ≤ CLASS_EVENT_MAX_BYTES（默认 64KB），
 *      且过一遍 JSON 往返做深拷贝（防引用逃逸 + 保证一定序列化得出去）；
 *   ③ 每 uid×kind 最小间隔 CLASS_EVENT_MIN_MS（默认 1s）——同 uid 同 kind
 *      连发直接限流，回执带 throttled:true，端侧自己知道被收了；
 *   ④ fanout 背压（bufferedAmount > CLASS_RELAY_BUFFER_MAX 跳过）。
 *
 * @returns {{ok:boolean, throttled?:boolean, id?:number, viewers?:number, code?:string, message?:string}}
 */
export function pushClassEvent(code, kind, data, uid) {
  const k = String(kind ?? '');
  if (!KIND_RE.test(k)) {
    return { ok: false, code: 'BAD_PAYLOAD', message: 'kind 格式不合法（小写开头，≤32 位 [a-z0-9_.-]）' };
  }
  if (!data || typeof data !== 'object' || Array.isArray(data)) {
    return { ok: false, code: 'BAD_PAYLOAD', message: 'data 必须是非数组对象' };
  }
  let raw;
  try { raw = JSON.stringify(data); }
  catch { return { ok: false, code: 'BAD_PAYLOAD', message: 'data 无法序列化' }; }
  if (Buffer.byteLength(raw) > CLASS_EVENT_MAX_BYTES) {
    return { ok: false, code: 'BAD_PAYLOAD', message: `事件数据过大（上限 ${CLASS_EVENT_MAX_BYTES} 字节）` };
  }

  // 限流：同 uid×kind 最小间隔。命中不投递、不回错（这是正常负载保护，不是故障），
  // 回执带 throttled:true 让端侧知道自己被收了（纪律：不许静默丢弃）。
  const tkey = `${uid}|${k}`;
  const now = Date.now();
  const last = lastDeliverAt.get(tkey) || 0;
  if (now - last < CLASS_EVENT_MIN_MS) {
    return { ok: true, throttled: true, viewers: 0 };
  }
  lastDeliverAt.set(tkey, now);
  // 顺手防这个表无限增长（uid×kind 组合有界：设备数 × kind 数，但 kind 由端侧
  // 自定义 ⇒ 恶意端可以天天换 kind 灌）。超 10000 条清最旧的一半。
  if (lastDeliverAt.size > 10000) {
    const drop = Math.floor(lastDeliverAt.size / 2);
    let i = 0;
    for (const key of lastDeliverAt.keys()) {
      lastDeliverAt.delete(key);
      if (++i >= drop) break;
    }
  }

  const ev = { id: ++evSeq, at: new Date().toISOString(), kind: k, uid: String(uid || ''), data: JSON.parse(raw) };
  // 缩略图不进历史环（见 THUMB_KIND 注释）；实时 fanout 照旧。
  if (k !== THUMB_KIND) logEvent(code, ev);
  const r = fanoutClass(code, 'class.event', { classCode: code, ...ev });
  return { ok: true, id: ev.id, viewers: r.hits };
}

/**
 * 成员上线（markConnected 之后调）。classCode 取云端表；没绑班的设备静默跳过
 * （不绑班就没有"哪个班"可言，刷一条无班事件只会让前端多一种要处理的情况）。
 */
export function noteMemberOnline(uid) {
  const code = getDeviceClassCode(uid);
  if (!code) return { ok: true, skipped: true };
  const ev = {
    id: ++evSeq, at: new Date().toISOString(), kind: 'member.online',
    uid: String(uid || ''), data: {},
  };
  logEvent(code, ev);
  const r = fanoutClass(code, 'class.member.online', { classCode: code, ...ev });
  return { ok: true, viewers: r.hits };
}

/**
 * 成员离线（markDisconnected / sweepStale 之后调）。reason: 'disconnect' | 'heartbeat-timeout'。
 * 与上线同理：没绑班静默跳过。
 */
export function noteMemberOffline(uid, reason = 'disconnect') {
  const code = getDeviceClassCode(uid);
  if (!code) return { ok: true, skipped: true };
  const ev = {
    id: ++evSeq, at: new Date().toISOString(), kind: 'member.offline',
    uid: String(uid || ''), data: { reason: String(reason || 'disconnect').slice(0, 32) },
  };
  logEvent(code, ev);
  const r = fanoutClass(code, 'class.member.offline', { classCode: code, ...ev });
  return { ok: true, viewers: r.hits };
}

/* ---------- 两侧消息分发（index.js 委托，行数压在这里） ---------- */

/**
 * 管理端班级域消息：subscribe_class / unsubscribe_class。
 * 权限判断（canAccess）在 subscribeClass 内做，与 subscribe/instruction 同口径。
 *
 * @param ctx {{ident:object|null, snd:Function, sndErr:Function}} —— 从 index.js
 *   传进来（snd/sndErr 与 index.js 其余调用点同一实现），避免本模块反向 import
 *   index.js 造成循环依赖。
 */
export function handleViewerClassMsg(ws, p, ctx) {
  const { ident, snd, sndErr } = ctx || {};
  if (p.type === 'subscribe_class') {
    const r = subscribeClass(ws, p.payload?.code, ident);
    if (!r.ok) { sndErr(ws, r.code, r.detail); return; }
    const code = String(p.payload.code).trim();
    const snap = classSnapshot(code, ident);
    snd(ws, 'class-subscribed', { ok: true, ...snap });
    return;
  }
  if (p.type === 'unsubscribe_class') {
    const code = String(p.payload?.code ?? '').trim();
    if (!code) { sndErr(ws, 'BAD_PAYLOAD', 'unsubscribe_class 缺 code'); return; }
    unsubscribeClass(ws, code);
    snd(ws, 'class-unsubscribed', { ok: true, classCode: code });
    return;
  }
  sndErr(ws, 'UNKNOWN_TYPE', p.type);
}

/**
 * 被控端班级域消息：class-event（设备 → 所在班订阅者）。
 * 班级归属只认云端表（getDeviceClassCode），载荷 classCode 一律忽略。
 */
export function handleAgentClassMsg(ws, p, uid, snd) {
  const code = getDeviceClassCode(uid);
  if (!code) {
    // 没绑班 → 拒收并如实回错（不静默丢弃；端侧看到 ok:false 只记日志）
    snd(ws, 'class-event-ack', {
      ok: false, code: 'NO_CLASS', message: '设备未绑定班级，无法上报班级事件',
    });
    return;
  }
  const r = pushClassEvent(code, p.payload?.kind, p.payload?.data, uid);
  if (r.ok) {
    snd(ws, 'class-event-ack', {
      ok: true, id: r.id ?? null, viewers: r.viewers ?? 0,
      ...(r.throttled ? { throttled: true } : {}),
    });
  } else {
    snd(ws, 'class-event-ack', { ok: false, code: r.code, message: r.message });
  }
}
