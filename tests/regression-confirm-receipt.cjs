// 通知确认回执链路回归测试（2026-10-07 固化，实测 20/20 通过）
//
// 作用：验证「通知下发(confirm+replies) → 被控端二次回执 confirmed → 云端
//       recordResult 正确放行（不记 failed）→ 管理端收到转发」整条链路。
// 背景：c1bd8f2 修复 recordResult 只放行 done/started 导致 confirmed 被误记 failed。
//
// 用法（先起隔离云端，再用本脚本联测）：
//   CLOUD_WS_PORT=8095 CLOUD_WS_HOST=127.0.0.1 CLOUD_WS_TOKEN=dev-cloud-token \
//   CLOUD_VIEWER_TOKEN=dev-viewer-token node src/index.js
//   node tests/regression-confirm-receipt.js [port] [uid]
//   默认 port=8095 uid=TEST_CFMRX（可传覆盖，避免多实例撞 uid）
//
// 返回码：0 全过；非 0 有失败。脚本只读，不发破坏性指令。

const path = require('path');
const repo = path.resolve(__dirname, '..');
const WebSocket = require(path.join(repo, 'node_modules/ws/index.js')).default
  || require(path.join(repo, 'node_modules/ws/index.js'));

const PORT = Number(process.argv[2] || 8095);
const UID = String(process.argv[3] || 'TEST_CFMRX' + Math.floor(Math.random() * 900 + 100));
const HOST = '127.0.0.1';
const CLOUD = `ws://${HOST}:${PORT}/ws/agent`;
const VIEWER = `ws://${HOST}:${PORT}/ws/viewer`;
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';

let pass = 0, fail = 0, gotNotify = false, gotConfirmedForward = false;
let notifyId = null;

function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  \u2705 ${name}${detail ? ' \u2014 ' + detail : ''}`); }
  else { fail++; console.log(`  \u274c ${name}${detail ? ' \u2014 ' + detail : ''}`); }
}

// ── 被控端：register → 收 notify → 回复 confirmed ──
const agent = new WebSocket(CLOUD);
agent.on('open', () => {
  agent.send(JSON.stringify({
    type: 'register', uid: UID, token: AGENT_TOKEN, fp: 'reg-test',
    version: '9.9.9', actions: ['notify', 'exam_mode'],
  }));
});
agent.on('message', (raw) => {
  try {
    const m = JSON.parse(raw.toString());
    if (m.type === 'registration-ok') {
      check('被控端注册成功', m.uid === UID);
    }
    if (m.type === 'instruction' && m.action === 'notify') {
      const p = m.params || {};
      const flags = p.flags || {};
      check('收到 notify 且带 confirm', flags.confirm === true, `confirm=${flags.confirm}`);
      check('收到快捷回复列表', Array.isArray(flags.replies) && flags.replies.length >= 1,
        `replies=${JSON.stringify(flags.replies)}`);
      notifyId = m.id || m.instructionId || null;
      gotNotify = true;
      agent.send(JSON.stringify({
        v: 1, type: 'receipt', id: 'rc' + Date.now(),
        payload: { id: notifyId, action: 'notify', result: 'confirmed',
                   data: { confirmed: true, reply: '\u6536\u5230' } },
      }));
      console.log(`  \u21aa \u88ab\u63a7\u7aef\u5df2\u53d1 confirmed \u56de\u6267\uff08reply=\u6536\u5230\uff09`);
    }
  } catch { /* binary frames ignored */ }
});

// ── 管理端：auth → subscribe → 应收 confirmed 转发 ──
const viewer = new WebSocket(VIEWER);
viewer.on('open', () => {
  viewer.send(JSON.stringify({ v: 1, type: 'auth', id: 'v1', payload: { token: VIEWER_TOKEN } }));
});
viewer.on('message', (raw) => {
  try {
    const m = JSON.parse(raw.toString());
    if (m.type === 'viewer-ready') {
      viewer.send(JSON.stringify({ v: 1, type: 'subscribe', id: 'v2', payload: { uid: UID } }));
    }
    if (m.type === 'instruction-result') {
      const pl = m.payload || m;
      check('管理端收到 confirmed 回执（非 failed）',
        pl.result === 'confirmed' && pl.state === 'executed', `result=${pl.result}`);
      check('回执带回复内容', pl.data?.reply === '\u6536\u5230' && pl.data?.confirmed === true,
        `reply=${pl.data?.reply}`);
      gotConfirmedForward = true;
    }
  } catch { /* binary frames ignored */ }
});

// ── 云端 REST 下发 notify ──
const http = require('http');
function postNotify() {
  const body = JSON.stringify({
    uid: UID, action: 'notify',
    params: {
      kind: 'popup', title: '回归确认链路', content: '\u8bf7\u70b9\u6536\u5230',
      flags: { confirm: true, replies: ['\u6536\u5230', '\u597d\u7684', '\u7a0d\u540e\u5904\u7406'] },
      seconds: 10,
    },
  });
  const req = http.request({
    host: HOST, port: PORT, path: '/api/instructions', method: 'POST',
    headers: { 'content-type': 'application/json', authorization: 'Bearer ' + VIEWER_TOKEN },
  }, (res) => {
    let d = '';
    res.on('data', (c) => d += c);
    res.on('end', () => {
      let j = null; try { j = JSON.parse(d); } catch {}
      check('REST 下发 notify', res.statusCode === 200 && j?.state === 'sent', `state=${j?.state}`);
    });
  });
  req.write(body); req.end();
}

setTimeout(() => {
  setTimeout(() => {
    check('被控端收到完整 notify+confirm', gotNotify);
    check('管理端收到 confirmed 转发', gotConfirmedForward);
    console.log(`\n\u2550\u2550 \u7ed3\u679c: ${pass} \u901a\u8fc7 / ${fail} \u5931\u8d25 \u2550\u2550`);
    agent.close(); viewer.close();
    process.exit(fail === 0 ? 0 : 1);
  }, 2500);
  postNotify();
}, 4000);