// 设备在线表 + 指令队列 + 事件流水。
// 纪律（从 Rust/NSIS 那几步继承下来的三条）：
//   1. 失败必须报出来，不许静默；
//   2. 不许"报了成功其实没做"——设备不在线就是不在线，不许返回 sent；
//   3. 状态只有两种真值：online / offline，中间没有"大概在线"。

import fs from 'node:fs';
import path from 'node:path';
import { appendFile } from 'node:fs/promises';
import { EV_LIMIT, KEEP_LAST_FRAME, EVENTS_FILE, HEARTBEAT_TIMEOUT_MS,
  EVENTS_MAX_BYTES, EVENTS_KEEP_FILES } from './config.js';
import { wrapOutgoing, makeFrame } from './protocol.js';
import { enqueueDispatch, dropDispatch, settle as settleQueue, pendingFor, listPendingAll, paramsOf } from './store.js';

/** 出向统一走协议层：对端是 v1 就发信封，是旧客户端就发旧扁平格式（过渡期不断链）。 */
function sendTo(ws, type, payload, id = '') {
  return ws.send(wrapOutgoing(ws.__v1 === true, type, payload, id));
}

/** uid -> { uid, version, ws, connectedAt, lastSeen, framesIn, bytesIn, lastError, recentFrame } */
const devices = new Map();

/* 说明：指令队列已改为**落盘**（见 store.js）。
 * 原先这里有个内存态 `pending` Map，但云端一重启就没了、设备重连也不回放——那正是本功能要修的洞。
 * 现在"已下发未回执"的指令一律进 SQLite，重启不丢、设备重连按原顺序补发。 */

/**
 * viewer ws -> { subs:Set<uid>, auto:boolean }
 *   subs = 这个管理端在看哪几台
 *   auto = true 表示「还没让管理端显式挑过」，设备上线时自动补订阅
 * 为什么要有 auto：管理端常常比教室机先开机（老师先打开管理软件，教室机后启动），
 * 只在「连上时就订阅第一台」会漏掉这种情况 → 管理端永远黑屏。
 * 只推订阅的，不群发给所有人。
 */
const viewers = new Map();

const events = [];
let seq = 0;

/* 已落盘字节数 —— **必须自己记，不能每次 statSync 现读**。
 * appendFile 是异步的：广播那种一瞬间连着来几十条事件的场景里，同步循环跑完时
 * 磁盘上的文件还几乎是空的，statSync 每次都读到旧大小，轮转判断永远不成立
 * （2026-10-06 实测：钉 4096 字节上限、连推 301 条，文件长到 38KB 一次都没转）。
 * 自己累加不受异步滞后影响。 */
let eventsBytes = 0;

/** id -> { id, uid, action, result:'done'|'failed', error, at }  设备真实执行回执 */
const results = new Map();
/* 回执只保留一个滚动窗口（2026-10-06 占用优化）。
 * 这张表以前只 set 不删：每条指令回执永久留在内存里，教室机一天下发几千条就是
 * 几千个永不释放的对象，跑一学期内存只涨不落。
 * 现在超限先删最老的（Map 保持插入序，第一个 key 就是最老的那条）。
 * 不会有"刚到的回执被挤掉"的问题：窗口 5000 条，比一个教学日下发量还宽，
 * 真要用早先从这台机器上查过（真查走 SQLite 的 /api/instructions/notice）。 */
const RESULT_KEEP = Number(process.env.CLOUD_RESULT_KEEP || 5000);
function pruneResults() {
  if (results.size <= RESULT_KEEP) return;
  const drop = results.size - RESULT_KEEP;
  let i = 0;
  for (const k of results.keys()) {
    if (i++ >= drop) break;
    results.delete(k);
  }
}

/**
 * 事件日志体积到顶就归档：当前 events.log → events.log.<ISO时间戳>，然后重建一个空的。
 * 归档按文件名倒序只留最近 EVENTS_KEEP_FILES 个，更老的删掉 —— 留痕还是留，
 * 但不能让一个日志文件陪着教室机一起把盘吃干。
 *
 * 只在体积确实到顶时才会动 rename，平时花的是一次 statSync（十来字节的 syscall）。
 * 失败必须报出来（本文件头第 1 条纪律），但**不许中断这一次 pushEvent**：
 * 归档出问题不该让设备状态更新跟着失败。
 */
function rotateEventsLogIfNeeded(sizeBytes) {
  if (!(sizeBytes >= EVENTS_MAX_BYTES) || EVENTS_MAX_BYTES <= 0) return;
  try {
    const stamp = new Date().toISOString().replace(/[-:.]/g, '-');
    const archived = `${EVENTS_FILE}.${stamp}`;
    fs.renameSync(EVENTS_FILE, archived);
    fs.writeFileSync(EVENTS_FILE, '');
    eventsBytes = 0;

    const dir = path.dirname(EVENTS_FILE);
    const base = path.basename(EVENTS_FILE);
    let olds = [];
    try {
      olds = fs.readdirSync(dir)
        .filter((f) => f.startsWith(base + '.') && f !== archived)
        .sort()
        .reverse()
        // 归档名是 ISO 时间戳，字典序 == 时间序，直接截断就行
        .slice(EVENTS_KEEP_FILES);
    } catch { olds = []; }
    for (const f of olds) {
      try { fs.unlinkSync(path.join(dir, f)); } catch { /* 删不掉也要把轮转本身报完 */ }
    }
    console.log(`[cloud] 事件日志已轮转：${sizeBytes} 字节 → ${archived}，删除旧归档 ${olds.length} 个`);
  } catch (e) {
    console.error(`[cloud] FAIL 事件日志轮转失败（本次落盘照常进行）：${e.message}`);
  }
}

/**
 * 事件落盘走一条 Promise 链，一次只写一条。
 *
 * 为什么串行：appendFile 本来是并行的，但轮转要 rename 当前文件 ——
 * 2026-10-06 实测，轮转那一瞬间还有几次异步写在飞，rename 之后它们写进了**归档文件**，
 * 结果当前 events.log 只剩 31 行，最近一大段事全跑到归档里去了，主日志看着像断过。
 * 串成一条链，检查与写之间不会插进别的写，轮转永远发生在两次写之间。
 * 顺带还压掉了"广播风暴时几百个 appendFile 一起飞"这件事。
 *
 * 前一条写失败不影响后一条（then 的第二参数就是 run），失败照样在链上往下走。
 */
let eventsChain = Promise.resolve();
function writeEventLine(line) {
  const run = async () => {
    rotateEventsLogIfNeeded(eventsBytes);
    eventsBytes += Buffer.byteLength(line);
    await appendFile(EVENTS_FILE, line);
  };
  eventsChain = eventsChain.then(run, run);
  eventsChain.catch(() => {});   // 失败由下面的 catch 报，别在链上堆 unhandled rejection
  return eventsChain;
}

export function pushEvent(level, msg, extra = {}) {
  const ev = { id: ++seq, at: new Date().toISOString(), level, msg, ...extra };
  events.push(ev);
  if (events.length > EV_LIMIT) events.splice(0, events.length - EV_LIMIT);
  // 事件流水落盘：被控端没日志等于瞎子，云端也一样。落盘失败要报出来。
  const logLine = JSON.stringify(ev) + '\n';
  writeEventLine(logLine).catch((e) => console.error(`[cloud] FAIL 事件落盘失败: ${e.message}`));
  const line = `[cloud] ${ev.level.toUpperCase()} ${ev.msg}${ev.uid ? ' uid=' + ev.uid : ''}`;
  if (ev.level === 'error') console.error(line);
  else console.log(line);
  return ev;
}

export function markConnected(uid, version, ws) {
  const old = devices.get(uid);
  if (old && old.ws && old.ws !== ws) {
    pushEvent('warn', '同 uid 重连，踢掉旧连接', { uid, reason: 'duplicate' });
    try { old.ws.close(4001, 'superseded'); } catch { /* 已断就算了，别静默吞掉别的 */ }
  }
  const now = Date.now();
  devices.set(uid, {
    uid,
    version: version || 'unknown',
    ws,
    connectedAt: now,
    lastSeen: now,
    framesIn: 0,
    bytesIn: 0,
    heartbeatsIn: 0,
    lastError: null,
    recentFrame: old?.recentFrame ?? null,
  });
  pushEvent('info', '设备已注册并连上', { uid, version });
}

export function markDisconnected(ws) {
  for (const [uid, d] of devices) {
    if (d.ws === ws) {
      devices.delete(uid);
      pushEvent('info', '设备掉线', { uid, frames: d.framesIn });
      return uid;
    }
  }
  return null;
}

export function addFrame(uid, bytes) {
  const d = devices.get(uid);
  if (!d) return null;
  d.framesIn++;
  d.bytesIn += bytes;
  d.lastSeen = Date.now();
  if (!KEEP_LAST_FRAME) d.recentFrame = null;
  return d;
}

export function setRecentFrame(uid, buf) {
  const d = devices.get(uid);
  if (d) d.recentFrame = buf;
}

/**
 * 被控端心跳：刷新 lastSeen 并计数。
 * 心跳必须与帧解耦——将来上「按需拉流」省带宽后，没人看的机器根本不推帧，
 * 那时帧就不能当存活依据了，必须有一条独立的存活信号。
 * @returns {boolean} false = 这个 uid 不在在线表里（异常）
 */
export function touchHeartbeat(uid) {
  const d = devices.get(uid);
  if (!d) return false;
  d.lastSeen = Date.now();
  d.heartbeatsIn++;
  return true;
}

/**
 * 清扫僵死设备：超过 HEARTBEAT_TIMEOUT_MS 既没心跳也没帧 → 判定离线并踢掉连接。
 * 不扫的后果：教室机掉电/断网但 TCP 没干净关闭时，面板会一直显示"在线"，
 * 老师点了指令才发现没反应——那比显示离线更糟（假绿）。
 * @returns {string[]} 本次被判定离线的 uid 列表
 */
export function sweepStale() {
  const now = Date.now();
  const stale = [];
  for (const [uid, d] of devices) {
    if (now - d.lastSeen <= HEARTBEAT_TIMEOUT_MS) continue;
    stale.push(uid);
    pushEvent('warn', '心跳超时，判定离线并踢掉', {
      uid,
      lastSeenAgoSec: Math.round((now - d.lastSeen) / 1000),
      frames: d.framesIn,
      heartbeats: d.heartbeatsIn,
    });
    try { if (d.ws) d.ws.close(4000, 'heartbeat timeout'); } catch { /* 已经断了，不必再报 */ }
    devices.delete(uid);
  }
  return stale;
}

export function listDevices() {
  const now = Date.now();
  return [...devices.values()].map((d) => ({
    uid: d.uid,
    version: d.version,
    online: true,
    connectedAt: new Date(d.connectedAt).toISOString(),
    lastSeenAgoSec: Math.round((now - d.lastSeen) / 1000),
    framesIn: d.framesIn,
    bytesIn: d.bytesIn,
    heartbeatsIn: d.heartbeatsIn,
    lastError: d.lastError,
  }));
}

export function getDevice(uid) {
  return devices.get(uid) || null;
}

/* ---------- 管理端（viewer）：订阅帧 + 收设备表 ---------- */

export function addViewer(ws) {
  viewers.set(ws, { subs: new Set(), auto: true });
  return viewers.size;
}

export function removeViewer(ws) {
  viewers.delete(ws);
}

export function viewerCount() {
  return viewers.size;
}

/** 订阅某设备的帧。返回 false = 这个.ws 不是已注册的 viewer（连接早断了）。 */
export function subscribeViewer(ws, uid) {
  const v = viewers.get(ws);
  if (!v) return false;
  v.subs.add(String(uid || '').trim());
  return true;
}

/** 退订某设备的帧（按需拉流预留：没人看了就别推）。返回 false = 这个 ws 不是已注册的 viewer。 */
export function unsubscribeViewer(ws, uid) {
  const v = viewers.get(ws);
  if (!v) return false;
  return v.subs.delete(String(uid || '').trim());
}

/** 管理端显式挑了某台之后，就不再自动给它补订阅别的设备了。 */
export function pinViewer(ws) {
  const v = viewers.get(ws);
  if (v) v.auto = false;
}

/** 设备上线时：所有还没挑设备的管理端，自动补上这个 uid 并收到 subscribed 通知。 */
export function autoSubscribeViewers(uid) {
  const key = String(uid || '');
  let n = 0;
  for (const [ws, v] of viewers) {
    if (!v.auto) continue;
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    v.subs.add(key);
    n++;
    try { sendTo(ws, 'subscribed', { uid: key, auto: true }); }
    catch (e) { pushEvent('error', '给管理端补订阅通知失败', { uid: key, error: e.message }); viewers.delete(ws); }
    // 设备上线后第一次有人看 -> 让设备把 WebRTC 推流起起来（没人看就不推，不空耗 CPU）
    rtcSignalToAgent(key, 'rtc-start', { uid: key, auto: true });
  }
  return n;
}

/**
 * 把一帧推给所有订阅了该 uid 的 viewer。推不出去就报出来，绝不假装推成功。
 * v1 对端收**二进制帧**（带 seq/ts/mime，省掉 base64 的 33% 开销），旧对端仍收 JSON base64。
 * @returns {{hits:number, bytes:number}}
 */
export function broadcastFrame(uid, buf, frameSeq = 0) {
  let hits = 0;
  for (const [ws, v] of viewers) {
    if (!v.subs.has(String(uid))) continue;
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    try {
      if (ws.__v1 === true) ws.send(makeFrame(uid, frameSeq, 'image/jpeg', buf));
      else ws.send(JSON.stringify({
        type: 'frame', uid: String(uid), ts: new Date().toISOString(),
        bytes: buf.length, data: buf.toString('base64'),
      }));
      hits++;
    } catch (e) {
      pushEvent('error', '推帧给管理端失败：写 socket 出错', { uid, error: e.message });
      viewers.delete(ws);
    }
  }
  return { hits, bytes: buf.length };
}

/** 设备表广播（管理端每几秒刷一次列表用）。 */
export function broadcastDevices() {
  const devices = listDevices();
  let hits = 0;
  for (const [ws] of viewers) {
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    try { sendTo(ws, 'devices', { devices, at: new Date().toISOString() }); hits++; }
    catch { viewers.delete(ws); }
  }
  return hits;
}

/** 把任意消息广播给所有在线管理端（指令回执、执行结果等都走它）。返回命中数。 */
export function broadcastToViewers(type, payload) {
  let hits = 0;
  for (const [ws] of viewers) {
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    try { sendTo(ws, type, payload); hits++; }
    catch { viewers.delete(ws); }
  }
  return hits;
}

/**
 * 记录设备真实执行回执（done/failed）。失败必须如实记下，不许静默。
 * @param data 动作的附加数据（进程列表 / 日志行 / 音量实际值 / 截图路径…），可空。
 *             协议规范第十节：可选字段，客户端不认就忽略，不算错。
 * @returns {{id, uid, action, result, error, data, at}}
 */
export function recordResult(id, uid, action, result, error, data) {
  const rec = {
    id,
    uid,
    action,
    result: (result === 'done') ? 'done' : 'failed',
    error: error || null,
    data: (data && typeof data === 'object') ? data : null,
    at: new Date().toISOString(),
  };
  // 2026-10-04：把下发时携带的 notice_id 带进回执，站点广播才能把"已执行"对账回通知表。
  // paramsOf 查这条指令的原始 params（云端表完整保留），无则留空。
  try {
    const src = paramsOf(id);
    if (src && src.params && typeof src.params === 'object') {
      const nid = src.params.notice_id ?? src.params.params?.notice_id;
      if (nid !== undefined && nid !== null) rec.notice_id = nid;
    }
  } catch { /* 查不到就不带，不影响回执本身 */ }
  results.set(id, rec);
  pruneResults();
  // 回执到达 → 队列里这条"已下发未回执"标完成，之后重连不再补发。
  // 不在队列（已过期、或落盘降级）也不报错：回执本身仍如实转给管理端。
  settleQueue(id, rec.result, rec.error);
  pushEvent(rec.result === 'done' ? 'info' : 'error',
    `设备真实执行回执 action=${action} result=${rec.result}`,
    { uid, id, error: rec.error || undefined, ...(rec.notice_id !== undefined ? { notice_id: rec.notice_id } : {}) });
  return rec;
}

export function getResult(id) {
  return results.get(id) || null;
}

/**
 * 下发指令。在线 → 先落盘拿到稳定 id，再真的写进 socket，返回 sent；
 * 不在线 → 返回 offline（绝不谎报 sent，**也不入补发队列**——离线不能当收下）。
 * @returns {{state:'sent'|'offline', id?:number, detail?:string}}
 */
export function sendInstruction(uid, action, params) {
  const d = devices.get(uid);
  if (!d || d.ws?.readyState !== 1) {
    // 关键：离线只报离线。若把离线指令也塞进队列，重连时就会补发出去 = 变相把"离线"当成了"收下"。
    return { state: 'offline', detail: '设备不在线，指令未下发（也不入补发队列，避免假绿）' };
  }
  // 先落盘再发：要拿到重启后仍单调不重号的 id（回执按它配对、重连按它排序补发）。
  // 落盘失败（降级为 null）时退回进程内自增 id，照常下发，只是这条没有"重启不丢"的兜底。
  const id = enqueueDispatch(uid, action, params);
  const msgId = id ?? ++seq;
  // 出向走协议层：v1 对端收信封（ts 在信封上），旧对端保持原有扁平格式（含 at 字段，透传不改）
  const text = (d.ws.__v1 === true)
    ? wrapOutgoing(true, 'instruction', { action, params }, String(msgId))
    : JSON.stringify({ type: 'instruction', action, params, id: msgId, at: new Date().toISOString() });
  try {
    d.ws.send(text);
    pushEvent('info', '已下发指令', { uid, action, id: msgId });
    return { state: 'sent', id: msgId };
  } catch (e) {
    // 写 socket 失败 = 没发出去：把刚落的那条记录撤掉（别留成"等回执"），并报 error，不能当 sent
    dropDispatch(id);
    pushEvent('error', '下发失败：写 socket 出错', { uid, action, error: e.message });
    return { state: 'offline', detail: `写 socket 失败：${e.message}` };
  }
}

/* ---------- WebRTC 信令中继（P1-A 实时画面）----------
 * 三进程链路：被控端(agent) -> 云端按 uid 转发 -> 管理端(viewer)，反之亦然。
 * 云端**只转不发**：不解析 sdp / candidate 内容，媒体流（RTP）不经过云端 —— 云端一旦挂，画面立刻断，
 *   这是刻意的：云端只当信令中间人，媒体走点对点，教室机出公网也不必让云端扛带宽。
 * 三条纪律（沿用本文件开头那三条）：
 *   ① 转不出去必须报出来（不许静默）；② 转了 0 个对端必须报出来（"发出去了" != "有人收"）；
 *   ③ 只投给订阅了这个 uid 的那一端，不群发。
 * 谁建 offer：被控端建 —— 它一直抓着屏，教室机无人值守，不能等有人去点按钮。
 */

/** 云端 -> 设备的 WebRTC 开关信号（订阅开始/停止），与管理端 subscribe/unsubscribe 一一对应。
 *  有人在看了设备才起推流，没人看就停：免得在没人看的机器上空耗 CPU。
 *  @returns {{ok:boolean, to:number, detail?:string}} */
export function rtcSignalToAgent(uid, type, payload = {}) {
  const key = String(uid || '').trim();
  const d = devices.get(key);
  if (!d || !d.ws || d.ws.readyState !== 1) {
    // 这条很常见（管理端先订阅、设备后上线）：不判成错误，但必须记一笔，不许假装发过了
    pushEvent('warn', 'WebRTC 开关信号没送到设备（这台此刻不在线）', { uid: key, type });
    return { ok: false, to: 0, detail: '设备不在线' };
  }
  try {
    sendTo(d.ws, type, { ...payload, uid: key, from: 'cloud' });
    return { ok: true, to: 1 };
  } catch (e) {
    pushEvent('error', 'WebRTC 开关信号写给设备失败', { uid: key, type, error: e.message });
    return { ok: false, to: 0, detail: e.message };
  }
}

/** 设备 -> 管理端的 WebRTC 信令（offer/answer/ice）转发，只投给订阅了这个 uid 的管理端。
 *  @returns {{ok:boolean, to:number, detail?:string}} */
export function rtcRelayToViewers(uid, type, payload = {}) {
  const key = String(uid || '').trim();
  // 防回声：发信人就是本端（管理端）自己的消息，不转 —— 否则 offer/answer 会在两端之间无限对传
  if (payload.from === 'viewer') {
    pushEvent('warn', '丢弃同源 WebRTC 信令（防回声）', { uid: key, type, from: payload.from });
    return { ok: false, to: 0, detail: '同源信令，不回传' };
  }
  let hits = 0;
  for (const [ws, v] of viewers) {
    if (!v.subs.has(key)) continue;
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    try {
      // 转发时**补上 uid**：对端有时只带 sdp/candidate 不带 uid，但它必须知道这是哪台机器
      sendTo(ws, type, { ...payload, uid: key, from: 'agent' });
      hits++;
    } catch (e) {
      pushEvent('error', 'WebRTC 信令写给管理端失败：写 socket 出错', { uid: key, type, error: e.message });
      viewers.delete(ws);
    }
  }
  if (hits === 0) pushEvent('warn', 'WebRTC 信令发出去了但没人订阅这台设备', { uid: key, type });
  return { ok: hits > 0, to: hits };
}

/** 管理端 -> 设备的 WebRTC 信令（offer/answer/ice）转发。 */
export function rtcRelayToAgent(uid, type, payload = {}) {
  const key = String(uid || '').trim();
  // 防回声：发信人就是本端（设备）自己的消息，不转
  if (payload.from === 'agent') {
    pushEvent('warn', '丢弃同源 WebRTC 信令（防回声）', { uid: key, type, from: payload.from });
    return { ok: false, to: 0, detail: '同源信令，不回传' };
  }
  const d = devices.get(key);
  if (!d || !d.ws || d.ws.readyState !== 1) {
    pushEvent('warn', 'WebRTC 信令发不出去：设备不在线', { uid: key, type });
    return { ok: false, to: 0, detail: '设备不在线' };
  }
  try {
    sendTo(d.ws, type, { ...payload, uid: key, from: 'viewer' });
    return { ok: true, to: 1 };
  } catch (e) {
    pushEvent('error', 'WebRTC 信令写给设备失败', { uid: key, type, error: e.message });
    return { ok: false, to: 0, detail: e.message };
  }
}

export function listEvents(limit = 50) {
  return events.slice(-limit).reverse();
}

/** 某设备"已下发未回执"的指令（落盘队列），按原下发顺序。无 uid → 全部按 uid 分组。 */
export function listPending(uid) {
  return uid ? pendingFor(uid) : listPendingAll();
}

export function ensureEventsFile() {
  try {
    eventsBytes = fs.existsSync(EVENTS_FILE) ? fs.statSync(EVENTS_FILE).size : 0;
    if (!fs.existsSync(EVENTS_FILE)) fs.writeFileSync(EVENTS_FILE, '');
  } catch (e) {
    console.error(`[cloud] FAIL 无法初始化 ${EVENTS_FILE}: ${e.message}`);
  }
}
