/* TCP 闪传会话 · 接线回归测试（2026-10-20 · TCP 主轴定稿后）
 *
 * 覆盖 index.js 接线层（单测 unit-tcp-relay.mjs 不管这些）：
 *   ① WS「tcp-session open」→ 云端建会话 → 管理端/被控端**双端都拿到** sessionId+密钥+tcp 地址
 *   ② 两端真 TCP 连接 + HELLO 握手（密钥只有 WS 协商过的人会算）⇒ HELLO_ACK
 *   ③ agent→viewer DATA 经中继原样到达；PING(to=relay)⇒PONG
 *   ④ 管理端主动 close ⇒ 双端 WS 收 closed，且 TCP 连接被收口断开
 *   ⑤ 管理端 WS 直接断 ⇒ 会话自动收口（防泄漏——远控会话泄漏那次同一病）
 *   ⑥ 被控端 WS 直接断 ⇒ 会话同样收口（TCP 中专有：一端断即收口）
 *
 * 用法（先起隔离云端，再用本脚本联测）：
 *   CLOUD_WS_PORT=18096 CLOUD_WS_HOST=127.0.0.1 \
 *   CLOUD_WS_TOKEN=dev-cloud-token CLOUD_VIEWER_TOKEN=dev-viewer-token \
 *   TCP_RELAY_PORT=18098 TCP_RELAY_HOST=127.0.0.1 TCP_RELAY_TOKEN=dev-tcp-token \
 *   CLOUD_EVENTS_FILE=$TMP/ev.log CLOUD_QUEUE_DB_FILE=$TMP/q.db CLOUD_INTERACT_DB_FILE=$TMP/i.db \
 *   node src/index.js
 *   node tests/regression-tcp-session.mjs [wsPort=18096] [tcpPort=18098] [uid]
 * 返回码：0 全过；非 0 有失败。脚本只读，不发破坏性指令，全走 127.0.0.1 临时端口。
 */
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { encodeFrame, decodeFrame, sealPayload, openPayload, T,
  HEADER_SIZE, TAG_SIZE, MAX_PAYLOAD_STREAM } from '../src/flash-core.js';

const require = createRequire(import.meta.url);
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const WebSocket = require(path.join(repo, 'node_modules/ws/index.js')).default
  || require(path.join(repo, 'node_modules/ws/index.js'));

const WSPORT = Number(process.argv[2] || 18096);
const TCPPORT = Number(process.argv[3] || 18098);
const UID = String(process.argv[4] || 'TEST_TCPS' + Math.floor(Math.random() * 900 + 100));
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';
const TCP_TOKEN = process.env.TCP_RELAY_TOKEN || 'dev-tcp-token';

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  ✅ ${name}${detail ? ' — ' + detail : ''}`); }
  else { fail++; console.log(`  ❌ ${name}${detail ? ' — ' + detail : ''}`); }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const nonceOf = (sid, seq) => { const n = Buffer.alloc(12); n.writeUInt32BE(sid, 0); n.writeBigUInt64BE(BigInt(seq), 4); return n; };
function sealFrame(sid, key, type, seq, payload) {
  const hdr = encodeFrame({ type, session: sid, seq, payload, maxPayload: MAX_PAYLOAD_STREAM });
  return Buffer.concat([hdr.subarray(0, HEADER_SIZE), sealPayload(key, nonceOf(sid, seq), hdr.subarray(0, HEADER_SIZE), payload)]);
}

/** 测试用 TCP 端（演被控端/管理端）：自带流式分帧，inbox 存整帧。 */
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
    wait(pred, ms = 2000) {
      return new Promise((resolve) => {
        const t0 = Date.now();
        const tick = () => {
          const i = inbox.findIndex(pred);
          if (i >= 0) { resolve(inbox.splice(i, 1)[0]); return; }
          if (Date.now() - t0 > ms) { resolve(null); return; }
          setTimeout(tick, 20);
        };
        tick();
      });
    },
    close() { try { sock.destroy(); } catch {} },
  };
}

const agent = new WebSocket(`ws://${HOST}:${WSPORT}/ws/agent`);
const viewer = new WebSocket(`ws://${HOST}:${WSPORT}/ws/viewer`);
const agentInbox = [];
const viewerInbox = [];
agent.on('message', (raw) => { try { agentInbox.push(JSON.parse(raw.toString())); } catch {} });
viewer.on('message', (raw) => { try { viewerInbox.push(JSON.parse(raw.toString())); } catch {} });

const guard = setTimeout(() => { console.error('回归测试总超时'); process.exit(1); }, 30000);

await new Promise((resolve, reject) => {
  let n = 0;
  const done = () => { if (++n === 2) resolve(); };
  agent.on('open', () => {
    agent.send(JSON.stringify({ v: 1, type: 'register', id: 'a1',
      payload: { uid: UID, token: AGENT_TOKEN, fp: 'tcp-reg', version: '9.9.9', actions: [] } }));
    done();
  });
  viewer.on('open', () => {
    viewer.send(JSON.stringify({ v: 1, type: 'auth', id: 'v1', payload: { token: VIEWER_TOKEN } }));
    done();
  });
  agent.on('error', reject); viewer.on('error', reject);
});
await sleep(300);

// v1 信封注册回包是 'registered'（旧扁平格式才是 'registration-ok'）
check('被控端注册成功', agentInbox.some((m) => (m.type === 'registered' || m.type === 'registration-ok') && (m.uid === UID || m.payload?.uid === UID)));
check('管理端鉴权就绪', viewerInbox.some((m) => m.type === 'viewer-ready'));

/* ① 协商：管理端发起 open */
viewer.send(JSON.stringify({ v: 1, type: 'tcp-session', id: 'v2', payload: { action: 'open', uid: UID } }));
await sleep(400);
const vOpened = viewerInbox.find((m) => m.type === 'tcp-session' && m.payload?.action === 'opened');
const aOpened = agentInbox.find((m) => m.type === 'tcp-session' && m.payload?.action === 'opened');
check('管理端收到 opened（带 sessionId+密钥+tcp 地址）',
  !!vOpened && !!vOpened.payload.key && !!vOpened.payload.tcp && vOpened.payload.role === 'viewer',
  vOpened ? `sid=${vOpened.payload.sessionId} tcp=${vOpened.payload.tcp.host}:${vOpened.payload.tcp.port}` : '未收到');
check('被控端也收到 opened（同一 sessionId+同一密钥）',
  !!aOpened && aOpened.payload.sessionId === vOpened?.payload.sessionId
  && aOpened.payload.key === vOpened?.payload.key && aOpened.payload.role === 'agent',
  aOpened ? `sid=${aOpened.payload.sessionId}` : '未收到');
check('opened 带的 TCP 端口与中继一致', vOpened?.payload?.tcp?.port === TCPPORT);

if (!vOpened || !aOpened) {
  console.error('\n══ 协商失败，后续步骤无法继续 ══');
  clearTimeout(guard); process.exit(1);
}
const sid = vOpened.payload.sessionId;
const key = Buffer.from(vOpened.payload.key, 'base64');
const peerA = tcpPeer(TCPPORT);   // 演被控端
const peerB = tcpPeer(TCPPORT);   // 演管理端
await sleep(50);

/* ② 双端 TCP HELLO 握手 */
peerA.send(sealFrame(sid, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TCP_TOKEN, role: 'agent' }), 'utf8')));
const ackA = await peerA.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
check('被控端 TCP HELLO ⇒ HELLO_ACK', !!ackA);
peerB.send(sealFrame(sid, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TCP_TOKEN, role: 'viewer' }), 'utf8')));
const ackB = await peerB.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
check('管理端 TCP HELLO ⇒ HELLO_ACK', !!ackB);
if (ackB) {
  const f = decodeFrame(ackB, MAX_PAYLOAD_STREAM);
  const pt = openPayload(key, nonceOf(sid, f.seq), f.aad, f.tag, f.payload);
  const body = pt && JSON.parse(pt.toString());
  check('HELLO_ACK 报双端就绪', body?.peersReady === true);
}

/* ③ DATA 中转 + PING/PONG */
const f1 = sealFrame(sid, key, T.DATA, 1, Buffer.from('rfb-frame-bytes', 'utf8'));
peerA.send(f1);
const gotB = await peerB.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.DATA);
check('agent→viewer DATA 原样到达', !!gotB && gotB.equals(f1));

peerB.send(sealFrame(sid, key, T.PING, 1, Buffer.from(JSON.stringify({ to: 'relay', sentTs: Date.now() }), 'utf8')));
const pong = await peerB.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.PONG);
check('PING(to=relay) ⇒ PONG', !!pong);

/* ④ 管理端主动 close */
viewer.send(JSON.stringify({ v: 1, type: 'tcp-session', id: 'v3', payload: { action: 'close', sessionId: sid } }));
await sleep(300);
check('管理端收到 closed 回执', viewerInbox.some((m) => m.type === 'tcp-session' && m.payload?.action === 'closed' && m.payload?.sessionId === sid));
check('被控端也收到 closed 通知', agentInbox.some((m) => m.type === 'tcp-session' && m.payload?.action === 'closed' && m.payload?.sessionId === sid));
await sleep(200);
check('收口 ⇒ 两端 TCP 连接被断开（不幽灵转发）', peerA.closed && peerB.closed);

/* ⑤ 管理端断线 ⇒ 会话自动收口（防泄漏） */
viewer.send(JSON.stringify({ v: 1, type: 'tcp-session', id: 'v4', payload: { action: 'open', uid: UID } }));
await sleep(400);
const v2 = viewerInbox.find((m) => m.type === 'tcp-session' && m.payload?.action === 'opened' && m.payload.sessionId !== sid);
check('第二个会话协商成功', !!v2);
if (v2) {
  const key2 = Buffer.from(v2.payload.key, 'base64');
  const sid2 = v2.payload.sessionId;
  const pA2 = tcpPeer(TCPPORT), pB2 = tcpPeer(TCPPORT);
  await sleep(50);
  pA2.send(sealFrame(sid2, key2, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TCP_TOKEN, role: 'agent' }), 'utf8')));
  await pA2.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  pB2.send(sealFrame(sid2, key2, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TCP_TOKEN, role: 'viewer' }), 'utf8')));
  await pB2.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  const before = agentInbox.length;
  viewer.close();   // 管理端直接走，不发 close
  await sleep(500);
  const closedAfter = agentInbox.slice(before).some((m) => m.type === 'tcp-session' && m.payload?.action === 'closed' && m.payload?.sessionId === sid2);
  check('管理端断线 ⇒ 被控端收到 closed（会话不泄漏）', closedAfter);
  await sleep(200);
  check('管理端断线 ⇒ 两端 TCP 连接被收口断开', pA2.closed && pB2.closed);
  pA2.close(); pB2.close();
}

/* ⑥ 被控端断线 ⇒ 会话同样收口（换一条新管理端连接——⑤ 里那条已关） */
const viewer2 = new WebSocket(`ws://${HOST}:${WSPORT}/ws/viewer`);
const viewer2Inbox = [];
viewer2.on('message', (raw) => { try { viewer2Inbox.push(JSON.parse(raw.toString())); } catch {} });
await new Promise((resolve, reject) => {
  viewer2.on('open', () => {
    viewer2.send(JSON.stringify({ v: 1, type: 'auth', id: 'v6', payload: { token: VIEWER_TOKEN } }));
    resolve();
  });
  viewer2.on('error', reject);
});
await sleep(300);
viewer2.send(JSON.stringify({ v: 1, type: 'tcp-session', id: 'v7', payload: { action: 'open', uid: UID } }));
await sleep(400);
const v3 = viewer2Inbox.find((m) => m.type === 'tcp-session' && m.payload?.action === 'opened');
check('第三个会话协商成功', !!v3);
if (v3) {
  const key3 = Buffer.from(v3.payload.key, 'base64');
  const sid3 = v3.payload.sessionId;
  const pA3 = tcpPeer(TCPPORT), pB3 = tcpPeer(TCPPORT);
  await sleep(50);
  pA3.send(sealFrame(sid3, key3, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TCP_TOKEN, role: 'agent' }), 'utf8')));
  await pA3.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  pB3.send(sealFrame(sid3, key3, T.HELLO, 0, Buffer.from(JSON.stringify({ token: TCP_TOKEN, role: 'viewer' }), 'utf8')));
  await pB3.wait((m) => decodeFrame(m, MAX_PAYLOAD_STREAM).type === T.HELLO_ACK);
  const before = viewer2Inbox.length;
  agent.close();   // 被控端直接走
  await sleep(500);
  const closedAfter = viewer2Inbox.slice(before).some((m) => m.type === 'tcp-session' && m.payload?.action === 'closed' && m.payload?.sessionId === sid3);
  check('被控端断线 ⇒ 管理端收到 closed', closedAfter);
  await sleep(200);
  check('被控端断线 ⇒ 两端 TCP 连接被收口断开', pA3.closed && pB3.closed);
  pA3.close(); pB3.close();
  try { viewer2.close(); } catch {}
}

peerA.close(); peerB.close();
try { viewer.close(); } catch {}
try { agent.close(); } catch {}
clearTimeout(guard);
console.log(`\n══ 结果: ${pass} 通过 / ${fail} 失败 ══`);
process.exit(fail ? 1 : 0);
