/* 闪传通道 · 共享内核（2026-10-20 · U1 建，TCP 主轴定稿）
 *
 * 定位：UDP（udp-relay.js）与 TCP（tcp-relay.js）两个传输层中继共用的一套：
 *   帧编解码、AEAD（ChaCha20-Poly1305）、会话存储、中继自发帧构造、HELLO/PING 校验。
 *   传输层只负责「收发字节 + 对端标识」，安全相关逻辑不重复实现（一份代码两处审计）。
 *
 * 帧格式（小端长度字段除外，全部大端）：
 *   magic 'SU'(2) | ver(1) | type(1) | session(4) | seq(8) | sendTs(8) | len(2) | tag(16) | payload
 *   - 帧头 26B 明文：中继要按 session 路由，且 AAD 用整个帧头（防改类型/会话/序号重放）。
 *   - tag 16B + payload：AEAD 输出。nonce = session(4B) || seq(8B)（12B，RFC 7539），
 *     每会话每发送端 seq 单调递增 ⇒ nonce 永不重用；HELLO 固定 seq=0（中继强制）。
 *   - len = payload 明文长度（流密码 ct 与 pt 等长）。
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
 * 传输层差异（同一套帧，两种管道，2026-10-20 用户定「那就 tcp」后定型）：
 *   - UDP（udp-relay.js）：回环/局域网直连。cloudflared 隧道只转 TCP ⇒ 公网不可达。
 *     中继**不做** seq 单调闸门——UDP 会丢包，端侧可靠性层（U2）要合法重传，
 *     闸门会把重传挡掉；重放防护在两端（AEAD nonce + 端侧单调）。
 *   - TCP（tcp-relay.js）：公网主路（专用连接，与 WS 控制/心跳彻底分离，
 *     200KB/s 背压闸与停等协议的病根一起去掉）。TCP 有序可靠 ⇒ 中继**做**严格
 *     单调闸门（重放即拒，顺带守住 nonce 不重用）；一端 socket 断开 ⇒ 整个
 *     会话收口（防跨连接 nonce 重用窗口，也让两端明确走 WS 重协或回落）。
 */
import { createCipheriv, createDecipheriv, randomBytes, timingSafeEqual } from 'node:crypto';

export const MAGIC = Buffer.from('SU', 'ascii');
export const VERSION = 1;
export const HEADER_SIZE = 26;          // magic(2)+ver(1)+type(1)+session(4)+seq(8)+sendTs(8)+len(2)
export const TAG_SIZE = 16;
export const NONCE_SIZE = 12;
export const MAX_PAYLOAD = 2048;          // UDP：IPv4 不分片安全载荷（U2 定正式分片大小）
export const MAX_PAYLOAD_STREAM = 65535;  // TCP：len 字段（UInt16）物理上限，流式无 MTU 问题
export const RELAY_SEQ_BASE = 2n ** 63n;  // 中继自发帧的 seq 空间起点（见文件头 nonce 重用说明）

export const T = { HELLO: 1, HELLO_ACK: 2, DATA: 3, ACK: 4, NACK: 5, PARITY: 6, PING: 7, PONG: 8, CLOSE: 9 };
export const ROLE_AGENT = 'agent';
export const ROLE_VIEWER = 'viewer';
export const OTHER = { [ROLE_AGENT]: ROLE_VIEWER, [ROLE_VIEWER]: ROLE_AGENT };

/* ---------- 帧编解码 ---------- */

/** 组装帧头（payload 为明文 Buffer；tag+密文由 sealPayload 回填）。 */
export function encodeFrame({ type, session, seq, sendTs = Date.now(), payload = Buffer.alloc(0), maxPayload = MAX_PAYLOAD }) {
  if (payload.length > maxPayload) throw new Error(`flash frame payload too large: ${payload.length} > ${maxPayload}`);
  const buf = Buffer.alloc(HEADER_SIZE + TAG_SIZE + payload.length);
  MAGIC.copy(buf, 0);
  buf.writeUInt8(VERSION, 2);
  buf.writeUInt8(type, 3);
  buf.writeUInt32BE(session >>> 0, 4);
  buf.writeBigUInt64BE(BigInt(seq), 8);
  buf.writeBigUInt64BE(BigInt(sendTs), 16);
  buf.writeUInt16BE(payload.length, 24);
  return buf;
}

/** 只 peek 长度字段（流式解析用：不满一头返回 null）。 */
export function peekFrameLen(buf) {
  if (!buf || buf.length < HEADER_SIZE) return null;
  return buf.readUInt16BE(24);
}

/** 解析整帧（不解密）。ok=false 时 reason 说明原因。maxPayload 按传输层传。 */
export function decodeFrame(buf, maxPayload = MAX_PAYLOAD) {
  if (!buf || buf.length < HEADER_SIZE + TAG_SIZE) return { ok: false, reason: 'too-short' };
  if (buf[0] !== MAGIC[0] || buf[1] !== MAGIC[1]) return { ok: false, reason: 'bad-magic' };
  const ver = buf.readUInt8(2);
  if (ver !== VERSION) return { ok: false, reason: 'bad-version' };
  const len = buf.readUInt16BE(24);
  if (len > maxPayload) return { ok: false, reason: 'payload-too-large' };
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

/* ---------- AEAD ---------- */

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

export function nonceFor(session, seq) {
  const n = Buffer.alloc(NONCE_SIZE);
  n.writeUInt32BE(session >>> 0, 0);
  n.writeBigUInt64BE(BigInt(seq), 4);
  return n;
}

/** 令牌比对：先比长度再 timingSafeEqual（防异常侧信道）。 */
export function safeEqual(a, b) {
  const ba = Buffer.from(String(a), 'utf8');
  const bb = Buffer.from(String(b), 'utf8');
  if (ba.length !== bb.length) return false;
  return timingSafeEqual(ba, bb);
}

/** 端→中继帧的 seq 必须落在客户端空间（<2^63）；越界即协议违规。 */
export function clientSeqOk(seq) {
  return BigInt(seq) < RELAY_SEQ_BASE;
}

/* ---------- 中继自发帧 ---------- */

/**
 * 中继亲自发的帧（HELLO_ACK / PONG）：**必须新建帧头**（payload 长度与请求帧不同，
 * 复用请求头会让 len 对不上 ⇒ 对端 bad-length 拒收），且 seq 走中继空间
 * （BASE+会话内计数）——与客户端帧同 nonce 会 keystream 重用泄令牌。
 */
export function makeRelayFrame(s, type, payloadObj) {
  const payload = Buffer.from(JSON.stringify(payloadObj), 'utf8');
  const seq = Number(RELAY_SEQ_BASE + BigInt(s.relaySeq++));
  const hdr = encodeFrame({ type, session: s.id, seq, payload, maxPayload: MAX_PAYLOAD_STREAM });
  const sealed = sealPayload(s.key, nonceFor(s.id, seq), hdr.subarray(0, HEADER_SIZE), payload);
  return Buffer.concat([hdr.subarray(0, HEADER_SIZE), sealed]);
}

/* ---------- HELLO / PING 校验（中继亲自解的低频控制帧） ---------- */

/** 校验 HELLO：解密 → JSON → 令牌 → role。失败时 reason: decrypt/json/token/role。 */
export function verifyHello(s, f, token) {
  const pt = openPayload(s.key, nonceFor(s.id, 0), f.aad, f.tag, f.payload);
  if (!pt) return { ok: false, reason: 'decrypt' };
  let hello;
  try { hello = JSON.parse(pt.toString('utf8')); } catch { return { ok: false, reason: 'json' }; }
  if (!safeEqual(hello.token, token)) return { ok: false, reason: 'token' };
  if (hello.role !== ROLE_AGENT && hello.role !== ROLE_VIEWER) return { ok: false, reason: 'role' };
  return { ok: true, hello };
}

/** 解 PING body（中继亲自解的那类）；解密失败/坏 JSON 返回 null。 */
export function parsePing(s, f) {
  const pt = openPayload(s.key, nonceFor(s.id, f.seq), f.aad, f.tag, f.payload);
  if (!pt) return null;
  try { return JSON.parse(pt.toString('utf8')); } catch { return null; }
}

/* ---------- 会话存储（两个中继共用） ---------- */

/**
 * 会话表 + 计数器 + WS 通知。传输层通过 opts 注入差异化行为：
 *   pushEvent    —— 事件流水（与云端其余部分同一出口）
 *   notifyType   —— WS 通知消息类型（'udp-session' / 'tcp-session'）
 *   label        —— 日志里的通道名（'UDP 闪传' / 'TCP 闪传'）
 *   onSessionClosed(s) —— 收口前回调：传输层清理自己的对端绑定（addrIndex / socket）
 */
export function createFlashSessionStore({ pushEvent, notifyType = 'flash-session', label = '闪传', onSessionClosed } = {}) {
  /** @type {Map<number, object>} */
  const sessions = new Map();
  const totals = { framesIn: 0, framesOut: 0, bytesIn: 0, bytesOut: 0, drops: 0, tampers: 0, sessionsOpened: 0 };

  function newSessionId() {
    for (let i = 0; i < 8; i++) {
      const id = randomBytes(4).readUInt32BE(0);
      if (!sessions.has(id)) return id;
    }
    throw new Error(`${label} session id 碰撞重试耗尽`);
  }

  /** 传输层在 WS 协商成功后调用：建会话并把两端 WS 发送函数挂上。 */
  function createSession({ uid, viewerWs, sendToAgent, sendToViewer }) {
    const id = newSessionId();
    sessions.set(id, {
      id, key: randomBytes(32), uid, viewerWs,
      peers: { [ROLE_AGENT]: null, [ROLE_VIEWER]: null },   // 传输层自有结构（{addr}|{sock}|…）
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
    if (onSessionClosed) { try { onSessionClosed(s); } catch { /* 清理失败不挡收口 */ } }
    for (const role of [ROLE_AGENT, ROLE_VIEWER]) {
      const send = s.send[role];
      if (send) {
        try { send({ type: notifyType, action: 'closed', sessionId, reason }); } catch { /* 对端已断 */ }
      }
    }
    sessions.delete(sessionId);
    pushEvent('info', `${label} 会话收口 #${sessionId}`, { reason, uid: s.uid, framesIn: s.framesIn, framesOut: s.framesOut });
  }

  /** WS 侧断开时按条件收口（agent 掉线 / viewer 离开），防会话泄漏（远控泄漏的同一病）。 */
  function closeSessionsWhere(pred) {
    for (const s of [...sessions.values()]) {
      if (pred(s)) closeSession(s.id, 'peer-ws-gone');
    }
  }

  function bumpIn(s, n) {
    s.lastAt = Date.now();
    s.framesIn++; s.bytesIn += n;
    totals.framesIn++; totals.bytesIn += n;
  }
  function bumpOut(s, n) {
    s.framesOut++; s.bytesOut += n;
    totals.framesOut++; totals.bytesOut += n;
  }
  function drop(s, kind = 'drops') {
    if (s) s[kind]++;
    totals[kind]++;
  }

  return {
    sessions, totals,
    createSession, closeSession, closeSessionsWhere,
    bumpIn, bumpOut, drop,
    sessionCount: () => sessions.size,
  };
}
