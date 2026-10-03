// 星集控云端（被控端最小可用后端）
//   ws://host:8788/ws/agent   被控端接入（先 register，token 校验，之后推帧）
//   ws://host:8788/ws/viewer  管理端接入（D4：看画面 + 刷设备表 + 真下发指令）
//   GET  /api/devices         设备在线表
//   POST /api/instructions    下发指令（设备不在线返回 409，绝不谎报 sent）
//   GET  /api/events          事件流水（最近 50 条）
//   GET  /api/frame?uid=...   最近一帧（管理端 D4 用；内存里，不落盘）

import http from 'node:http';
import { WebSocketServer } from 'ws';
import { PORT, DEV_TOKEN, DEV_TOKEN_IS_DEFAULT, HANDSHAKE_TIMEOUT_MS, SWEEP_INTERVAL_MS,
  VIEWER_TOKEN, VIEWER_TOKEN_IS_DEFAULT, VIEWER_SECRET,
  HEARTBEAT_TIMEOUT_MS, HEARTBEAT_INTERVAL_MS, PROTOCOL_MODE } from './config.js';
import { verifyViewerTicket } from './ticket.js';
import { parseIncoming, parseFrame, wrapOutgoing, errPayload, ERR, PROTOCOL_VERSION } from './protocol.js';

/** 发一条协议消息（自动按对端版本选格式），失败不静默。 */
function snd(ws, type, payload, id = '') {
  try { ws.send(wrapOutgoing(ws.__v1 === true, type, payload, id)); } catch (e) {
    pushEvent('error', '给对端发消息失败', { type, error: e.message });
  }
}

/** 发一条协议错误；code 里标了要关连接的，发完就关（并写明原因，不许静默断）。 */
function sndErr(ws, code, detail) {
  const p = errPayload(code, detail);
  snd(ws, 'error', p);                        // v1：统一走 error 通道
  if (ws.__v1 !== true) snd(ws, p.code, p);   // 旧客户端：额外补一条它认得的类型，避免它看不懂 error
  const e = ERR[code];
  if (e?.close) { try { ws.close(e.close, code); } catch { /* 已断就算了 */ } }
  pushEvent('warn', `协议错误 ${code}`, { detail: detail || e?.msg || '' });
}
import {
  pushEvent, markConnected, markDisconnected, addFrame, setRecentFrame,
  listDevices, getDevice, sendInstruction, listEvents, listPending, ensureEventsFile,
  addViewer, removeViewer, subscribeViewer, pinViewer, autoSubscribeViewers,
  broadcastFrame, broadcastDevices, broadcastToViewers, recordResult, getResult, viewerCount,
  touchHeartbeat, sweepStale,
} from './registry.js';

ensureEventsFile();

if (DEV_TOKEN_IS_DEFAULT) {
  console.warn(`[cloud] ⚠ 正在使用内置开发令牌 "${DEV_TOKEN}"，生产环境请设环境变量 CLOUD_WS_TOKEN`);
}
if (VIEWER_TOKEN_IS_DEFAULT) {
  console.warn('[cloud] ⚠ 管理端令牌未单独配置（CLOUD_VIEWER_TOKEN），正回落到内置开发令牌；生产环境必须设');
}
if (!VIEWER_SECRET) {
  console.warn('[cloud] ⚠ 未配置 CLOUD_VIEWER_SECRET：浏览器票据通道关闭（桌面端长期令牌通道仍可用）');
}
console.log(`[cloud] 云端监听 http://127.0.0.1:${PORT} （被控端路径 /ws/agent，协议模式 ${PROTOCOL_MODE}）`);

const server = http.createServer((req, res) => {
  const u = new URL(req.url, `http://127.0.0.1:${PORT}`);
  const send = (code, obj) => {
    const body = JSON.stringify(obj);
    res.writeHead(code, { 'content-type': 'application/json; charset=utf-8', 'content-length': Buffer.byteLength(body) });
    res.end(body);
  };

  if (req.method === 'GET' && u.pathname === '/api/devices') {
    return send(200, { ok: true, count: listDevices().length, devices: listDevices() });
  }

  if (req.method === 'GET' && u.pathname === '/api/events') {
    return send(200, { ok: true, events: listEvents(Number(u.searchParams.get('limit') || 50)) });
  }

  if (req.method === 'GET' && u.pathname === '/api/instructions/pending') {
    const uid = u.searchParams.get('uid');
    return send(200, { ok: true, pending: uid ? listPending(uid) : Object.fromEntries(listPending(uid ?? '')) });
  }

  if (req.method === 'GET' && u.pathname === '/api/frame') {
    const uid = u.searchParams.get('uid');
    const d = uid ? getDevice(uid) : null;
    if (!d || !d.recentFrame) return send(404, { ok: false, error: '该设备没有可取的帧（可能没推过或已掉线）' });
    res.writeHead(200, { 'content-type': 'image/jpeg', 'content-length': d.recentFrame.length });
    return res.end(d.recentFrame);
  }

  if (req.method === 'POST' && u.pathname === '/api/instructions') {
    let body = '';
    req.on('data', (c) => {
      body += c;
      if (body.length > 1e6) { req.destroy(); } // 防失控上报
    });
    req.on('end', () => {
      let payload;
      try {
        payload = JSON.parse(body || '{}');
      } catch (e) {
        return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` });
      }
      const { uid, action, params } = payload;
      if (!uid || !action) return send(400, { ok: false, error: '缺 uid 或 action' });
      const r = sendInstruction(uid, String(action), params ?? {});
      if (r.state === 'sent') return send(200, { ok: true, state: 'sent', uid, action });
      return send(409, { ok: false, state: r.state, uid, action, detail: r.detail });
    });
    return undefined;
  }

  return send(404, { ok: false, error: `未知道路 ${req.method} ${u.pathname}` });
});

const wss = new WebSocketServer({ noServer: true });        // 被控端接入
const viewerWss = new WebSocketServer({ noServer: true });  // 管理端（看的人）

server.on('upgrade', (req, socket, head) => {
  const u = new URL(req.url, 'http://127.0.0.1');
  if (u.pathname === '/ws/agent') {
    wss.handleUpgrade(req, socket, head, (ws) => wss.emit('connection', ws, req));
    return;
  }
  if (u.pathname === '/ws/viewer') {
    viewerWss.handleUpgrade(req, socket, head, (ws) => viewerWss.emit('connection', ws, req));
    return;
  }
  pushEvent('warn', '拒绝了非 agent/viewer 路径的 WS 升级', { path: u.pathname });
  socket.write('HTTP/1.1 400 Bad Request\r\n\r\n');
  socket.destroy();
});

/* ---------- 管理端（viewer）：只看 + 真下发 ---------- */
/**
 * 管理端鉴权：长期令牌 或 站点签发的短票据，任一通过即可。
 * 令牌给桌面端（Qt viewer，守得住密钥）；票据给浏览器（票只有 2 分钟，抄走也很快失效）。
 * @returns {{ok:boolean, mode?:'token'|'ticket', uid?:string|null, reason?:string}}
 */
function authorizeViewer(msg) {
  const token = typeof msg.token === 'string' ? msg.token.trim() : '';
  if (token) {
    if (token === VIEWER_TOKEN) return { ok: true, mode: 'token', uid: null };
    return { ok: false, reason: '管理端令牌不对' };
  }
  const ticket = typeof msg.ticket === 'string' ? msg.ticket.trim() : '';
  if (ticket) return verifyViewerTicket(ticket);
  return { ok: false, reason: 'auth 消息里既没有 token 也没有 ticket' };
}

/** 管理端通过鉴权之后才做的事：登记 + 补订阅 + 推初始设备表。 */
function onViewerAuthed(ws, who) {
  addViewer(ws);
  pushEvent('info', '管理端已接入（已鉴权）', { viewers: viewerCount(), mode: who.mode, uid: who.uid || undefined });

  // 两条补订阅路径都要有，缺一条就黑屏：
  //   ① 管理端后到（教室机早就在线）→ 这里立刻订阅第一台；
  //   ② 教室机后到（管理端先开机）  → autoSubscribeViewers 在设备上线时补（见 markConnected 之后那步）。
  const firstOnline = listDevices()[0]?.uid;
  if (firstOnline) {
    if (subscribeViewer(ws, firstOnline)) snd(ws, 'subscribed', { uid: firstOnline, auto: true });
  }
  snd(ws, 'viewer-ready', { viewers: viewerCount() });
  snd(ws, 'devices', { devices: listDevices(), at: new Date().toISOString() });
}

viewerWss.on('connection', (ws) => {
  // 关键：鉴权通过前绝不 addViewer()。否则设备表和画面帧会被推给没通过鉴权的连接，
  // 加了 auth 也等于没加——这是最容易写歪的一处。
  let authed = false;
  const deadline = setTimeout(() => {
    if (authed) return;
    pushEvent('warn', '管理端握手超时：连上后没在限定时间内鉴权，已踢掉');
    try { ws.close(4002, 'viewer handshake timeout'); } catch { /* 已断就算了 */ }
  }, HANDSHAKE_TIMEOUT_MS);

  ws.on('message', (data, isBinary) => {
    if (isBinary) {
      pushEvent('warn', '管理端发了二进制消息（管理端只该发文本）', { bytes: data.length });
      return;
    }
    // 入向统一走协议层：v1 信封与旧扁平格式都规范化成同一种内部表示
    const p = parseIncoming(data.toString('utf8'));
    if (!p.ok) { sndErr(ws, p.code, p.detail); return; }
    if (!p.legacy) ws.__v1 = true;   // 对端说 v1 → 之后回它 v1 信封

    if (!authed) {
      if (p.type !== 'auth') {
        pushEvent('warn', '管理端未鉴权就发消息，已拒绝', { type: String(p.type).slice(0, 32) });
        if (ws.__v1 === true) sndErr(ws, 'AUTH_REQUIRED', `先发 auth，收到的是 ${p.type}`);
        else { snd(ws, 'auth-fail', { reason: '请先发 {"type":"auth","token":"…"} 或带 ticket' }); try { ws.close(4003, 'auth required'); } catch {} }
        return;
      }
      const r = authorizeViewer(p.payload);
      if (!r.ok) {
        // 鉴权失败必须报出来（不许静默断掉让人以为"网络问题"）
        pushEvent('warn', '管理端鉴权失败，连接已拒绝', { reason: r.reason });
        if (ws.__v1 === true) sndErr(ws, 'AUTH_FAILED', r.reason);
        else { snd(ws, 'auth-fail', { reason: r.reason }); try { ws.close(4006, 'viewer auth failed'); } catch {} }
        return;
      }
      authed = true;
      clearTimeout(deadline);
      // 旧客户端认的是 auth-ok，v1 客户端收 authed（统一命名，旧名进兼容清单）
      snd(ws, ws.__v1 === true ? 'authed' : 'auth-ok', { mode: r.mode, uid: r.uid ?? null });
      onViewerAuthed(ws, r);
      return;
    }

    switch (p.type) {
      case 'subscribe': {
        pinViewer(ws); // 管理端自己挑了 → 以后不再自动给它补别的设备
        const uid = p.payload.uid;
        const ok = subscribeViewer(ws, uid);
        snd(ws, 'subscribed', { uid: ok ? uid : null, ok });
        return;
      }
      case 'unsubscribe': {
        // 按需拉流预留：不再看这台就退订，云端将来可据此让设备停推
        const ok = unsubscribeViewer(ws, p.payload.uid);
        snd(ws, 'unsubscribed', { uid: ok ? p.payload.uid : null, ok });
        return;
      }
      case 'devices':
        snd(ws, 'devices', { devices: listDevices(), at: new Date().toISOString() });
        return;
      case 'instruction': {
        const { uid, action, params } = p.payload;
        if (!uid || !action) {
          sndErr(ws, 'BAD_PAYLOAD', 'instruction 缺 uid 或 action');
          return;
        }
        const r = sendInstruction(uid, String(action), params ?? {});
        // 云端只说它发送成功与否；到没到机器、有没有真做，是设备回执的事
        snd(ws, 'instruction-result', {
          ok: r.state === 'sent', state: r.state, uid, action,
          detail: r.detail || '云端已写入被控端连接',
        });
        return;
      }
      default:
        // 未知类型**不关闭连接**：允许新客户端试探新能力（协议规范第十节）
        sndErr(ws, 'UNKNOWN_TYPE', p.type);
        return;
    }
  });

  ws.on('close', () => {
    clearTimeout(deadline);
    removeViewer(ws);
    if (authed) pushEvent('info', '管理端断开', { viewers: viewerCount() });
  });
  ws.on('error', (e) => { pushEvent('error', '管理端连接出错', { error: e.message }); });
});

// 管理端刷新设备表：有 viewer 在才推，没人看就不空转
setInterval(() => {
  if (viewerCount() > 0) {
    const n = broadcastDevices();
    if (n === 0) console.log('[cloud] 管理端刷新：推了设备表但没收到回执（连接已失效）');
  }
}, 2000);

// 离线清扫：机器掉电/拔网线时 TCP 不会立刻关，只等 close 事件会出现"永远在线"的假绿。
// 每 SWEEP_INTERVAL_MS 扫一次，把超过 HEARTBEAT_TIMEOUT_MS 没心跳也没帧的设备判离线。
setInterval(() => {
  try {
    const stale = sweepStale();
    if (stale.length > 0) broadcastDevices();   // 立刻把"它没了"推给管理端，别等下一次轮询
  } catch (e) {
    pushEvent('error', '离线清扫异常', { error: e.message });
  }
}, SWEEP_INTERVAL_MS);

wss.on('connection', (ws) => {
  let registered = false;
  let uid = null;
  const deadline = setTimeout(() => {
    if (!registered) {
      pushEvent('error', '握手超时：连上后没发 register，已踢掉', { uid: uid || '(未知)' });
      ws.close(4002, 'handshake timeout');
    }
  }, HANDSHAKE_TIMEOUT_MS);

  const finish = () => { clearTimeout(deadline); };

  ws.on('message', (data, isBinary) => {
    if (!registered) {
      // 握手：只认 register（v1 信封或旧扁平格式都行），不是就明确拒绝，不重置超时
      const p = parseIncoming(data.toString('utf8'));
      if (!p.ok) { finish(); sndErr(ws, p.code, p.detail); return; }
      if (!p.legacy) ws.__v1 = true;
      if (p.type !== 'register') {
        finish();
        sndErr(ws, 'AUTH_REQUIRED', `握手期只认 register，收到的是 ${p.type}`);
        return;
      }
      const { uid: ruid, token, version } = p.payload;
      if (token !== DEV_TOKEN) {
        finish();
        pushEvent('error', '设备令牌校验失败，连接已拒绝', { uid: String(ruid ?? '') });
        sndErr(ws, 'BAD_TOKEN', '设备令牌与云端 CLOUD_WS_TOKEN 不一致');
        return;
      }
      uid = String(ruid || '').trim();
      if (!uid) {
        finish();
        pushEvent('error', 'register 没带 uid，拒绝');
        sndErr(ws, 'BAD_PAYLOAD', 'register 缺 uid');
        return;
      }
      registered = true;
      finish();
      markConnected(uid, String(version || ''), ws);
      // 设备上线：把还没挑设备的管理端补上订阅（否则老师先开软件 → 永远黑屏）
      const auto = autoSubscribeViewers(uid);
      if (auto > 0) pushEvent('info', '已自动给管理端补上订阅', { uid, viewers: auto });
      // 回执：v1 收 registered（顺带**下发心跳参数**，不再靠两端各猜一个数字）；旧客户端仍收 registration-ok
      snd(ws, ws.__v1 === true ? 'registered' : 'registration-ok', {
        uid,
        server: 'stelarith-cloud-ws/0.4.0',
        heartbeatMs: HEARTBEAT_INTERVAL_MS,
        timeoutMs: HEARTBEAT_TIMEOUT_MS,
      });
      return;
    }

    if (isBinary) {
      const f = parseFrame(Buffer.from(data));
      if (!f.ok) { sndErr(ws, f.code, f.detail); return; }
      const seq = f.header?.seq ?? 0;
      const info = addFrame(uid, f.payload.length);
      if (!info) {
        pushEvent('error', '收到帧但设备不在表里（异常状态）', { uid });
        return;
      }
      setRecentFrame(uid, f.payload);
      // 推给订阅了这个 uid 的管理端（谁订阅推谁，不群发）
      const r = broadcastFrame(uid, f.payload, seq);
      if (r.hits > 0) pushEvent('info', '帧已推给管理端', { uid, viewers: r.hits, bytes: r.bytes, seq });
      else pushEvent('warn', '帧推了但没人订阅（管理端没连或没选这台）', { uid });
      return;
    }

    const p = parseIncoming(data.toString('utf8'));
    if (!p.ok) { sndErr(ws, p.code, p.detail); return; }
    if (!p.legacy) ws.__v1 = true;

    if (p.type === 'heartbeat') {
      // 存活信号：与帧解耦（按需拉流后没人看的机器不推帧，帧不能当存活依据）
      const ok = touchHeartbeat(uid);
      if (!ok) pushEvent('error', '收到心跳但设备不在在线表里（异常状态）', { uid });
      return;
    }

    if (p.type === 'receipt') {
      // 被控端真执行完了，回的是 done/failed（不是 received）。必须如实记、如实转。
      const { id, action, result, error, data } = p.payload;
      const rec = recordResult(id, uid, action, result, error, data);
      // 转发给所有管理端：状态行从「已下发」变成「真做了 / 真没做成」，
      // 并带上动作的附加数据（进程列表 / 日志行 / 音量实际值 / 截图路径）
      broadcastToViewers('instruction-result', {
        uid,
        action: rec.action,
        id,
        state: 'executed',
        result: rec.result,
        error: rec.error,
        data: rec.data,
      });
      return;
    }

    if (p.type === 'event') {
      // 设备侧事件上报（将来回吐站点入库用；现在先如实记进事件流水，不丢）
      const { level, msg, extra } = p.payload;
      pushEvent(level === 'error' ? 'error' : (level === 'warn' ? 'warn' : 'info'),
        `设备上报：${String(msg ?? '')}`, { uid, ...(extra || {}) });
      return;
    }

    // 未知类型**不关闭连接**：允许新客户端试探新能力（协议规范第十节）
    sndErr(ws, 'UNKNOWN_TYPE', p.type);
  });

  ws.on('close', () => { finish(); if (registered) markDisconnected(ws); });

  ws.on('error', (e) => {
    pushEvent('error', 'WS 连接出错', { uid: uid || '(未知)', error: e.message });
  });
});

server.listen(PORT, '127.0.0.1', () => {
  pushEvent('info', '云端已启动，等被控端接入', { port: PORT });
});

server.on('error', (e) => {
  console.error(`[cloud] FAIL 端口占用或启动失败: ${e.message}`);
  process.exit(1);
});
