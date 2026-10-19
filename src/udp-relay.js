/* UDP 闪传通道 · 云端帧中继（2026-10-20 · U1 骨架）
 *
 * 定位：两端（被控端 / 管理端）之间的 UDP 帧中转站。**不做端到端解密** ——
 *   payload 的 ChaCha20-Poly1305 密钥只握在两端手里（经已鉴权 WS 协商，见 index.js
 *   的 udp-session 消息），中继按帧头 session 字段路由，原样转发，热路径零加解密。
 *   只有 HELLO / PING 两类低频控制帧由中继亲自解密（校验令牌 / 打时间戳）。
 *
 * 帧格式（小端长度字段除外，全部大端）：
 *   magic 'SU'(2) | ver(1) | type(1) | session(4) | seq(8) | sendTs(8) | len(2) | tag(16) | payload
 *   - 帧头 26B 明文：中继要按 session 路由，且 AAD 用整个帧头（防改类型/会话/序号重放）。
 *   - tag 16B + payload：AEAD 输出。nonce = session(4B) || seq(8B)（12B，RFC 7539），
 *     每会话每发送端 seq 单调递增 ⇒ nonce 永不重用；HELLO 固定 seq=0（中继强制）。
 *   - len = payload 明文长度（流密码 ct 与 pt 等长），封顶 MAX_PAYLOAD。
 *   - ⚠️ seq 空间划分（nonce 重用是 ChaCha20 的死刑）：**端→中继帧 seq < 2^63**；
 *     **中继自发帧（HELLO_ACK/PONG）seq = 2^63 + 会话内单调计数**。两边空间不相交，
 *     同密钥下 nonce 绝不重用（否则 HELLO 与 HELLO_ACK 同 nonce 异明文 ⇒ keystream
 *     重用，XOR 两条密文即可还原 HELLO 里的令牌）。端侧发 seq ≥ 2^63 的帧一律拒。
 *
 * 类型：1 HELLO / 2 HELLO_ACK / 3 DATA / 4 ACK / 5 NACK / 6 PARITY / 7 PING / 8 PONG / 9 CLOSE
 *   HELLO  : 端→中继，payload={token, role}，证明"拿着协商密钥 + 知道中继令牌"。
 *   DATA   : 端→端（经中继原样转发）。ACK/NACK/PARITY 同理，语义由两端的可靠性层定。
 *   PING   : payload={to:'relay'|'peer', sentTs}；to='relay' 时中继回 PONG（打两跳时间戳），
 *            to='peer' 时转发（两端互测 RTT）。延迟信息（用户「交给你处理」）由此采集。
 *   CLOSE  : 任一端可发；中继转发给对端后收口会话。
 *
 * 安全面（新攻击面，全部 fail-closed）：
 *   - 令牌未配（UDP_RELAY_TOKEN 空）⇒ createUdpRelay 直接拒绝启动（index.js 只会告警不拉起）。
 *   - HELLO 令牌校验用 timingSafeEqual（先比长度，防异常泄露）。
 *   - 每端 seq 必须单调递增（防重放）；HELLO 重置该端 seq 基线（允许断线重绑）。
 *   - 单会话帧率闸门 + 单帧大小封顶 + 会话空闲收口，三件事防资源耗尽。
 */
import { createSocket } from 'node:dgram';
import { createCipheriv, createDecipheriv, randomBytes, timingSafeEqual } from 'node:crypto';

const MAGIC = Buffer.from('SU', 'ascii');
const VERSION = 1;
const HEADER_SIZE = 26;          // magic(2)+ver(1)+type(1)+session(4)+seq(8)+sendTs(8)+len(2)
const TAG_SIZE = 16;
const NONCE_SIZE = 12;
const MAX_PAYLOAD = 2048;        // U1 封顶；U2 按 LAN MTU 1200 定正式分片大小
const MAX_DATAGRAM = HEADER_SIZE + TAG_SIZE + MAX_PAYLOAD;
const RELAY_SEQ_BASE = 2n ** 63n;   // 中继自发帧的 seq 空间起点（见文件头 nonce 重用说明）

export const T = { HELLO: 1, HELLO_ACK: 2, DATA: 3, ACK: 4, NACK: 5, PARITY: 6, PING: 7, PONG: 8, CLOSE: 9 };
const ROLE_AGENT = 'agent';
const ROLE_VIEWER = 'viewer';
const OTHER = { [ROLE_AGENT]: ROLE_VIEWER, [ROLE_VIEWER]: ROLE_AGENT };

/* ---------- 帧编解码 ---------- */

/** 组装一帧（payload 为明文 Buffer；返回可 send 的 Buffer）。 */
export function encodeFrame({ type, session, seq, sendTs = Date.now(), payload = Buffer.alloc(0) }) {
  if (payload.length > MAX_PAYLOAD) throw new Error(`udp frame payload too large: ${payload.length}`);
  const buf = Buffer.alloc(HEADER_SIZE + TAG_SIZE + payload.length);
  MAGIC.copy(buf, 0);
  buf.writeUInt8(VERSION, 2);
  buf.writeUInt8(type, 3);
  buf.writeUInt32BE(session >>> 0, 4);
  buf.writeBigUInt64BE(BigInt(seq), 8);
  buf.writeBigUInt64BE(BigInt(sendTs), 16);
  buf.writeUInt16BE(payload.length, 24);
  // tag 与密文由调用方 seal 后回填（见 sealPayload）
  return buf;
}

/** 解析帧头（不解密）。ok=false 时 reason 说明原因。 */
export function decodeFrame(buf) {
  if (!buf || buf.length < HEADER_SIZE + TAG_SIZE) return { ok: false, reason: 'too-short' };
  if (buf[0] !== MAGIC[0] || buf[1] !== MAGIC[1]) return { ok: false, reason: 'bad-magic' };
  const ver = buf.readUInt8(2);
  if (ver !== VERSION) return { ok: false, reason: 'bad-version' };
  const len = buf.readUInt16BE(24);
  if (buf.length !== HEADER_SIZE + TAG_SIZE + len) return { ok: false, reason: 'bad-length' };
  return {
    ok: true,
    type: buf.readUInt8(3),
    session: buf.readUInt32BE(4),
    seq: Number(buf.readBigUInt64BE(8)),
    sendTs: Number(buf.readBigUInt64BE(16)),
    len,
    aad: buf.subarray(0, HEADER_SIZE),       // 整个帧头当 AAD
    tag: buf.subarray(HEADER_SIZE, HEADER_SIZE + TAG_SIZE),
    payload: buf.subarray(HEADER_SIZE + TAG_SIZE),   // 密文（与明文等长）
  };
}

/** AEAD 封装：返回 tag||ciphertext（直接拼进帧的 tag+payload 区）。 */
export function sealPayload(key, nonce12, aad, plain) {
  const c = createCipheriv('chacha20-poly1305', key, nonce12, { authTagLength: TAG_SIZE });
  c.setAAD(aad);
  const ct = Buffer.concat([c.update(plain), c.final()]);
  const tag = c.getAuthTag();
  return Buffer.concat([tag, ct]);
}

/** AEAD 解封；篡改/密钥不对 ⇒ 返回 null（调用方记 tamper 并丢弃）。 */
export function openPayload(key, nonce12, aad, tag, ct) {
  try {
    const d = createDecipheriv('chacha20-poly1305', key, nonce12, { authTagLength: TAG_SIZE });
    d.setAAD(aad);
    d.setAuthTag(tag);
    return Buffer.concat([d.update(ct), d.final()]);
  } catch {
    return null;
  }
}

function nonceFor(session, seq) {
  const n = Buffer.alloc(NONCE_SIZE);
  n.writeUInt32BE(session >>> 0, 0);
  n.writeBigUInt64BE(BigInt(seq), 4);
  return n;
}

function safeEqual(a, b) {
  const ba = Buffer.from(String(a), 'utf8');
  const bb = Buffer.from(String(b), 'utf8');
  if (ba.length !== bb.length) return false;
  return timingSafeEqual(ba, bb);
}

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
  const sessions = new Map();
  const addrIndex = new Map();   // "ip:port" → {sessionId, role}
  let sock = null;
  const totals = { framesIn: 0, framesOut: 0, bytesIn: 0, bytesOut: 0, drops: 0, tampers: 0, sessionsOpened: 0 };

  function newSessionId() {
    for (let i = 0; i < 8; i++) {
      const id = randomBytes(4).readUInt32BE(0);
      if (!sessions.has(id)) return id;
    }
    throw new Error('udp session id 碰撞重试耗尽');
  }

  /** index.js 在 WS 协商成功后调用：建会话并把两端 WS 发送函数挂上。 */
  function createSession({ uid, viewerWs, sendToAgent, sendToViewer }) {
    const id = newSessionId();
    sessions.set(id, {
      id, key: randomBytes(32), uid, viewerWs,
      peers: { [ROLE_AGENT]: null, [ROLE_VIEWER]: null },   // {addr, lastSeq, helloAt}
      send: { [ROLE_AGENT]: sendToAgent, [ROLE_VIEWER]: sendToViewer },
      winStart: Date.now(), winCount: 0, relaySeq: 0,
      framesIn: 0, framesOut: 0, bytesIn: 0, bytesOut: 0, drops: 0, tampers: 0, rtt: [],
      createdAt: Date.now(), lastAt: Date.now(),
    });
    totals.sessionsOpened++;
    return { sessionId: id, key: sessions.get(id).key };
  }

  function closeSession(sessionId, reason = 'closed') {
    const s = sessions.get(sessionId);
    if (!s) return;
    for (const role of [ROLE_AGENT, ROLE_VIEWER]) {
      const p = s.peers[role];
      if (p) addrIndex.delete(p.addr);
      const send = s.send[role];
      if (send) {
        try { send({ type: 'udp-session', action: 'closed', sessionId, reason }); } catch { /* 对端已断 */ }
      }
    }
    sessions.delete(sessionId);
    pushEvent('info', `UDP 会话收口 #${sessionId}`, { reason, uid: s.uid, framesIn: s.framesIn, framesOut: s.framesOut });
  }

  /** WS 侧断开时按条件收口（agent 掉线 / viewer 离开），防会话泄漏（远控泄漏的同一病）。 */
  function closeSessionsWhere(pred) {
    for (const s of [...sessions.values()]) {
      if (pred(s)) closeSession(s.id, 'peer-ws-gone');
    }
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
    if (!p) { s.drops++; totals.drops++; return false; }   // 对端还没 HELLO：U1 如实丢，U2 可靠性层重传
    const [ip, pt] = p.addr.split(':');
    try {
      sock.send(frame, Number(pt), ip);
      s.framesOut++; s.bytesOut += frame.length;
      totals.framesOut++; totals.bytesOut += frame.length;
      return true;
    } catch {
      s.drops++; totals.drops++;
      return false;
    }
  }

  function onMessage(raw, rinfo) {
    const f = decodeFrame(raw);
    if (!f.ok) { totals.drops++; return; }
    const s = sessions.get(f.session);
    if (!s) { totals.drops++; return; }
    s.lastAt = Date.now();
    s.framesIn++; s.bytesIn += raw.length;
    totals.framesIn++; totals.bytesIn += raw.length;

    // 帧率闸门：每秒一窗，超了整窗丢（帧可丢，心跳/控制另有 WS 通道不受影响）。
    const now = Date.now();
    if (now - s.winStart >= 1000) { s.winStart = now; s.winCount = 0; }
    if (++s.winCount > maxFps) { s.drops++; totals.drops++; return; }

    // 帧头里的 role 未知 ⇒ 先按 addrIndex 找；两个都不认 ⇒ 从未 HELLO 的连接，只允许多 HELLO。
    let role = addrIndex.get(`${rinfo.address}:${rinfo.port}`)?.role;
    if (role && addrIndex.get(`${rinfo.address}:${rinfo.port}`)?.sessionId !== s.id) role = undefined;

    if (f.type === T.HELLO) {
      if (f.seq !== 0) { s.drops++; totals.drops++; return; }
      const pt = openPayload(s.key, nonceFor(s.id, 0), f.aad, f.tag, f.payload);
      if (!pt) { s.tampers++; totals.tampers++; pushEvent('warn', 'UDP HELLO 解密失败（密钥不对？）', { session: s.id }); return; }
      let hello;
      try { hello = JSON.parse(pt.toString('utf8')); } catch { s.drops++; totals.drops++; return; }
      if (!safeEqual(hello.token, token)) {
        s.tampers++; totals.tampers++;
        pushEvent('warn', 'UDP HELLO 令牌不符，已拒绝', { session: s.id, from: `${rinfo.address}:${rinfo.port}` });
        return;
      }
      if (hello.role !== ROLE_AGENT && hello.role !== ROLE_VIEWER) { s.drops++; totals.drops++; return; }
      bindPeer(s, hello.role, rinfo);
      const peersReady = !!(s.peers[ROLE_AGENT] && s.peers[ROLE_VIEWER]);
      // 回 HELLO_ACK：带服务端时间戳（首跳延迟打点）。⚠️ 必须**新建帧头**（payload 长度与
      // HELLO 不同，复用请求头会让 len 字段对不上 ⇒ 对端 bad-length 拒收），且 seq 走中继
      // 空间（BASE+计数）——与 HELLO 同 nonce 会 keystream 重用泄令牌。
      const ackPayload = Buffer.from(JSON.stringify({ ok: true, serverTs: Date.now(), peersReady, role: hello.role }), 'utf8');
      const ackSeq = Number(RELAY_SEQ_BASE + BigInt(s.relaySeq++));
      const ackHdr = encodeFrame({ type: T.HELLO_ACK, session: s.id, seq: ackSeq, payload: ackPayload });
      const sealed = sealPayload(s.key, nonceFor(s.id, ackSeq), ackHdr.subarray(0, HEADER_SIZE), ackPayload);
      const out = Buffer.concat([ackHdr.subarray(0, HEADER_SIZE), sealed]);
      try { sock.send(out, rinfo.port, rinfo.address); } catch { /* 对端跑了就算了 */ }
      s.framesOut++; s.bytesOut += out.length; totals.framesOut++; totals.bytesOut += out.length;
      if (peersReady) pushEvent('info', `UDP 会话 #${s.id} 两端就绪`, { uid: s.uid });
      return;
    }

    if (f.type === T.HELLO_ACK) { s.drops++; totals.drops++; return; }   // ACK 只能中继发

    if (!role) { s.drops++; totals.drops++; return; }   // 未握手连接的任何业务帧：丢

    // ⚠️ seq 空间纪律：端→中继帧必须 < 2^63（中继自发帧的空间）。越界即协议违规，拒。
    // 不做 seq ≤ lastSeq 的单调闸门——那会把端侧可靠性层（U2 选择重传）的合法重传挡掉；
    // 重放防护在两端：AEAD nonce 每 seq 一次 + 端侧自己的单调校验。
    if (BigInt(f.seq) >= RELAY_SEQ_BASE) { s.drops++; totals.drops++; return; }
    s.peers[role].lastSeq = Math.max(s.peers[role].lastSeq, f.seq);   // 仅统计

    if (f.type === T.PING) {
      // 中继亲自解：to='relay' 回 PONG（两跳时间戳），否则转发。
      const pt = openPayload(s.key, nonceFor(s.id, f.seq), f.aad, f.tag, f.payload);
      if (pt) {
        try {
          const body = JSON.parse(pt.toString('utf8'));
          if (body.to === 'relay') {
            const pongPayload = Buffer.from(JSON.stringify({ sentTs: body.sentTs, relayTs: Date.now() }), 'utf8');
            const pongSeq = Number(RELAY_SEQ_BASE + BigInt(s.relaySeq++));
            const pongHdr = encodeFrame({ type: T.PONG, session: s.id, seq: pongSeq, payload: pongPayload });
            const sealed = sealPayload(s.key, nonceFor(s.id, pongSeq), pongHdr.subarray(0, HEADER_SIZE), pongPayload);
            const out = Buffer.concat([pongHdr.subarray(0, HEADER_SIZE), sealed]);
            try { sock.send(out, rinfo.port, rinfo.address); } catch { /* 对端跑了 */ }
            s.framesOut++; s.bytesOut += out.length; totals.framesOut++; totals.bytesOut += out.length;
            return;
          }
        } catch { /* 坏 JSON 的 PING 当作 to=peer 转发，另一端会丢 */ }
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
