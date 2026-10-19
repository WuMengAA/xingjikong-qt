/* UDP 闪传会话 · 接线回归测试（2026-10-20 · U1）
 *
 * 覆盖 index.js 接线层（单测 unit-udp-relay.mjs 不管这些）：
 *   ① WS「udp-session open」→ 云端建会话 → 管理端/被控端**双端都拿到** sessionId+密钥
 *   ② 两端真 UDP HELLO 握手（密钥只有 WS 协商过的人会算）⇒ HELLO_ACK
 *   ③ agent→viewer DATA 经中继原样到达；PING(to=relay)⇒PONG
 *   ④ 管理端主动 close ⇒ 双端 WS 收 closed，且随后 UDP 帧被弃（会话真没了）
 *   ⑤ 管理端 WS 直接断 ⇒ 会话自动收口（防泄漏——远控会话泄漏那次同一病）
 *
 * 用法（先起隔离云端，再用本脚本联测）：
 *   CLOUD_WS_PORT=8096 CLOUD_WS_HOST=127.0.0.1 \
 *   CLOUD_WS_TOKEN=dev-cloud-token CLOUD_VIEWER_TOKEN=dev-viewer-token \
 *   UDP_RELAY_PORT=8097 UDP_RELAY_HOST=127.0.0.1 UDP_RELAY_TOKEN=dev-udp-token \
 *   CLOUD_EVENTS_FILE=$TMP/ev.log CLOUD_QUEUE_DB_FILE=$TMP/q.db CLOUD_INTERACT_DB_FILE=$TMP/i.db \
 *   node src/index.js
 *   node tests/regression-udp-session.mjs [wsPort=8096] [udpPort=8097] [uid]
 * 返回码：0 全过；非 0 有失败。脚本只读，不发破坏性指令，全走 127.0.0.1 临时端口。
 */
import { createSocket } from 'node:dgram';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { encodeFrame, decodeFrame, sealPayload, openPayload, T } from '../src/udp-relay.js';

const require = createRequire(import.meta.url);
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const WebSocket = require(path.join(repo, 'node_modules/ws/index.js')).default
  || require(path.join(repo, 'node_modules/ws/index.js'));

const WSPORT = Number(process.argv[2] || 8096);
const UDPPORT = Number(process.argv[3] || 8097);
const UID = String(process.argv[4] || 'TEST_UDPS' + Math.floor(Math.random() * 900 + 100));
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';
const UDP_TOKEN = process.env.UDP_RELAY_TOKEN || 'dev-udp-token';

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  ✅ ${name}${detail ? ' — ' + detail : ''}`); }
  else { fail++; console.log(`  ❌ ${name}${detail ? ' — ' + detail : ''}`); }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const nonceOf = (sid, seq) => { const n = Buffer.alloc(12); n.writeUInt32BE(sid, 0); n.writeBigUInt64BE(BigInt(seq), 4); return n; };
function sealFrame(sid, key, type, seq, payload) {
  const hdr = encodeFrame({ type, session: sid, seq, payload });
  return Buffer.concat([hdr.subarray(0, 26), sealPayload(key, nonceOf(sid, seq), hdr.subarray(0, 26), payload)]);
}

function udpPeer() {
  const s = createSocket('udp4');
  s.bind(0, HOST);
  const inbox = [];
  s.on('message', (m) => inbox.push(m));
  return {
    send(buf) { s.send(buf, UDPPORT, HOST); },
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
    close() { try { s.close(); } catch {} },
  };
}

const agent = new WebSocket(`ws://${HOST}:${WSPORT}/ws/agent`);
const viewer = new WebSocket(`ws://${HOST}:${WSPORT}/ws/viewer`);
const agentInbox = [];
const viewerInbox = [];
agent.on('message', (raw) => { try { agentInbox.push(JSON.parse(raw.toString())); } catch {} });
viewer.on('message', (raw) => { try { viewerInbox.push(JSON.parse(raw.toString())); } catch {} });

const guard = setTimeout(() => { console.error('回归测试总超时'); process.exit(1); }, 20000);

await new Promise((resolve, reject) => {
  let n = 0;
  const done = () => { if (++n === 2) resolve(); };
  agent.on('open', () => {
    agent.send(JSON.stringify({ v: 1, type: 'register', id: 'a1',
      payload: { uid: UID, token: AGENT_TOKEN, fp: 'udp-reg', version: '9.9.9', actions: [] } }));
    done();
  });
  viewer.on('open', () => {
    viewer.send(JSON.stringify({ v: 1, type: 'auth', id: 'v1', payload: { token: VIEWER_TOKEN } }));
    done();
  });
  agent.on('error', reject); viewer.on('error', reject);
});
await sleep(300);

// v1 信封注册回包是 'registered'（旧扁平格式才是 'registration-ok'，见 index.js:1900）
check('被控端注册成功', agentInbox.some((m) => (m.type === 'registered' || m.type === 'registration-ok') && (m.uid === UID || m.payload?.uid === UID)));
check('管理端鉴权就绪', viewerInbox.some((m) => m.type === 'viewer-ready'));

/* ① 协商：管理端发起 open */
viewer.send(JSON.stringify({ v: 1, type: 'udp-session', id: 'v2', payload: { action: 'open', uid: UID } }));
await sleep(400);
const vOpened = viewerInbox.find((m) => m.type === 'udp-session' && m.payload?.action === 'opened');
const aOpened = agentInbox.find((m) => m.type === 'udp-session' && m.payload?.action === 'opened');
check('管理端收到 opened（带 sessionId+密钥+udp 地址）',
  !!vOpened && !!vOpened.payload.key && !!vOpened.payload.udp && vOpened.payload.role === 'viewer',
  vOpened ? `sid=${vOpened.payload.sessionId} udp=${vOpened.payload.udp.host}:${vOpened.payload.udp.port}` : '未收到');
check('被控端也收到 opened（同一 sessionId+同一密钥）',
  !!aOpened && aOpened.payload.sessionId === vOpened?.payload.sessionId
  && aOpened.payload.key === vOpened?.payload.key && aOpened.payload.role === 'agent',
  aOpened ? `sid=${aOpened.payload.sessionId}` : '未收到');
check('opened 带的 UDP 端口与中继一致', vOpened?.payload?.udp?.port === UDPPORT);

if (!vOpened || !aOpened) {
  console.error('\n══ 协商失败，后续步骤无法继续 ══');
  clearTimeout(guard); process.exit(1);
}
const sid = vOpened.payload.sessionId;
const key = Buffer.from(vOpened.payload.key, 'base64');
const peerA = udpPeer();   // 演被控端
const peerB = udpPeer();   // 演管理端
await sleep(50);

/* ② 双端 HELLO 握手 */
peerA.send(sealFrame(sid, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: UDP_TOKEN, role: 'agent' }), 'utf8')));
const ackA = await peerA.wait((m) => decodeFrame(m).type === T.HELLO_ACK);
check('被控端 HELLO ⇒ HELLO_ACK', !!ackA);
peerB.send(sealFrame(sid, key, T.HELLO, 0, Buffer.from(JSON.stringify({ token: UDP_TOKEN, role: 'viewer' }), 'utf8')));
const ackB = await peerB.wait((m) => decodeFrame(m).type === T.HELLO_ACK);
check('管理端 HELLO ⇒ HELLO_ACK', !!ackB);
if (ackB) {
  const f = decodeFrame(ackB);
  const pt = openPayload(key, nonceOf(sid, f.seq), f.aad, f.tag, f.payload);
  const body = pt && JSON.parse(pt.toString());
  check('HELLO_ACK 报双端就绪', body?.peersReady === true);
}

/* ③ DATA 中转 + PING/PONG */
const f1 = sealFrame(sid, key, T.DATA, 1, Buffer.from('rfb-frame-bytes', 'utf8'));
peerA.send(f1);
const gotB = await peerB.wait((m) => decodeFrame(m).type === T.DATA);
check('agent→viewer DATA 原样到达', !!gotB && gotB.equals(f1));

peerB.send(sealFrame(sid, key, T.PING, 1, Buffer.from(JSON.stringify({ to: 'relay', sentTs: Date.now() }), 'utf8')));
const pong = await peerB.wait((m) => decodeFrame(m).type === T.PONG);
check('PING(to=relay) ⇒ PONG', !!pong);

/* ④ 管理端主动 close */
viewer.send(JSON.stringify({ v: 1, type: 'udp-session', id: 'v3', payload: { action: 'close', sessionId: sid } }));
await sleep(300);
check('管理端收到 closed 回执', viewerInbox.some((m) => m.type === 'udp-session' && m.payload?.action === 'closed' && m.payload?.sessionId === sid));
check('被控端也收到 closed 通知', agentInbox.some((m) => m.type === 'udp-session' && m.payload?.action === 'closed' && m.payload?.sessionId === sid));
peerA.send(sealFrame(sid, key, T.DATA, 2, Buffer.from('after-close', 'utf8')));
const afterClose = await peerB.wait((m) => decodeFrame(m).type === T.DATA, 400);
check('会话关闭后的 UDP 帧被弃（不幽灵转发）', afterClose === null);

/* ⑤ 管理端断线 ⇒ 会话自动收口（防泄漏） */
viewer.send(JSON.stringify({ v: 1, type: 'udp-session', id: 'v4', payload: { action: 'open', uid: UID } }));
await sleep(400);
const v2 = viewerInbox.find((m) => m.type === 'udp-session' && m.payload?.action === 'opened' && m.payload.sessionId !== sid);
check('第二个会话协商成功', !!v2);
if (v2) {
  const before = agentInbox.length;
  viewer.close();   // 管理端直接走，不发 close
  await sleep(400);
  const closedAfter = agentInbox.slice(before).some((m) => m.type === 'udp-session' && m.payload?.action === 'closed' && m.payload.sessionId === v2.payload.sessionId);
  check('管理端断线 ⇒ 被控端收到 closed（会话不泄漏）', closedAfter);
}

peerA.close(); peerB.close();
try { agent.close(); } catch {}
clearTimeout(guard);
console.log(`\n══ 结果: ${pass} 通过 / ${fail} 失败 ══`);
process.exit(fail ? 1 : 0);
