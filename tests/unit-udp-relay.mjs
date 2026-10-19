/* UDP 闪传中继 · 单元 + 回环集成测试（2026-10-20 · U1）
 * 跑法：node tests/unit-udp-relay.mjs
 * 覆盖：帧编解码回环（含偏移正确性）/ AEAD 篡改检测 / nonce 派生 /
 *       HELLO 握手（双端就绪）/ DATA 原样中继 / 同 seq 重传不被挡（U2 ARQ 前提）/
 *       seq 空间纪律（≥2^63 拒收）/ PING→PONG 打点 / CLOSE 收口 /
 *       令牌不符拒绝 / 帧率闸门 / 未配令牌 fail-closed。
 * 全部走 127.0.0.1 临时端口，不碰生产 8789。 */
import assert from 'node:assert/strict';
import { createSocket } from 'node:dgram';
import { createUdpRelay, encodeFrame, decodeFrame, sealPayload, openPayload, T } from '../src/udp-relay.js';

const TOKEN = 'unit-test-udp-token';
const RELAY_SEQ_BASE = 2 ** 63;
let passed = 0;
const failures = [];
function ok(cond, name, extra) {
  if (cond) { passed++; console.log(`  ✅ ${name}`); }
  else { failures.push(name); console.log(`  ❌ ${name}${extra ? ' — ' + extra : ''}`); }
}

/* ---------- 1. 编解码 ---------- */
console.log('【帧编解码】');
{
  const payload = Buffer.from('hello-udp', 'utf8');
  const frame = encodeFrame({ type: T.DATA, session: 0x01020304, seq: 7, sendTs: 1234567890, payload });
  ok(frame.length === 26 + 16 + payload.length, '帧总长 = 帧头+tag+payload');
  const f = decodeFrame(frame);
  ok(f.ok && f.type === T.DATA && f.session === 0x01020304 && f.seq === 7 && f.sendTs === 1234567890,
    '帧头字段回环（seq@8/sendTs@16/len@24 不重叠）', JSON.stringify(f));
  ok(f.len === payload.length && f.aad.length === 26 && f.tag.length === 16, 'AAD/tag/len 切分正确');
  ok(decodeFrame(Buffer.alloc(10)).ok === false, '过短帧拒认');
  ok(decodeFrame(Buffer.from('XX' + frame.subarray(2))).ok === false, '坏 magic 拒认');
  // 大小端/边界：seq 与 sendTs 都吃满 8 字节时不互啃
  const big = encodeFrame({ type: T.DATA, session: 1, seq: 2 ** 53, sendTs: 2 ** 53, payload: Buffer.alloc(3) });
  const fb = decodeFrame(big);
  ok(fb.ok && fb.seq === 2 ** 53 && fb.sendTs === 2 ** 53, '大 seq/sendTs 不互相覆盖');
}

/* ---------- 2. AEAD ---------- */
console.log('【AEAD】');
{
  const key = Buffer.alloc(32, 7);
  const nonce = Buffer.alloc(12); nonce.writeUInt32BE(0xAABBCCDD, 0); nonce.writeBigUInt64BE(42n, 4);
  const aad = Buffer.from('header-bytes', 'utf8');
  const sealed = sealPayload(key, nonce, aad, Buffer.from('secret', 'utf8'));
  const tag = sealed.subarray(0, 16), ct = sealed.subarray(16);
  const pt = openPayload(key, nonce, aad, tag, ct);
  ok(pt && pt.toString() === 'secret', '加解密回环');
  const badAad = Buffer.from('header-byteX', 'utf8');   // 改一个 AAD 字节
  ok(openPayload(key, nonce, badAad, tag, ct) === null, 'AAD 被改 ⇒ 解密失败（帧头防篡改）');
  const badKey = Buffer.alloc(32, 8);
  ok(openPayload(badKey, nonce, aad, tag, ct) === null, '密钥不对 ⇒ 解密失败');
  const n2 = Buffer.alloc(12); n2.writeUInt32BE(0xAABBCCDD, 0); n2.writeBigUInt64BE(43n, 4);
  ok(!nonce.equals(n2), 'nonce = session||seq，seq 变则 nonce 变（不重用）');
}

/* ---------- 3. fail-closed ---------- */
console.log('【fail-closed】');
{
  const relay = createUdpRelay({ host: '127.0.0.1', port: 0, token: '', maxFps: 100, idleMs: 1000,
    pushEvent: () => {} });
  ok(relay.configured === false, '无令牌 ⇒ 中继不装配');
  let threw = false;
  try { relay.start(); } catch { threw = true; }
  ok(threw, '无令牌 start() 直接抛错');
}

/* ---------- 工具 ---------- */
function udpClient(port) {
  const s = createSocket('udp4');
  s.bind(0, '127.0.0.1');
  const inbox = [];
  s.on('message', (msg) => inbox.push(msg));
  return {
    sock: s, inbox,
    send(buf) { s.send(buf, port, '127.0.0.1'); },
    /** 等一条满足 pred 的帧，超时返回 null */
    wait(pred, ms = 1500) {
      return new Promise((resolve) => {
        const t0 = Date.now();
        const tick = () => {
          const hit = inbox.findIndex(pred);
          if (hit >= 0) { resolve(inbox.splice(hit, 1)[0]); return; }
          if (Date.now() - t0 > ms) { resolve(null); return; }
          setTimeout(tick, 20);
        };
        tick();
      });
    },
    close() { try { s.close(); } catch {} },
  };
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const nonceOf = (sid, seq) => { const n = Buffer.alloc(12); n.writeUInt32BE(sid, 0); n.writeBigUInt64BE(BigInt(seq), 4); return n; };
/** 生成某会话的客户端帧构造器（key 来自 createSession）。 */
function frameMakers(sessionId, key) {
  const seal = (type, seq, payload) => {
    const hdr = encodeFrame({ type, session: sessionId, seq, payload });
    return Buffer.concat([hdr.subarray(0, 26), sealPayload(key, nonceOf(sessionId, seq), hdr.subarray(0, 26), payload)]);
  };
  return {
    hello: (role, token = TOKEN) => seal(T.HELLO, 0, Buffer.from(JSON.stringify({ token, role }), 'utf8')),
    data: (seq, text) => seal(T.DATA, seq, Buffer.from(text, 'utf8')),
    ping: (seq, to) => seal(T.PING, seq, Buffer.from(JSON.stringify({ to, sentTs: Date.now() }), 'utf8')),
    close: (seq) => seal(T.CLOSE, seq, Buffer.from(JSON.stringify({ reason: 'done' }), 'utf8')),
  };
}

/* ---------- 4. 回环全链路 ---------- */
console.log('【回环全链路】');
await (async () => {
  const events = [];
  const relay = createUdpRelay({ host: '127.0.0.1', port: 0, token: TOKEN, maxFps: 10000, idleMs: 60000,
    pushEvent: (lv, msg, d) => events.push({ lv, msg, d }) });
  const bound = await relay.start();
  const wsMsgs = { agent: [], viewer: [] };
  const { sessionId, key } = relay.createSession({
    uid: 'dev-1', viewerKey: 'vk-1',
    sendToAgent: (m) => wsMsgs.agent.push(m),
    sendToViewer: (m) => wsMsgs.viewer.push(m),
  });
  const mk = frameMakers(sessionId, key);

  const agent = udpClient(bound.port);
  const viewer = udpClient(bound.port);
  await sleep(50);

  // HELLO：agent 先，viewer 后 ⇒ 第二帧回 peersReady
  agent.send(mk.hello('agent'));
  const ack1 = await agent.wait((m) => decodeFrame(m).type === T.HELLO_ACK);
  ok(!!ack1, 'agent HELLO ⇒ HELLO_ACK');
  ok(!!ack1 && decodeFrame(ack1).session === sessionId, 'HELLO_ACK 带对会话号');
  ok(!!ack1 && decodeFrame(ack1).seq >= RELAY_SEQ_BASE,
    'HELLO_ACK 走中继 seq 空间（≥2^63，不与 HELLO 同 nonce）');
  viewer.send(mk.hello('viewer'));
  const ack2 = await viewer.wait((m) => decodeFrame(m).type === T.HELLO_ACK);
  ok(!!ack2, 'viewer HELLO ⇒ HELLO_ACK');
  ok(events.some((e) => e.msg.includes('两端就绪')), '双端就绪事件');

  // DATA：agent → viewer 原样中继
  const f1 = mk.data(1, 'frame-bytes');
  agent.send(f1);
  const got = await viewer.wait((m) => decodeFrame(m).type === T.DATA);
  ok(!!got, 'agent→viewer DATA 中继到达');
  ok(got && got.equals(f1), '中继字节级原样（未解密未改动）');

  // 同 seq 重传：必须照转（中继若做单调闸门会把 U2 选择重传挡掉——回归护栏）
  agent.send(f1);
  const got2 = await viewer.wait((m) => decodeFrame(m).type === T.DATA);
  ok(!!got2 && got2.equals(f1), '同 seq 重传也被转发（不挡端侧重传）');

  // seq 空间纪律：端侧发 ≥2^63 的帧 ⇒ 拒
  const dropsBefore = relay.stats().perSession[0].drops;
  agent.send(mk.data(RELAY_SEQ_BASE, 'evil'));
  const none = await viewer.wait((m) => decodeFrame(m).type === T.DATA, 300);
  ok(none === null, '端侧 seq ≥2^63（中继空间）⇒ 拒收');
  ok(relay.stats().perSession[0].drops > dropsBefore, '违规帧计入 drops');

  // PING to relay ⇒ PONG 带 relayTs
  viewer.send(mk.ping(1, 'relay'));
  const pong = await viewer.wait((m) => decodeFrame(m).type === T.PONG);
  ok(!!pong, 'PING(to=relay) ⇒ PONG');
  if (pong) {
    const f = decodeFrame(pong);
    ok(f.seq >= RELAY_SEQ_BASE, 'PONG 也走中继 seq 空间');
    const pt = openPayload(key, nonceOf(sessionId, f.seq), f.aad, f.tag, f.payload);
    const body = pt && JSON.parse(pt.toString());
    ok(body && typeof body.relayTs === 'number' && typeof body.sentTs === 'number', 'PONG 带两跳时间戳（延迟采集）');
  }

  // 令牌不符 ⇒ 无 ACK
  viewer.send(mk.hello('viewer', 'wrong-token'));
  const noAck = await viewer.wait((m) => decodeFrame(m).type === T.HELLO_ACK, 400);
  ok(noAck === null, '令牌不符 ⇒ 不给 HELLO_ACK');
  ok(events.some((e) => e.msg.includes('令牌不符')), '令牌不符已告警');

  // CLOSE ⇒ 会话收口 + 两端 WS 都收到 closed
  agent.send(mk.close(2));
  await sleep(150);
  ok(relay.sessionCount() === 0, 'CLOSE ⇒ 会话收口');
  ok(wsMsgs.agent.some((m) => m.action === 'closed') && wsMsgs.viewer.some((m) => m.action === 'closed'),
    '两端 WS 均收到 closed 通知');

  // stats 可读
  const st = relay.stats();
  ok(st.configured && st.sessions === 0 && st.framesIn > 0 && st.framesOut > 0, 'stats 汇总可用');

  agent.close(); viewer.close();
  relay.stop();
})().catch((e) => { console.error('测试崩了：', e); process.exit(1); });

/* ---------- 5. 帧率闸门（独立中继，maxFps=4） ---------- */
console.log('【帧率闸门】');
await (async () => {
  const relay = createUdpRelay({ host: '127.0.0.1', port: 0, token: TOKEN, maxFps: 4, idleMs: 60000,
    pushEvent: () => {} });
  const bound = await relay.start();
  const { sessionId, key } = relay.createSession({ uid: 'g', viewerKey: 'g', sendToAgent: () => {}, sendToViewer: () => {} });
  const mk = frameMakers(sessionId, key);
  const a = udpClient(bound.port), v = udpClient(bound.port);
  await sleep(30);
  a.send(mk.hello('agent')); await a.wait((m) => decodeFrame(m).type === T.HELLO_ACK);
  v.send(mk.hello('viewer')); await v.wait((m) => decodeFrame(m).type === T.HELLO_ACK);
  for (let i = 1; i <= 5; i++) a.send(mk.data(i, `f${i}`));
  await sleep(300);
  const dataCount = v.inbox.filter((m) => decodeFrame(m).type === T.DATA).length;
  ok(dataCount === 2, `每秒超窗丢帧：发 5 收 ${dataCount}（闸门 4/s，前两帧 HELLO 占窗）`);
  a.close(); v.close(); relay.stop();
})().catch((e) => { console.error('测试崩了：', e); process.exit(1); });

console.log(`\n══ 结果: ${passed} 通过 / ${failures.length} 失败 ══`);
if (failures.length) { console.log('失败项：', failures); process.exit(1); }
process.exit(0);
