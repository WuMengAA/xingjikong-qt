// 设备在线表 + 指令队列 + 事件流水。
// 纪律（从 Rust/NSIS 那几步继承下来的三条）：
//   1. 失败必须报出来，不许静默；
//   2. 不许"报了成功其实没做"——设备不在线就是不在线，不许返回 sent；
//   3. 状态只有两种真值：online / offline，中间没有"大概在线"。

import fs from 'node:fs';
import { appendFile } from 'node:fs/promises';
import { EV_LIMIT, KEEP_LAST_FRAME, EVENTS_FILE, HEARTBEAT_TIMEOUT_MS } from './config.js';
import { wrapOutgoing, makeFrame } from './protocol.js';

/** 出向统一走协议层：对端是 v1 就发信封，是旧客户端就发旧扁平格式（过渡期不断链）。 */
function sendTo(ws, type, payload, id = '') {
  return ws.send(wrapOutgoing(ws.__v1 === true, type, payload, id));
}

/** uid -> { uid, version, ws, connectedAt, lastSeen, framesIn, bytesIn, lastError, recentFrame } */
const devices = new Map();

/** uid -> [{ id, action, params, at, state }]  仅设备在线期间保留的下发表 */
const pending = new Map();

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

/** id -> { id, uid, action, result:'done'|'failed', error, at }  设备真实执行回执 */
const results = new Map();

export function pushEvent(level, msg, extra = {}) {
  const ev = { id: ++seq, at: new Date().toISOString(), level, msg, ...extra };
  events.push(ev);
  if (events.length > EV_LIMIT) events.splice(0, events.length - EV_LIMIT);
  // 事件流水落盘：被控端没日志等于瞎子，云端也一样。落盘失败要报出来。
  appendFile(EVENTS_FILE, JSON.stringify(ev) + '\n')
    .then(() => {})
    .catch((e) => console.error(`[cloud] FAIL 事件落盘失败: ${e.message}`));
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
  results.set(id, rec);
  pushEvent(rec.result === 'done' ? 'info' : 'error',
    `设备真实执行回执 action=${action} result=${rec.result}`,
    { uid, id, error: rec.error || undefined });
  return rec;
}

export function getResult(id) {
  return results.get(id) || null;
}

/**
 * 下发指令。在线 → 真的写进 socket，返回 sent；不在线 → 返回 offline（绝不谎报 sent）。
 * @returns {{state:'sent'|'offline', detail?:string}}
 */
export function sendInstruction(uid, action, params) {
  const d = devices.get(uid);
  if (!d || d.ws?.readyState !== 1) {
    const list = pending.get(uid) || [];
    list.push({ id: ++seq, action, params, at: new Date().toISOString(), state: 'offline' });
    pending.set(uid, list);
    return { state: 'offline', detail: '设备不在线，指令未下发（也没写死队列，避免假绿）' };
  }
  // 出向走协议层：v1 对端收信封（ts 在信封上），旧对端保持原有扁平格式（含 at 字段，透传不改）
  const msgId = ++seq;   // 必须先自增：id 要参与回执配对（receipt 带回同一个 id）
  const text = (d.ws.__v1 === true)
    ? wrapOutgoing(true, 'instruction', { action, params }, String(msgId))
    : JSON.stringify({ type: 'instruction', action, params, id: msgId, at: new Date().toISOString() });
  try {
    d.ws.send(text);
    pushEvent('info', '已下发指令', { uid, action });
    return { state: 'sent' };
  } catch (e) {
    // 写 socket 失败 = 没发出去，必须报 error，不能当 sent
    pushEvent('error', '下发失败：写 socket 出错', { uid, action, error: e.message });
    return { state: 'offline', detail: `写 socket 失败：${e.message}` };
  }
}

export function listEvents(limit = 50) {
  return events.slice(-limit).reverse();
}

export function listPending(uid) {
  return pending.get(uid) || [];
}

export function ensureEventsFile() {
  try {
    if (!fs.existsSync(EVENTS_FILE)) fs.writeFileSync(EVENTS_FILE, '');
  } catch (e) {
    console.error(`[cloud] FAIL 无法初始化 ${EVENTS_FILE}: ${e.message}`);
  }
}
