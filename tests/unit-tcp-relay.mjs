/* TCP 闪传中继 · 单元 + 回环集成测试（2026-10-20 · TCP 主轴定稿后）
 * 跑法：node tests/unit-tcp-relay.mjs
 * 覆盖：
 *   ① 流式分帧：一帧切三瓣 / 两帧合一 chunk / 半帧等下一 chunk / 空 payload /
 *      最大 payload(65535) 回环 / len 越界即断
 *   ② 回环全链路：双端 HELLO(peersReady) / DATA 双向原样中继 /
 *      **严格单调**（同 seq 重传被拒——与 UDP 中继故意相反，TCP 有序可靠）/ seq≥2^63 拒 /
 *      未 HELLO 的业务帧丢 / 令牌不符拒 / 重复 HELLO 拒 / PING(relay⇒PONG, peer⇒转发) /
 *      CLOSE 收口 / 一端 socket 断 ⇒ 会话收口(peer-tcp-gone) / 空闲收口 / HELLO 宽限断开
 *   ③ 背压：对端停读 ⇒ 中继暂停源读取，drain 后续读，**一帧不丢**
 *   ④ fail-closed：无令牌不装配
 * 全部走 127.0.0.1 临时端口，不碰生产 8790。 */
import net from 'node:net';
import assert from 'node:assert/strict';
import {
  createTcpRelay,
} from '../src/tcp-relay.js';
import {
  encodeFrame, decodeFrame, sealPayload, openPayload, T,
  HEADER_SIZE, TAG_SIZE, MAX_PAYLOAD_STREAM, RELAY_SEQ_BASE,
} from '../src/flash-core.js';

const TOKEN = 'unit-test-tcp-token';
const HOST = '127.0.0.1';
let passed = 0;
const failures = [];
function ok(cond, name, extra) {
  if (cond) { passed++; console.log(`  ✅ ${name}`); }
  else { failures.push(name); console.log(`  ❌ ${name}${extra ? ' — ' + extra : ''}`); }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const nonceOf = (sid, seq) => { const n = Buffer.alloc(12); n.writeUInt32BE(sid, 0); n.writeBigUInt64BE(BigInt(seq), 4); return n; };
function sealFrame(sid, key, type, seq, payload) {
  const hdr = encodeFrame({ type, session: sid, seq, payload, maxPayload: MAX_PAYLOAD_STREAM });
  return Buffer.concat([hdr.subarray(0, HEADER_SIZE), sealPayload(key, nonceOf(sid, seq), hdr.subarray(0, HEADER_SIZE), payload)]);
}

/** 测试用 TCP 端：自带流式分帧（与中继同规），inbox 存整帧 Buffer。 */
function tcpPeer(port) {
  const sock = net.connect(port, HOST);
  const inbox = [];
  let buf = Buffer.alloc(0);
  let closed = false;
  sock.on('data', (chunk) => {
    buf = buf.length ? Buffer.concat([buf, chunk]) : chunk;
    for (;;) {
      if (buf.length < HEADER_SIZE) break;
      const len = buf.readUInt16BE(24);
      const total = HEADER_SIZE + TAG_SIZE + len;
      if (buf.length < total) break;
      inbox.push(Buffer.from(buf.subarray(0, total)));
      buf = buf.subarray(total);
    }
  });
  sock.on('close', () => { closed = true; });
  sock.on('error', () => {});
  return {
    sock, inbox,
    get closed() { return closed; },
    send(buf) { sock.write(buf); },
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
    close() { try { sock.destroy(); } catch {} },
  };
}

/* ---------- 0. fail-closed ---------- */
console.log('【fail-closed】');
{
  const relay = createTcpRelay({ host: HOST, port: 0, token: '', idleMs: 1000, helloGraceMs: 1000,
    reapIntervalMs: 100, pushEvent: () => {} });
  ok(relay.configured === false, '无令牌 ⇒ 中继不装配');
  let threw = false;
  try { relay.start(); } catch { threw = true; }
  ok(threw, '无令牌 start() 直接抛错');
}

/* ---------- 1. 流式分帧（专用小会话，直接打字节） ---------- */
console.log('【流式分帧】');
{
  const relay = createTcpRelay({ host: HOST, port: 0, token: TOKEN, idleMs: 60000, helloGraceMs: 60000,
    reapIntervalMs: 60000, pushEvent: () => {} });
  const bound = await relay.start();
  const { sessionId, key } = relay.createSession({ uid: 's', viewerWs: null, sendToAgent: () => {}, sendToViewer: () => {} });
  const a = tcpPeer(bound.port), v = tcpPeer(bound.port);
  await sleep(50);
  a.send(sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TOKEN, role: 'agent' }), 'utf8')));
  await a.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  v.send(sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TOKEN, role: 'viewer' }), 'utf8')));
  await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);

  // ① 一帧切三瓣（半帧/续帧）
  const f1 = sealFrame(sessionId, key, T.DATA, 1, Buffer.from('split-frame', 'utf8'));
  a.send(f1.subarray(0, 10)); await sleep(30);
  a.send(f1.subarray(10, 30)); await sleep(30);
  a.send(f1.subarray(30));
  const g1 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.DATA);
  ok(!!g1 && g1.equals(f1), '一帧切三瓣 ⇒ 完整重组到达');

  // ② 两帧合一 chunk
  const f2 = sealFrame(sessionId, key, T.DATA, 2, Buffer.from('frame-A', 'utf8'));
  const f3 = sealFrame(sessionId, key, T.DATA, 3, Buffer.from('frame-B', 'utf8'));
  a.send(Buffer.concat([f2, f3]));
  const g2 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).seq === 2);
  const g3 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).seq === 3);
  ok(!!g2 && g2.equals(f2) && !!g3 && g3.equals(f3), '两帧合一 chunk ⇒ 逐帧切开');

  // ③ 空 payload
  const f4 = sealFrame(sessionId, key, T.DATA, 4, Buffer.alloc(0));
  a.send(f4);
  const g4 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).seq === 4);
  ok(!!g4 && g4.length === HEADER_SIZE + TAG_SIZE, '空 payload 帧回环（总长=26+16）');

  // ④ 最大 payload 65535
  const big = Buffer.alloc(65535, 0x5A);
  const f5 = sealFrame(sessionId, key, T.DATA, 5, big);
  a.send(f5);
  const g5 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).seq === 5, 3000);
  ok(!!g5 && g5.equals(f5), '最大 payload(65535) 原样中继');

  // ⑤ 极限半帧：声称最大 len(65535) 只到一半 ⇒ 连接存活、不投递、不崩；
  //    补全后完整到达（流式累积的正确行为）。⚠️ len 是 UInt16，65535 即字段上限，
  //    所以「len 越界即断」在协议封顶下不可达——中继里那句判断是封顶值被调小时的防御。
  const half = sealFrame(sessionId, key, T.DATA, 6, Buffer.alloc(65535, 0x11));
  a.send(half.subarray(0, HEADER_SIZE + TAG_SIZE + 30000));
  await sleep(200);
  ok(!a.closed, '极限半帧 ⇒ 连接存活（在等后续字节，不猜不丢）');
  a.send(half.subarray(HEADER_SIZE + TAG_SIZE + 30000));
  const g6 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).seq === 6, 3000);
  ok(!!g6 && g6.equals(half), '补全后极限帧完整到达');

  // ⑥ 纯垃圾流（无有效 magic）⇒ 连接存活、帧被丢、中继照样服务别的会话。
  //    注意：垃圾要先凑满它自己声称的帧长才会被消费判死（len=0x4141=16705）。
  const junk = Buffer.alloc(HEADER_SIZE + TAG_SIZE + 16705 + 10, 0x41);
  a.send(junk);
  await sleep(200);
  ok(!a.closed, '垃圾流不炸连接');
  const st = relay.stats();
  ok(st.drops > 0, `垃圾帧计入 drops（drops=${st.drops}）`);
  ok(st.sessions === 1, '中继仍持有原会话（未被垃圾流带崩）');

  a.close(); v.close(); relay.stop();
}

/* ---------- 2. 回环全链路 ---------- */
console.log('【回环全链路】');
{
  const events = [];
  const relay = createTcpRelay({ host: HOST, port: 0, token: TOKEN, idleMs: 60000, helloGraceMs: 60000,
    reapIntervalMs: 60000, pushEvent: (lv, msg, d) => events.push({ lv, msg, d }) });
  const bound = await relay.start();
  const wsMsgs = { agent: [], viewer: [] };
  const { sessionId, key } = relay.createSession({
    uid: 'dev-1', viewerKey: 'vk-1',
    sendToAgent: (m) => wsMsgs.agent.push(m),
    sendToViewer: (m) => wsMsgs.viewer.push(m),
  });
  const a = tcpPeer(bound.port), v = tcpPeer(bound.port);
  await sleep(50);
  const hello = (role) => sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TOKEN, role }), 'utf8'));

  a.send(hello('agent'));
  const ackA = await a.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  ok(!!ackA, 'agent HELLO ⇒ HELLO_ACK');
  if (ackA) {
    const f = decodeFrame(ackA, MAX_PAYLOAD_STREAM);
    const body = JSON.parse(openPayload(key, nonceOf(sessionId, f.seq), f.aad, f.tag, f.payload).toString());
    ok(f.seq >= RELAY_SEQ_BASE && body.peersReady === false, '首端 ACK：中继 seq 空间 + 对端未就绪');
  }
  v.send(hello('viewer'));
  const ackB = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  ok(!!ackB, 'viewer HELLO ⇒ HELLO_ACK');
  ok(events.some((e) => e.msg.includes('两端就绪')), '双端就绪事件');

  // DATA 双向原样
  const d1 = sealFrame(sessionId, key, T.DATA, 1, Buffer.from('agent-to-viewer', 'utf8'));
  a.send(d1);
  const r1 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.DATA);
  ok(!!r1 && r1.equals(d1), 'agent→viewer DATA 原样');
  const d2 = sealFrame(sessionId, key, T.DATA, 1, Buffer.from('viewer-to-agent', 'utf8'));
  v.send(d2);
  const r2 = await a.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.DATA);
  ok(!!r2 && r2.equals(d2), 'viewer→agent DATA 原样（输入回传通路）');

  // 严格单调：同 seq 再来一帧 ⇒ 拒（TCP 有序可靠，不需要应用层重传）
  const before = relay.stats().perSession[0].drops;
  a.send(d1);
  const none = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.DATA, 300);
  ok(none === null, '同 seq 重传被拒（TCP 中继严格单调）');
  ok(relay.stats().perSession[0].drops > before, '重放帧计入 drops');

  // seq ≥ 2^63（中继空间）⇒ 拒
  a.send(sealFrame(sessionId, key, T.DATA, RELAY_SEQ_BASE, Buffer.from('evil', 'utf8')));
  const none2 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.DATA, 300);
  ok(none2 === null, '端侧 seq ≥2^63 ⇒ 拒收');

  // 未 HELLO 的连接发业务帧 ⇒ 丢
  const c = tcpPeer(bound.port);
  await sleep(30);
  c.send(sealFrame(sessionId, key, T.DATA, 1, Buffer.from('no-hello', 'utf8')));
  const none3 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.DATA, 300);
  ok(none3 === null, '未 HELLO 的连接发业务帧 ⇒ 丢');
  c.close();

  // 令牌不符 ⇒ 无 ACK
  const c2 = tcpPeer(bound.port);
  await sleep(30);
  c2.send(sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: 'wrong', role: 'agent' }), 'utf8')));
  const noAck = await c2.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK, 400);
  ok(noAck === null, '令牌不符 ⇒ 不给 HELLO_ACK');
  ok(events.some((e) => e.msg.includes('令牌不符')), '令牌不符已告警');
  c2.close();

  // 重复 HELLO（同角色第二个 socket）⇒ 拒，原会话不动
  const c3 = tcpPeer(bound.port);
  await sleep(30);
  c3.send(hello('agent'));
  const noAck2 = await c3.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK, 400);
  ok(noAck2 === null, '重复 HELLO（同角色）⇒ 拒');
  ok(events.some((e) => e.msg.includes('重复 HELLO')), '重复 HELLO 已告警');
  const still = sealFrame(sessionId, key, T.DATA, 2, Buffer.from('still-alive', 'utf8'));
  a.send(still);
  const r3 = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).seq === 2);
  ok(!!r3 && r3.equals(still), '原会话不受重复 HELLO 影响');
  c3.close();

  // PING：to=relay ⇒ PONG；to=peer ⇒ 转发
  v.send(sealFrame(sessionId, key, T.PING, 2, Buffer.from(JSON.stringify({ to: 'relay', sentTs: Date.now() }), 'utf8')));
  const pong = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.PONG);
  ok(!!pong, 'PING(to=relay) ⇒ PONG');
  if (pong) {
    const f = decodeFrame(pong, MAX_PAYLOAD_STREAM);
    const body = JSON.parse(openPayload(key, nonceOf(sessionId, f.seq), f.aad, f.tag, f.payload).toString());
    ok(f.seq >= RELAY_SEQ_BASE && typeof body.relayTs === 'number', 'PONG 走中继 seq 空间 + 两跳时间戳');
  }
  v.send(sealFrame(sessionId, key, T.PING, 3, Buffer.from(JSON.stringify({ to: 'peer', sentTs: Date.now() }), 'utf8')));
  const pingAtA = await a.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.PING);
  ok(!!pingAtA, 'PING(to=peer) ⇒ 转发给对端（两端互测 RTT）');

  // CLOSE ⇒ 对端收到 CLOSE 帧 + 会话收口 + 双端 WS closed + socket 断开
  a.send(sealFrame(sessionId, key, T.CLOSE, 4, Buffer.from(JSON.stringify({ reason: 'done' }), 'utf8')));
  const closeAtV = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.CLOSE);
  ok(!!closeAtV, 'CLOSE 先转发给对端');
  await sleep(200);
  ok(relay.sessionCount() === 0, 'CLOSE ⇒ 会话收口');
  ok(wsMsgs.agent.some((m) => m.action === 'closed') && wsMsgs.viewer.some((m) => m.action === 'closed'),
    '两端 WS 均收到 closed 通知');
  ok(v.closed || a.closed, '收口 ⇒ socket 被断开');

  a.close(); v.close(); relay.stop();
}

/* ---------- 3. 一端 socket 断 ⇒ 会话收口 ---------- */
console.log('【断线收口】');
{
  const wsMsgs = { agent: [], viewer: [] };
  const relay = createTcpRelay({ host: HOST, port: 0, token: TOKEN, idleMs: 60000, helloGraceMs: 60000,
    reapIntervalMs: 60000, pushEvent: () => {} });
  const bound = await relay.start();
  const { sessionId, key } = relay.createSession({
    uid: 'dev-2', viewerKey: 'vk-2',
    sendToAgent: (m) => wsMsgs.agent.push(m),
    sendToViewer: (m) => wsMsgs.viewer.push(m),
  });
  const a = tcpPeer(bound.port), v = tcpPeer(bound.port);
  await sleep(50);
  a.send(sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TOKEN, role: 'agent' }), 'utf8')));
  await a.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  v.send(sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TOKEN, role: 'viewer' }), 'utf8')));
  await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  v.close();   // 管理端直接走，不发 CLOSE
  await sleep(300);
  ok(relay.sessionCount() === 0, '一端 TCP 断 ⇒ 会话收口（peer-tcp-gone）');
  ok(wsMsgs.agent.some((m) => m.action === 'closed' && m.reason === 'peer-tcp-gone'),
    '对端收到 closed（reason=peer-tcp-gone，明确重协或回落）');
  ok(a.closed, '对端 socket 也被收口断开');
  a.close(); relay.stop();
}

/* ---------- 4. 空闲收口 + HELLO 宽限 ---------- */
console.log('【空闲/宽限】');
{
  const relay = createTcpRelay({ host: HOST, port: 0, token: TOKEN, idleMs: 250, helloGraceMs: 200,
    reapIntervalMs: 100, pushEvent: () => {} });
  const bound = await relay.start();
  // 久不 HELLO 的连接被断
  const idle1 = tcpPeer(bound.port);
  const closedFast = await new Promise((resolve) => {
    idle1.sock.on('close', () => resolve(true));
    setTimeout(() => resolve(false), 800);
  });
  ok(closedFast, '久不 HELLO 的连接按宽限断开');
  idle1.close();
  // 建了会话但无流量 ⇒ 空闲收口
  const { sessionId, key } = relay.createSession({ uid: 'dev-3', viewerKey: 'vk-3', sendToAgent: () => {}, sendToViewer: () => {} });
  const a = tcpPeer(bound.port), v = tcpPeer(bound.port);
  await sleep(30);
  a.send(sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TOKEN, role: 'agent' }), 'utf8')));
  await a.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  v.send(sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TOKEN, role: 'viewer' }), 'utf8')));
  await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  ok(relay.sessionCount() === 1, '会话就绪');
  await sleep(600);
  ok(relay.sessionCount() === 0, '无流量 ⇒ 空闲收口');
  a.close(); v.close(); relay.stop();
}

/* ---------- 5. 背压不丢帧 ---------- */
console.log('【背压】');
{
  const relay = createTcpRelay({ host: HOST, port: 0, token: TOKEN, idleMs: 60000, helloGraceMs: 60000,
    reapIntervalMs: 60000, pushEvent: () => {} });
  const bound = await relay.start();
  const { sessionId, key } = relay.createSession({ uid: 'dev-4', viewerKey: 'vk-4', sendToAgent: () => {}, sendToViewer: () => {} });
  const a = tcpPeer(bound.port), v = tcpPeer(bound.port);
  await sleep(50);
  a.send(sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TOKEN, role: 'agent' }), 'utf8')));
  await a.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  v.send(sealFrame(sessionId, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TOKEN, role: 'viewer' }), 'utf8')));
  await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);

  // 管理端停读 ⇒ 中继写缓冲堆积 ⇒ 应暂停被控端读取而不是丢帧
  v.sock.pause();
  const N = 96;
  const payload = Buffer.alloc(60000, 0x33);
  for (let i = 1; i <= N; i++) {
    a.send(sealFrame(sessionId, key, T.DATA, i, payload));
  }
  await sleep(400);   // 让缓冲堆起来
  const st1 = relay.stats();
  console.log(`  ℹ 停读期间中继已转出 ${st1.framesOut} 帧，背压暂停 ${st1.pauses} 次`);
  ok(st1.pauses > 0, '对端停读 ⇒ 中继真的暂停了源读取（不是靠用户态缓冲硬扛）');
  v.sock.resume();
  const t0 = Date.now();
  let got = 0;
  while (got < N && Date.now() - t0 < 15000) {
    const f = await v.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.DATA, 2000);
    if (f) got++; else break;
  }
  ok(got === N, `背压下 ${N} 帧全部到达（一帧不丢），实收 ${got}`);
  a.close(); v.close(); relay.stop();
}

console.log(`\n══ 结果: ${passed} 通过 / ${failures.length} 失败 ══`);
if (failures.length) { console.log('失败项：', failures); process.exit(1); }
process.exit(0);
