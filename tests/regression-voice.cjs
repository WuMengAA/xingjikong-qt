// 语音对讲链路回归测试（2026-10-07 固化）
//
// 作用：验证管理端 startSpeaking → 云端进入语音态 + 通知在线被控端 →
//       管理端推 PCM 帧 → 被控端收到 audio/pcm v1 二进制帧 → stop 后帧被拒。
// 覆盖 1ecf3c7 的语音核心路径（与 regression-broadcast 对称，事件驱动）。
//
// 用法（先起隔离云端，再联测）：
//   CLOUD_WS_PORT=8095 ... node src/index.js
//   node tests/regression-voice.cjs [port] [uid]
// 返回码：0 全过；非 0 有失败。

const path = require('path');
const repo = path.resolve(__dirname, '..');
const WebSocket = require(path.join(repo, 'node_modules/ws/index.js')).default
  || require(path.join(repo, 'node_modules/ws/index.js'));

const PORT = Number(process.argv[2] || 8095);
const UID = String(process.argv[3] || 'VC' + Math.floor(Math.random() * 900 + 100));
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  \u2705 ${name}${detail ? ' \u2014 ' + detail : ''}`); }
  else { fail++; console.log(`  \u274c ${name}${detail ? ' \u2014 ' + detail : ''}`); }
}
function waitFor(fn, timeoutMs = 6000) {
  return new Promise((resolve) => {
    const t0 = Date.now();
    const iv = setInterval(() => {
      const v = fn();
      if (v) { clearInterval(iv); resolve(v); }
      else if (Date.now() - t0 > timeoutMs) { clearInterval(iv); resolve(null); }
    }, 100);
  });
}

(async () => {
  // ── 被控端 ──
  const agentMsgs = [];
  let agentAudio = null;
  const agent = new WebSocket(`ws://${HOST}:${PORT}/ws/agent`);
  agent.on('open', () => {
    agent.send(JSON.stringify({
      v: 1, type: 'register', id: 'a1',
      payload: { uid: UID, token: AGENT_TOKEN, fp: 'vc-test', version: '9.9.9', actions: ['notify'] },
    }));
  });
  agent.on('message', (raw, isBinary) => {
    if (isBinary) agentAudio = Buffer.from(raw);
    else { try { agentMsgs.push(JSON.parse(raw.toString())); } catch {} }
  });

  // ── 管理端 ──
  const viewerMsgs = [];
  const viewer = new WebSocket(`ws://${HOST}:${PORT}/ws/viewer`);
  viewer.on('open', () => {
    viewer.send(JSON.stringify({ v: 1, type: 'auth', id: 'v1', payload: { token: VIEWER_TOKEN } }));
  });
  viewer.on('message', (raw) => { try { viewerMsgs.push(JSON.parse(raw.toString())); } catch {} });

  // 1) 被控端注册成功
  const reg = await waitFor(() =>
    agentMsgs.find((m) => m.type === 'registered' || m.type === 'registration-ok'));
  check('被控端注册成功', !!reg, UID);

  // 2) 管理端鉴权通过
  const authed = await waitFor(() => viewerMsgs.find((m) => m.type === 'viewer-ready' || m.type === 'authed'));
  check('管理端鉴权通过', !!authed);

  // 3) 管理端开语音
  viewer.send(JSON.stringify({ v: 1, type: 'audio.start', id: 'v2', payload: {} }));
  const started = await waitFor(() => viewerMsgs.find((m) => m.type === 'audio.started'));
  check('语音 start 被接受（audio.started）', !!started, started?.payload?.targets);

  // 4) 被控端收到 audio.start 通知（学生端"老师正在讲话"提示）
  const notice = await waitFor(() => agentMsgs.find((m) => m.type === 'audio.start'));
  check('被控端收到 audio.start 通知', !!notice);

  // 5) 管理端推 PCM 帧 → 被控端收到 audio/pcm v1 二进制帧
  viewer.send(Buffer.alloc(640, 0xCC), { binary: true });
  const frame = await waitFor(() => agentAudio);
  check('被控端收到语音二进制帧', !!frame && frame.length > 640, `bytes=${frame?.length}`);
  check('帧含 v1 帧头（PCM 负载 ≥640 不截）', !!frame && frame.length >= 640 + 4, `bytes=${frame?.length}`);

  // 6) 管理端停语音
  viewer.send(JSON.stringify({ v: 1, type: 'audio.stop', id: 'v3', payload: {} }));
  const stopped = await waitFor(() => viewerMsgs.find((m) => m.type === 'audio.stopped'));
  check('语音 stop 被接受（audio.stopped）', !!stopped);

  // 7) stop 后暂停帧 → 云端丢弃（不广播）
  agentAudio = null;
  viewer.send(Buffer.alloc(100, 0x11), { binary: true });
  await new Promise((r) => setTimeout(r, 1200));
  check('stop 后帧被丢弃（被控端无新帧）', agentAudio === null, agentAudio ? `意外收到 ${agentAudio.length}B` : '无');

  agent.close(); viewer.close();
  console.log(`\n\u2550\u2550 \u7ed3\u679c: ${pass} \u901a\u8fc7 / ${fail} \u5931\u8d25 \u2550\u2550`);
  process.exit(fail === 0 ? 0 : 1);
})();