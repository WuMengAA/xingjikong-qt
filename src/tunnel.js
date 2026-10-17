// 远控隧道（RFB over WS）——云端只做字节中继，不解析内容。
//
// 协议见 docs/方案-远控隧道-RFB-over-WS-协议草案-2026-10-18.md
//
// 为什么要它：教室机在 NAT 后，WebRTC 的 ICE 打洞反复失败（2026-10-17/18 两次卡死）。
// 改成「agent 主动外拨 + 云端字节中继」后没有任何打洞环节，VNC(RFB) 本身是纯 TCP 字节流，
// 语义上等价于老师端直连教室机的 5900。
//
// 安全边界（写死在代码里，不靠配置）：
//   · 目标地址由 agent 决定（它只连 127.0.0.1:5900）；云端**不下发 host:port**，
//     只下发 {sid,key,port}，port 仅作提示/审计。
//   · 开隧道要过两道：viewer 令牌 + canAccess(设备班级)，与"看画面"同一口径。
//   · 每台设备同时只允许 1 条隧道，新的顶掉旧的。

import crypto from 'node:crypto';
import { WebSocketServer } from 'ws';

const MAX_BUFFER = 4 * 1024 * 1024;   // 单向积压上限：超过就暂停对侧读取
const RESUME_AT = 1 * 1024 * 1024;    // 降到这个水位以下恢复
const IDLE_MS = 30 * 60 * 1000;       // 30 分钟无字节 → 断
const MAX_MS = 2 * 60 * 60 * 1000;    // 单会话硬上限 2 小时
const ATTACH_TIMEOUT_MS = 10 * 1000;  // agent 必须在 10s 内 attach
const TICK_MS = 30 * 1000;

export function createTunnel(deps) {
  const {
    authorizeViewer, identityFromAuth, canAccess, getDeviceClassCode,
    getDevice, sendToAgentMsg, pushEvent,
  } = deps;

  const viewerWss = new WebSocketServer({ noServer: true, maxPayload: 8 * 1024 * 1024 });
  const agentWss = new WebSocketServer({ noServer: true, maxPayload: 8 * 1024 * 1024 });
  const sessions = new Map();      // sid -> session
  const byUid = new Map();         // uid -> sid

  const now = () => Date.now();

  function sendJson(ws, type, payload = {}) {
    try {
      if (ws && ws.readyState === 1) {
        ws.send(JSON.stringify({ v: 1, type, ts: now(), payload }));
      }
    } catch { /* 对端已断，交给 close 处理 */ }
  }

  function closeSession(s, reason, from) {
    if (!s || s.closed) return;
    s.closed = true;
    s.endedAt = now();
    clearTimeout(s.attachTimer);
    if (s.viewerWs) { sendJson(s.viewerWs, 'tunnel.close', { reason, from }); try { s.viewerWs.close(); } catch {} }
    if (s.agentWs) { sendJson(s.agentWs, 'tunnel.close', { reason, from }); try { s.agentWs.close(); } catch {} }
    sessions.delete(s.sid);
    if (byUid.get(s.uid) === s.sid) byUid.delete(s.uid);
    pushEvent('info', '远控隧道关闭', {
      uid: s.uid, sid: s.sid, reason,
      seconds: Math.round((s.endedAt - s.createdAt) / 1000),
      upKiB: Math.round(s.up / 1024),
      downKiB: Math.round(s.down / 1024),
    });
  }

  /* ---------- 背压：单向积压过大就暂停对侧 ---------- */
  function pump(src, dst, counter) {
    // src/dst 都是 WebSocket。ws 没有 pause API，直接操作底层 socket。
    const srcSock = src && src._socket;
    if (!srcSock) return;
    if (dst.bufferedAmount > MAX_BUFFER) {
      try { srcSock.pause(); } catch {}
      const timer = setInterval(() => {
        if (src.readyState !== 1 || dst.readyState !== 1) { clearInterval(timer); return; }
        if (dst.bufferedAmount < RESUME_AT) {
          try { srcSock.resume(); } catch {}
          clearInterval(timer);
        }
      }, 50);
    }
    if (counter) counter();
  }

  /* ---------- viewer 侧 ---------- */
  viewerWss.on('connection', (ws) => {
    let ident = null;
    let authed = false;
    let sid = null;

    ws.on('message', (data, isBinary) => {
      if (isBinary) {
        const s = sessions.get(sid);
        if (!s || s.closed || !s.agentWs) return;
        s.up += data.length;
        s.lastAt = now();
        try { s.agentWs.send(data, { binary: true }); } catch { closeSession(s, 'agent send failed', 'cloud'); return; }
        pump(ws, s.agentWs);
        return;
      }
      let msg;
      try { msg = JSON.parse(data.toString('utf8')); } catch { return sendJson(ws, 'error', { code: 'BAD_JSON' }); }
      const type = msg && msg.type;
      const payload = (msg && msg.payload) || {};

      if (!authed) {
        if (type !== 'auth') return sendJson(ws, 'error', { code: 'NOT_AUTHED' });
        const r = authorizeViewer(payload);
        if (!r.ok) {
          sendJson(ws, 'error', { code: 'NOT_AUTHED', detail: r.reason || '' });
          try { ws.close(); } catch {}
          return;
        }
        authed = true;
        ident = identityFromAuth(r);
        sendJson(ws, 'tunnel.ready', {});
        return;
      }

      if (type === 'tunnel.open') {
        const uid = String(payload.uid || '').trim();
        if (!uid) return sendJson(ws, 'error', { code: 'BAD_SID', detail: '缺 uid' });

        // 权限：与"看画面"同一口径
        const cls = getDeviceClassCode(uid);
        if (!canAccess(ident, cls)) {
          pushEvent('warn', '远控隧道被拒（无权限）', { uid });
          return sendJson(ws, 'error', { code: 'NO_PERMISSION', uid });
        }
        const dev = getDevice(uid);
        if (!dev || !dev.ws || dev.ws.readyState !== 1) {
          return sendJson(ws, 'error', { code: 'DEVICE_OFFLINE', uid });
        }

        // 每台设备只留一条：旧的顶掉
        const old = byUid.get(uid);
        if (old && sessions.has(old)) closeSession(sessions.get(old), 'superseded', 'cloud');

        const newSid = crypto.randomBytes(9).toString('hex');
        const key = crypto.randomBytes(16).toString('hex');
        const s = {
          sid: newSid, key, uid, viewerWs: ws, agentWs: null,
          createdAt: now(), lastAt: now(), up: 0, down: 0, closed: false, attachTimer: null,
        };
        sessions.set(newSid, s);
        byUid.set(uid, newSid);
        sid = newSid;

        const r = sendToAgentMsg(uid, 'tunnel.open', { sid: newSid, key, port: 5900 });
        if (!r.ok) {
          closeSession(s, 'agent unreachable', 'cloud');
          return sendJson(ws, 'error', { code: 'DEVICE_OFFLINE', uid });
        }
        // agent 必须在 10s 内 attach，否则判失败（避免 viewer 干等）
        s.attachTimer = setTimeout(() => {
          if (!s.agentWs) {
            pushEvent('warn', '远控隧道超时（agent 未接入）', { uid, sid: newSid });
            closeSession(s, 'attach timeout', 'cloud');
          }
        }, ATTACH_TIMEOUT_MS);
        pushEvent('info', '远控隧道发起', { uid, sid: newSid });
        return;
      }

      if (type === 'tunnel.close') {
        const s = sessions.get(sid);
        if (s) closeSession(s, String(payload.reason || 'viewer closed'), 'viewer');
        return;
      }
      sendJson(ws, 'error', { code: 'BAD_PAYLOAD', detail: `未知 type ${String(type)}` });
    });

    ws.on('close', () => {
      const s = sessions.get(sid);
      if (s) closeSession(s, 'viewer gone', 'viewer');
    });
    ws.on('error', () => {});
  });

  /* ---------- agent 侧（第二条连接，只用来搬字节） ---------- */
  agentWss.on('connection', (ws) => {
    let sid = null;
    ws.on('message', (data, isBinary) => {
      if (isBinary) {
        const s = sessions.get(sid);
        if (!s || s.closed || !s.viewerWs) return;
        s.down += data.length;
        s.lastAt = now();
        try { s.viewerWs.send(data, { binary: true }); } catch { closeSession(s, 'viewer send failed', 'cloud'); return; }
        pump(ws, s.viewerWs);
        return;
      }
      let msg;
      try { msg = JSON.parse(data.toString('utf8')); } catch { return; }
      if (msg.type !== 'tunnel.attach') {
        sendJson(ws, 'error', { code: 'BAD_PAYLOAD', detail: '先 attach' });
        return;
      }
      const p = msg.payload || {};
      const s = sessions.get(String(p.sid || ''));
      if (!s || s.closed) return sendJson(ws, 'error', { code: 'BAD_SID' });
      if (s.key !== String(p.key || '')) return sendJson(ws, 'error', { code: 'BAD_SID', detail: 'key 不符' });
      if (s.agentWs) return sendJson(ws, 'error', { code: 'TUNNEL_BUSY' });

      clearTimeout(s.attachTimer);
      s.agentWs = ws;
      sid = s.sid;
      sendJson(ws, 'tunnel.attached', { sid: s.sid });
      sendJson(s.viewerWs, 'tunnel.opened', { sid: s.sid, uid: s.uid });
      pushEvent('info', '远控隧道已贯通', { uid: s.uid, sid: s.sid });
    });
    ws.on('close', () => {
      const s = sessions.get(sid);
      if (s) closeSession(s, 'agent gone', 'agent');
    });
    ws.on('error', () => {});
  });

  /* ---------- 巡检：空闲 / 超时 ---------- */
  const ticker = setInterval(() => {
    const t = now();
    for (const s of [...sessions.values()]) {
      if (s.closed) continue;
      if (t - s.createdAt > MAX_MS) closeSession(s, 'max duration', 'cloud');
      else if (t - s.lastAt > IDLE_MS) closeSession(s, 'idle', 'cloud');
    }
  }, TICK_MS);
  if (ticker.unref) ticker.unref();

  return {
    handleViewerUpgrade(req, socket, head) {
      viewerWss.handleUpgrade(req, socket, head, (ws) => viewerWss.emit('connection', ws, req));
    },
    handleAgentUpgrade(req, socket, head) {
      agentWss.handleUpgrade(req, socket, head, (ws) => agentWss.emit('connection', ws, req));
    },
    stats() {
      return { sessions: sessions.size, uids: [...byUid.keys()] };
    },
  };
}
