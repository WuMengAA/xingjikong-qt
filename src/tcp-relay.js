/* TCP 闪传通道 · 云端帧中继（2026-10-20 · 用户定「那就 tcp」后建）
 *
 * 定位：与 udp-relay.js 同一套帧、同一套加密、同一个会话存储（flash-core.js），
 *   换 TCP 管道。**为什么 TCP 是公网主路**：cloudflared 隧道只转 TCP，公网 UDP
 *   不可达；且专用 TCP 连接把「实时/大批数据」与「WS 控制+心跳」彻底分离——
 *   200KB/s 背压闸（保心跳的粗暴手段，却把远控全屏首帧丢光）和文件传输停等协议
 *   的病根一起去掉。帧格式/安全面/seq 空间纪律见 flash-core.js，此处只写 TCP 差异。
 *
 * 与 UDP 中继的差异（同一份帧协议的两种管道）：
 *   1. 流式分帧：TCP 是字节流，每 socket 一个累积缓冲，按 len 字段切帧
 *      （半帧/多帧合一帧/超限帧三种情况都有断言，见 unit-tcp-relay.mjs）。
 *   2. **严格单调闸门**：TCP 有序可靠，seq 必须递增，重放即拒——UDP 那边不敢做
 *      （会挡 U2 选择重传的合法重传），TCP 这边做了反而多一层 nonce 重用防线。
 *   3. **一端 socket 断 ⇒ 整个会话收口**（reason=peer-tcp-gone，双端 WS 收
 *      closed）：防「旧连接发过的 seq 在新连接重放」的跨连接 nonce 重用窗口，
 *      也让两端明确走 WS 重新协商（新密钥）或回落 WS 既有路径。
 *   4. **背压不丢帧**：对端写缓冲满 ⇒ pause 源 socket，drain 后 resume。
 *      中继转发放大但不积压（内存有界）；帧率闸门不需要了——TCP 自带流控，
 *      且不再与心跳抢同一条连接。
 *   5. 无帧率闸门、无 MTU 分片顾虑（len 字段上限 65535，单帧远大于 UDP 的 2048）。
 *
 * 安全面（新攻击面，全部 fail-closed）：
 *   - 令牌未配（TCP_RELAY_TOKEN 空）⇒ createTcpRelay 直接拒绝启动。
 *   - HELLO 令牌 timingSafeEqual；未 HELLO 的连接只许发 HELLO，其余全丢。
 *   - 久不 HELLO 的连接（helloGraceMs）断开；连接数封顶；会话空闲收口。
 */
import net from 'node:net';
import {
  T, ROLE_AGENT, ROLE_VIEWER, OTHER,
  HEADER_SIZE, TAG_SIZE, MAX_PAYLOAD_STREAM,
  decodeFrame, clientSeqOk, makeRelayFrame, verifyHello, parsePing,
  createFlashSessionStore,
} from './flash-core.js';

const MAX_CONNS = 256;              // 并发连接封顶（防 fd 耗尽）；超出直接拒
const REAP_INTERVAL_MS = 5000;      // 空闲收口巡检周期

/* ---------- 中继 ---------- */

/**
 * @param {{host:string, port:number, token:string, idleMs:number, helloGraceMs:number,
 *          reapIntervalMs?:number,
 *          pushEvent:(level:string, msg:string, detail?:object)=>void}} opts
 */
export function createTcpRelay(opts) {
  const { host, port, token, idleMs, helloGraceMs, pushEvent } = opts;
  const reapIntervalMs = Math.max(50, Number(opts.reapIntervalMs) || REAP_INTERVAL_MS);
  if (!token) {
    // 新攻击面不许 fail-open：没配令牌就不启动（index.js 据此只告警不拉 socket）。
    return { start: () => { throw new Error('TCP_RELAY_TOKEN 未配置，中继拒绝启动'); }, configured: false,
             stop() {}, stats: () => ({ configured: false }), createSession: () => null,
             closeSession() {}, closeSessionsWhere() {}, sessionCount: () => 0 };
  }

  let server = null;
  let reaper = null;
  let refused = 0;
  let pauses = 0;      // 背压暂停源读取的次数（ops 可见 + 测试可断言）
  let resumes = 0;     // drain 后续读的次数
  const conns = new Set();          // 全部存活 socket（含未 HELLO 的）

  const store = createFlashSessionStore({
    pushEvent, notifyType: 'tcp-session', label: 'TCP 闪传',
    onSessionClosed: (s) => {
      // 收口会话 ⇒ 两端 socket 一起断（它们随后会触发 onSockClose，但会话已不在表里，安全）
      for (const role of [ROLE_AGENT, ROLE_VIEWER]) {
        const p = s.peers[role];
        if (p?.sock && !p.sock.destroyed) { try { p.sock.destroy(); } catch { /* 已关 */ } }
      }
    },
  });
  const sessions = store.sessions;
  const totals = store.totals;

  const addrOf = (s) => `${s.remoteAddress}:${s.remotePort}`;

  function createSession({ uid, viewerWs, sendToAgent, sendToViewer }) {
    return store.createSession({ uid, viewerWs, sendToAgent, sendToViewer });
  }
  function closeSession(sessionId, reason = 'closed') {
    store.closeSession(sessionId, reason);
  }
  function closeSessionsWhere(pred) {
    store.closeSessionsWhere(pred);
  }

  /** 转发一帧给对端；对端写缓冲满时暂停源 socket（drain 后续读），中继不丢帧。 */
  function forward(s, fromRole, frame, fromSock) {
    const p = s.peers[OTHER[fromRole]];
    if (!p || !p.sock || p.sock.destroyed) { store.drop(s); return false; }
    let flushed = true;
    try { flushed = p.sock.write(frame); } catch { store.drop(s); return false; }
    store.bumpOut(s, frame.length);
    if (!flushed && fromSock && !fromSock.isPaused()) {
      fromSock.pause();
      pauses++;
      p.pausedFrom = fromRole;      // 记录：本 socket 的拥堵暂停了哪一端
    }
    return true;
  }

  function handleFrame(raw, sock) {
    const f = decodeFrame(raw, MAX_PAYLOAD_STREAM);
    if (!f.ok) { totals.drops++; return; }
    const s = sessions.get(f.session);
    if (!s) { totals.drops++; return; }
    const bind = sock.__flash;
    if (bind && bind.sessionId !== f.session) {
      // 一个 socket 绑定后只承载一个会话（防拿 A 会话的密钥帧混进 B 会话）
      store.drop(s);
      pushEvent('warn', `TCP 会话串线：socket 绑 #${bind.sessionId} 却发 #${f.session} 的帧`, { from: addrOf(sock) });
      return;
    }
    store.bumpIn(s, raw.length);

    /* ---- HELLO：绑定 socket ↔ (会话, 角色) ---- */
    if (f.type === T.HELLO) {
      if (f.seq !== 0) { store.drop(s); return; }
      if (bind) { store.drop(s); return; }               // 已绑定的 socket 再 HELLO：违规
      const v = verifyHello(s, f, token);
      if (!v.ok) {
        if (v.reason === 'token') {
          store.drop(s, 'tampers');
          pushEvent('warn', 'TCP HELLO 令牌不符，已拒绝', { session: s.id, from: addrOf(sock) });
        } else if (v.reason === 'decrypt') {
          store.drop(s, 'tampers');
          pushEvent('warn', 'TCP HELLO 解密失败（密钥不对？）', { session: s.id });
        } else {
          store.drop(s);
        }
        return;
      }
      const role = v.hello.role;
      if (s.peers[role]) {
        // 该端已有人在位（活着的 socket）：拒绝新人。会话归在位者所有——
        // 否则旧连接的 seq 空间与新连接重叠 ⇒ 跨连接 nonce 重用窗口。
        store.drop(s);
        pushEvent('warn', `TCP 会话 #${s.id} ${role} 重复 HELLO，已拒`, { from: addrOf(sock) });
        return;
      }
      if (sock.__helloTimer) { clearTimeout(sock.__helloTimer); sock.__helloTimer = null; }
      sock.__flash = { sessionId: s.id, role };
      s.peers[role] = { sock, lastSeq: 0, helloAt: Date.now(), pausedFrom: null };
      const peersReady = !!(s.peers[ROLE_AGENT] && s.peers[ROLE_VIEWER]);
      const out = makeRelayFrame(s, T.HELLO_ACK, { ok: true, serverTs: Date.now(), peersReady, role });
      try { sock.write(out); } catch { /* 对端跑了 */ }
      store.bumpOut(s, out.length);
      if (peersReady) pushEvent('info', `TCP 会话 #${s.id} 两端就绪`, { uid: s.uid });
      return;
    }

    if (f.type === T.HELLO_ACK) { store.drop(s); return; }   // ACK 只能中继发
    if (!bind) { store.drop(s); return; }                     // 未绑定连接的任何业务帧：丢
    const role = bind.role;

    // TCP 有序可靠 ⇒ 严格单调（重放即拒；顺带守住 nonce 不重用）。HELLO 时基线已置 0。
    if (!clientSeqOk(f.seq) || f.seq <= s.peers[role].lastSeq) { store.drop(s); return; }
    s.peers[role].lastSeq = f.seq;

    if (f.type === T.PING) {
      // 中继亲自解：to='relay' 回 PONG（两跳时间戳），否则转发。
      const body = parsePing(s, f);
      if (body && body.to === 'relay') {
        const out = makeRelayFrame(s, T.PONG, { sentTs: body.sentTs, relayTs: Date.now() });
        try { sock.write(out); } catch { /* 对端跑了 */ }
        store.bumpOut(s, out.length);
        return;
      }
      forward(s, role, raw, sock);
      return;
    }

    if (f.type === T.CLOSE) {
      forward(s, role, raw, sock);       // 先让对端知道，再收口
      closeSession(s.id, 'peer-close');
      return;
    }

    // DATA / ACK / NACK / PARITY / PONG：原样转发（中继不解密热路径帧）。
    forward(s, role, raw, sock);
  }

  function onConn(sock) {
    if (conns.size >= MAX_CONNS) {
      refused++;
      pushEvent('warn', 'TCP 闪传连接数封顶，已拒', { from: addrOf(sock), max: MAX_CONNS });
      try { sock.destroy(); } catch { /* 已关 */ }
      return;
    }
    conns.add(sock);
    try {
      sock.setNoDelay(true);                 // 闪传要低延迟：禁 Nagle
      sock.setKeepAlive(true, 30000);        // 早点发现半死连接
    } catch { /* 老平台没有这些选项就算了 */ }
    sock.__flash = null;

    // 久不 HELLO 的连接断开（防裸连接堆积）
    sock.__helloTimer = setTimeout(() => {
      if (!sock.__flash && !sock.destroyed) {
        pushEvent('warn', 'TCP 连接久不 HELLO，已断开', { from: addrOf(sock) });
        try { sock.destroy(); } catch { /* 已关 */ }
      }
    }, helloGraceMs);
    sock.__helloTimer.unref?.();

    // 流式分帧：累积缓冲 + 按 len 切帧（半帧等下一 chunk；一 chunk 多帧一次吃光）
    let buf = Buffer.alloc(0);
    let fatal = false;
    sock.on('data', (chunk) => {
      if (fatal) return;
      buf = buf.length ? Buffer.concat([buf, chunk]) : chunk;
      for (;;) {
        if (buf.length < HEADER_SIZE) return;
        const len = buf.readUInt16BE(24);
        if (len > MAX_PAYLOAD_STREAM) {
          // 帧长越界 = 协议违规（或非本协议流量）：断开，不猜
          fatal = true;
          pushEvent('warn', 'TCP 闪传帧超限，已断开', { from: addrOf(sock), len });
          try { sock.destroy(); } catch { /* 已关 */ }
          return;
        }
        const total = HEADER_SIZE + TAG_SIZE + len;
        if (buf.length < total) return;
        handleFrame(Buffer.from(buf.subarray(0, total)), sock);   // 拷贝：buf 马上重切片
        buf = buf.subarray(total);
      }
    });

    // 对端写缓冲排空 ⇒ 续读被背压暂停的源 socket
    sock.on('drain', () => {
      const bind = sock.__flash;
      if (!bind) return;
      const s = sessions.get(bind.sessionId);
      if (!s) return;
      const p = s.peers[bind.role];
      if (p && p.pausedFrom) {
        const src = s.peers[p.pausedFrom]?.sock;
        if (src && src.isPaused()) { src.resume(); resumes++; }
        p.pausedFrom = null;
      }
    });

    const bye = () => {
      conns.delete(sock);
      if (sock.__helloTimer) clearTimeout(sock.__helloTimer);
      const bind = sock.__flash;
      if (!bind) return;
      const s = sessions.get(bind.sessionId);
      if (!s) return;
      if (s.peers[bind.role]?.sock !== sock) return;   // 已被收口/顶替
      // 一端 TCP 断 ⇒ 会话收口（防跨连接 nonce 重用 + 两端明确重协/回落）
      closeSession(bind.sessionId, 'peer-tcp-gone');
    };
    sock.on('close', bye);
    sock.on('error', (err) => {
      pushEvent('warn', 'TCP 闪传连接出错', { from: addrOf(sock), message: String(err?.message || err) });
      try { sock.destroy(); } catch { /* 已关 */ }
    });
  }

  function startServer() {
    return new Promise((resolve, reject) => {
      server = net.createServer(onConn);
      server.on('error', (err) => {
        // 只记不关：单连接错已由 onConn 的 error handler 收拾；这里防的是未预期错误吵醒进程
        pushEvent('warn', 'TCP 中继 server 事件', { message: String(err?.message || err) });
      });
      server.listen(port, host, () => {
        const bound = server.address();
        pushEvent('info', `TCP 闪传中继已监听 ${bound.address}:${bound.port}`, { token: 'configured' });
        resolve({ host: bound.address, port: bound.port });
      });
      server.on('error', reject);   // listen 失败（端口占用）走 reject
    });
  }

  async function start() {
    const b = await startServer();
    reaper = setInterval(() => {
      const now = Date.now();
      for (const s of [...sessions.values()]) {
        if (now - s.lastAt > idleMs) closeSession(s.id, 'idle');
      }
    }, reapIntervalMs);
    reaper.unref?.();
    return b;
  }

  function stop() {
    if (reaper) clearInterval(reaper);
    reaper = null;
    for (const s of [...sessions.values()]) closeSession(s.id, 'relay-stop');
    for (const c of [...conns]) { try { c.destroy(); } catch { /* 已关 */ } }
    conns.clear();
    try { server?.close(); } catch { /* 已关 */ }
    server = null;
  }

  function stats() {
    const perSession = [...sessions.values()].map((s) => ({
      id: s.id, uid: s.uid,
      peers: { agent: !!s.peers[ROLE_AGENT], viewer: !!s.peers[ROLE_VIEWER] },
      framesIn: s.framesIn, framesOut: s.framesOut,
      bytesIn: s.bytesIn, bytesOut: s.bytesOut,
      drops: s.drops, tampers: s.tampers,
      rttSamples: s.rtt.length,
      ageMs: Date.now() - s.createdAt,
    }));
    return { configured: true, host, port, sessions: sessions.size, conns: conns.size, refused, pauses, resumes, ...totals, perSession };
  }

  // 启动后开始空闲巡检（stop 时清）
  return { configured: true, start, stop, stats, createSession, closeSession, closeSessionsWhere, sessionCount: () => sessions.size };
}
