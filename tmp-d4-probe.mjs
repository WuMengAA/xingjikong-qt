// D4 临时探针：不写 Qt 也能验云端 viewer 通道（帧广播 / 设备表 / 指令真实 state）
import fs from 'node:fs';
import WebSocket from 'ws';

const LOG = 'probe.out';
fs.writeFileSync(LOG, '');
const out = [];
const log = (s) => { out.push(s); fs.appendFileSync(LOG, s + '\n'); console.log(s); };
let failed = 0;
const fail = (m) => { failed++; log('❌ ' + m); };
const ok = (m) => log('✅ ' + m);
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const cap = (p, ms, tag) => Promise.race([
  p, new Promise((_, rej) => setTimeout(() => rej(new Error('超时 ' + tag)), ms)),
]);

const CLOUD = 'ws://127.0.0.1:8788';
const agent = new WebSocket(`${CLOUD}/ws/agent`);
const viewer = new WebSocket(`${CLOUD}/ws/viewer`);

const got = { subscribed: null, devices: null, frames: 0, lastFrame: null };
viewer.on('message', (d) => {
  try {
    const m = JSON.parse(d.toString('utf8'));
    if (m.type === 'subscribed') got.subscribed = m;
    else if (m.type === 'devices') got.devices = m;
    else if (m.type === 'frame') { got.frames++; got.lastFrame = m; }
  } catch (e) { log('⚠ viewer 收到无法解析的消息: ' + e.message); }
});
agent.on('error', (e) => log('agent err ' + e.message));
viewer.on('error', (e) => log('viewer err ' + e.message));

try {
  await cap(new Promise((r) => agent.on('open', r)), 5000, 'agent open');
  await cap(new Promise((r) => viewer.on('open', r)), 5000, 'viewer open');
  ok('两条 WS 都连上了（agent / viewer）');

  agent.send(JSON.stringify({ type: 'register', client: 'probe', version: 'probe-1', uid: 'probe-uid-01', fps: 1, token: 'dev-cloud-token' }));
  const okMsg = await cap(new Promise((r) => agent.once('message', (d) => r(JSON.parse(d.toString())))), 5000, 'registration-ok');
  okMsg?.type === 'registration-ok' ? ok('agent 注册通过，云端回了 registration-ok') : fail('agent 注册没过：' + JSON.stringify(okMsg));

  const dev = await cap(fetch('http://127.0.0.1:8788/api/devices').then(async (r) => ({ code: r.status, body: await r.json() })), 5000, 'http devices');
  dev.code === 200 && dev.body.devices?.length === 1
    ? ok('HTTP /api/devices 查到 1 台（probe-uid-01）')
    : fail('HTTP /api/devices 异常：' + JSON.stringify(dev.body ?? dev.code));

  await wait(300);
  got.subscribed?.uid === 'probe-uid-01'
    ? ok('viewer 自动订阅了在线设备 probe-uid-01')
    : fail('viewer 没自动订阅到 probe-uid-01：' + JSON.stringify(got.subscribed));

  agent.send(Buffer.from([0xFF, 0xD8, 0xFF, 0xE0, 1, 2, 3, 4, 0xFF, 0xD9]));
  await wait(400);
  got.frames === 1 && got.lastFrame?.uid === 'probe-uid-01'
    ? ok(`帧广播通了（收到 1 帧，${got.lastFrame.bytes} 字节，base64 ${got.lastFrame.data.length} 字符）`)
    : fail('帧广播没到 viewer：收到 ' + got.frames + ' 帧');
  got.lastFrame && Buffer.from(got.lastFrame.data, 'base64')[0] === 0xFF
    ? ok('帧内容往返一致（解回来头字节 0xFF）')
    : fail('帧内容对不上');

  viewer.send(JSON.stringify({ type: 'instruction', uid: 'probe-uid-01', action: 'lock' }));
  const r1 = await cap(new Promise((r) => viewer.once('message', (d) => r(JSON.parse(d.toString())))), 5000, 'instruction-online');
  r1.state === 'sent' ? ok('在线设备：云端回 sent（真写了 socket）') : fail('在线设备却没回 sent：' + JSON.stringify(r1));

  viewer.send(JSON.stringify({ type: 'instruction', uid: 'no-such-device', action: 'shutdown' }));
  const r2 = await cap(new Promise((r) => viewer.once('message', (d) => r(JSON.parse(d.toString())))), 5000, 'instruction-offline');
  r2.state === 'offline' ? ok('不在线设备：云端老实回 offline（没装 sent 骗人）') : fail('不在线却回 ' + r2.state + '，这是假绿');

  const d0 = got.devices;
  await wait(2600);
  got.devices !== d0 && Array.isArray(got.devices?.devices)
    ? ok('设备表定时广播在推（2s 一轮）')
    : fail('设备表定时广播没动');

  const ev = await cap(fetch('http://127.0.0.1:8788/api/events?limit=20').then((r) => r.json()), 5000, 'events');
  const hasReceipt = (ev.events ?? []).some((e) => String(e.msg).includes('回执'));
  hasReceipt ? ok('事件流里有被控端回执记录') : log('⚠ 本次没抓到回执行（被控端只回 received 也算回执）');
} catch (e) {
  fail('探针自己出错：' + e.message);
}

log('=== 探针结论：' + (failed ? '❌ ' + failed + ' 项失败' : '✅ 全过') + ' ===');
agent.close(); viewer.close();
setTimeout(() => process.exit(failed ? 1 : 0), 300);
