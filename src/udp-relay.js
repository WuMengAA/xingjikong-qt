/* UDP 闪传通道 · 云端帧中继（2026-10-20 · U1 骨架）
 *
 * 定位：两端（被控端 / 管理端）之间的 UDP 帧中转站。**不做端到端解密** ——
 *   payload 的 ChaCha20-Poly1305 密钥只握在两端手里（经已鉴权 WS 协商，见 index.js
 *   的 udp-session 消息），中继按帧头 session 字段路由，原样转发，热路径零加解密。
 *   只有 HELLO / PING 两类低频控制帧由中继亲自解密（校验令牌 / 打时间戳）。
 *
 * 帧格式 / 安全面 / seq 空间纪律见 flash-core.js（本文件只做 UDP 传输层）：
 *   可达范围仅回环/局域网（cloudflared 只转 TCP，公网 UDP 不可达）；
 *   公网主路已定走 TCP 专用连接（tcp-relay.js，用户 2026-10-20「那就 tcp」）。
 *   UDP 中继**不做** seq 单调闸门——UDP 会丢包，端侧可靠性层（U2）要合法重传，
 *   闸门会把重传挡掉；重放防护在两端（AEAD nonce + 端侧单调）。
 *
 * 安全面（新攻击面，全部 fail-closed）：
 *   - 令牌未配（UDP_RELAY_TOKEN 空）⇒ createUdpRelay 直接拒绝启动（index.js 只会告警不拉起）。
 *   - HELLO 令牌校验用 timingSafeEqual（先比长度，防异常泄露）。
 *   - 单会话帧率闸门 + 单帧大小封顶 + 会话空闲收口，三件事防资源耗尽。
 */
import { createSocket } from 'node:dgram';
import {
  T, ROLE_AGENT, ROLE_VIEWER, OTHER,
  HEADER_SIZE, TAG_SIZE, MAX_PAYLOAD,
  encodeFrame, decodeFrame, sealPayload, openPayload, nonceFor,
  safeEqual, clientSeqOk, makeRelayFrame, verifyHello, parsePing,
  createFlashSessionStore,
} from './flash-core.js';

// 兼容既有测试/调用方的命名再导出（实现已移至 flash-core.js）
export { encodeFrame, decodeFrame, sealPayload, openPayload, nonceFor, safeEqual, T };
export const MAX_DATAGRAM = HEADER_SIZE + TAG_SIZE + MAX_PAYLOAD;

/* ---------- 中继 ---------- */

/**
 * @param {{host:string, port:number, token:string, maxFps:number, idleMs:number,
 *          pushEvent:(level:string, msg:string, detail?:object)=>void}} opts
 */
export function createUdpRelay(opts) {
  const { host, port, token, maxFps, idleMs, pushEvent } = opts;
  if (!token) {
    // 新攻击面不许 fail-open：没配令牌就不启动（index.js 据此只告警不拉 socket）。
    return { start: () => { throw new Error('UDP_RELAY_TOKEN 未配置，中继拒绝启动'); }, configured: false,
             stop() {}, stats: () => ({ configured: false }), createSession: () => null,
             closeSession() {}, closeSessionsWhere() {}, sessionCount: () => 0 };
  }

  /** @type {Map<number, {id:number, key:Buffer, uid:string, viewerKey:string,
   *                       peers:Object, send:Object, winStart:number, winCount:number,
   *                       framesIn:number, framesOut:number, bytesIn:number, bytesOut:number,
   *                       drops:number, tampers:number, rtt:number[], createdAt:number, lastAt:number}>} */
  let sock = null;
  const addrIndex = new Map();   // "ip:port" → {sessionId, role}
  const store = createFlashSessionStore({
    pushEvent, notifyType: 'udp-session', label: 'UDP 闪传',
    onSessionClosed: (s) => {
      for (const role of [ROLE_AGENT, ROLE_VIEWER]) {
        const p = s.peers[role];
        if (p) addrIndex.delete(p.addr);
      }
    },
  });
  const sessions = store.sessions;
  const totals = store.totals;

  /** index.js 在 WS 协商成功后调用：建会话并把两端 WS 发送函数挂上。 */
  function createSession({ uid, viewerWs, sendToAgent, sendToViewer }) {
    return store.createSession({ uid, viewerWs, sendToAgent, sendToViewer });
  }

  function closeSession(sessionId, reason = 'closed') {
    store.closeSession(sessionId, reason);
  }

  function closeSessionsWhere(pred) {
    store.closeSessionsWhere(pred);
  }

  function bindPeer(s, role, rinfo) {
    const addr = `${rinfo.address}:${rinfo.port}`;
    const prev = s.peers[role];
    if (prev && prev.addr !== addr) {
      addrIndex.delete(prev.addr);   // 端重连换址：旧映射作废
      pushEvent('warn', `UDP 会话 #${s.id} ${role} 换址`, { from: prev.addr, to: addr });
    }
    s.peers[role] = { addr, lastSeq: 0, helloAt: Date.now() };
    addrIndex.set(addr, { sessionId: s.id, role });
    return addr;
  }

  function sendFrame(s, fromRole, frame) {
    const toRole = OTHER[fromRole];
    const p = s.peers[toRole];
    if (!p) { store.drop(s); return false; }   // 对端还没 HELLO：U1 如实丢，U2 可靠性层重传
    const [ip, pt] = p.addr.split(':');
    try {
      sock.send(frame, Number(pt), ip);
      store.bumpOut(s, frame.length);
      return true;
    } catch {
      store.drop(s);
      return false;
    }
  }

  function onMessage(raw, rinfo) {
    const f = decodeFrame(raw);
    if (!f.ok) { totals.drops++; return; }
    const s = sessions.get(f.session);
    if (!s) { totals.drops++; return; }
    store.bumpIn(s, raw.length);

    // 帧率闸门：每秒一窗，超了整窗丢（帧可丢，心跳/控制另有 WS 通道不受影响）。
    const now = Date.now();
    if (now - s.winStart >= 1000) { s.winStart = now; s.winCount = 0; }
    if (++s.winCount > maxFps) { store.drop(s); return; }

    // 帧头里的 role 未知 ⇒ 先按 addrIndex 找；两个都不认 ⇒ 从未 HELLO 的连接，只允许多 HELLO。
    let role = addrIndex.get(`${rinfo.address}:${rinfo.port}`)?.role;
    if (role && addrIndex.get(`${rinfo.address}:${rinfo.port}`)?.sessionId !== s.id) role = undefined;

    if (f.type === T.HELLO) {
      if (f.seq !== 0) { store.drop(s); return; }
      const v = verifyHello(s, f, token);
      if (!v.ok) {
        if (v.reason === 'token') {
          store.drop(s, 'tampers');
          pushEvent('warn', 'UDP HELLO 令牌不符，已拒绝', { session: s.id, from: `${rinfo.address}:${rinfo.port}` });
        } else if (v.reason === 'decrypt') {
          store.drop(s, 'tampers');
          pushEvent('warn', 'UDP HELLO 解密失败（密钥不对？）', { session: s.id });
        } else {
          store.drop(s);
        }
        return;
      }
      bindPeer(s, v.hello.role, rinfo);
      const peersReady = !!(s.peers[ROLE_AGENT] && s.peers[ROLE_VIEWER]);
      // 回 HELLO_ACK：带服务端时间戳（首跳延迟打点），走中继 seq 空间（防 nonce 重用）。
      const out = makeRelayFrame(s, T.HELLO_ACK, { ok: true, serverTs: Date.now(), peersReady, role: v.hello.role });
      try { sock.send(out, rinfo.port, rinfo.address); } catch { /* 对端跑了就算了 */ }
      store.bumpOut(s, out.length);
      if (peersReady) pushEvent('info', `UDP 会话 #${s.id} 两端就绪`, { uid: s.uid });
      return;
    }

    if (f.type === T.HELLO_ACK) { store.drop(s); return; }   // ACK 只能中继发

    if (!role) { store.drop(s); return; }   // 未握手连接的任何业务帧：丢

    // ⚠️ seq 空间纪律：端→中继帧必须 < 2^63（中继自发帧的空间）。越界即协议违规，拒。
    // 不做 seq ≤ lastSeq 的单调闸门——那会把端侧可靠性层（U2 选择重传）的合法重传挡掉；
    // 重放防护在两端：AEAD nonce 每 seq 一次 + 端侧自己的单调校验。
    if (!clientSeqOk(f.seq)) { store.drop(s); return; }
    s.peers[role].lastSeq = Math.max(s.peers[role].lastSeq, f.seq);   // 仅统计

    if (f.type === T.PING) {
      // 中继亲自解：to='relay' 回 PONG（两跳时间戳），否则转发。
      const body = parsePing(s, f);
      if (body && body.to === 'relay') {
        const out = makeRelayFrame(s, T.PONG, { sentTs: body.sentTs, relayTs: Date.now() });
        try { sock.send(out, rinfo.port, rinfo.address); } catch { /* 对端跑了 */ }
        store.bumpOut(s, out.length);
        return;
      }
      sendFrame(s, role, raw);
      return;
    }

    if (f.type === T.CLOSE) {
      sendFrame(s, role, raw);          // 先让对端知道，再收口
      closeSession(s.id, 'peer-close');
      return;
    }

    // DATA / ACK / NACK / PARITY / PONG：原样转发（中继不解密热路径帧）。
    sendFrame(s, role, raw);
  }

  function start() {
    return new Promise((resolve, reject) => {
      sock = createSocket('udp4');
      sock.on('message', onMessage);
      sock.on('error', (err) => {
        // 只记不关：回环上对端端口关了可能回 ICMP ⇒ send 侧 error，
        // 一遇错就 close 会把整个中继打死。真正要死的错（bind 失败）走下面的 reject。
        pushEvent('warn', 'UDP 中继 socket 事件', { message: String(err?.message || err) });
      });
      sock.bind(port, host, () => {
        const bound = sock.address();
        pushEvent('info', `UDP 闪传中继已监听 ${bound.address}:${bound.port}`, { token: 'configured' });
        resolve({ host: bound.address, port: bound.port });
      });
      sock.on('error', reject);   // bind 失败（端口占用）也走 reject
    });
  }

  function stop() {
    for (const s of [...sessions.values()]) closeSession(s.id, 'relay-stop');
    try { sock?.close(); } catch { /* 已关 */ }
    sock = null;
  }

  function stats() {
    const perSession = [...sessions.values()].map((s) => ({
      id: s.id, uid: s.uid, peers: {
        agent: !!s.peers[ROLE_AGENT], viewer: !!s.peers[ROLE_VIEWER],
      },
      framesIn: s.framesIn, framesOut: s.framesOut,
      bytesIn: s.bytesIn, bytesOut: s.bytesOut,
      drops: s.drops, tampers: s.tampers,
      rttSamples: s.rtt.length,
      ageMs: Date.now() - s.createdAt,
    }));
    return { configured: true, host, port, sessions: sessions.size, ...totals, perSession };
  }

  return { configured: true, start, stop, stats, createSession, closeSession, closeSessionsWhere, sessionCount: () => sessions.size };
}
