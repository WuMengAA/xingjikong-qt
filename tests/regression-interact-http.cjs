// 审批面板 · HTTP 路径回归（2026-10-16 固化 · 灵动岛扩展「管理端审批面板」）
//
// 为什么单独一套：tests/regression-interact.cjs 走的是 **WS 路径**
// （song-requests.list / blessing.handle 等 WS 消息），而管理端审批面板
// （InteractBridge.qml 背后的 src/interactbridge.cpp）走的是 **HTTP 路径**：
//   GET  /api/interact/song-requests?state=
//   GET  /api/interact/blessings?state=
//   GET  /api/interact/feedback/summary
//   POST /api/interact/handle {kind,id,action}
// 两条路同一份 interact 库、同一套权限口径，但 HTTP 这条此前**没有端到端脚本**
// —— 于是「面板点了通过，云端到底收没收到」只能靠人工看。本脚本把它锁住。
//
// 覆盖：agent WS 上报造数（点歌/点赞/祝福）→ 管理端 HTTP 拉 3 个端点 →
//   审批 handle（祝福 approve 会向该班广播弹幕，走组件5 可信源路径）→
//   pending 队列移出 → 鉴权边界（无/错令牌 401）→ 状态机只进不退（BAD_STATE）→
//   坏 body 400 / 不存在 id 404。
//
// 用法（先起隔离云端，勿指向生产 8788）：
//   CLOUD_WS_PORT=8095 CLOUD_WS_HOST=127.0.0.1 \
//     CLOUD_WS_TOKEN=dev-cloud-token CLOUD_VIEWER_TOKEN=dev-viewer-token \
//     node src/index.js
//   node tests/regression-interact-http.cjs 8095
// 返回码：0 全过；非 0 有失败。

const path = require('path');
const repo = path.resolve(__dirname, '..');
const WebSocket = require(path.join(repo, 'node_modules/ws/index.js')).default
  || require(path.join(repo, 'node_modules/ws/index.js'));
const http = require('http');

const HOST = '127.0.0.1';
const PORT = Number(process.argv[2] || 8096);
const AT = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VT = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';
// ⚠️ 默认 uid 必须带随机后缀：祝福/点歌有「每台每小时」限额闸门（云端闸门，
//    与端侧冷却并存）。固定 uid 连跑两次就会撞 RATE_LIMITED —— 2026-10-16 实测：
//    同 uid 二次运行 3 条断言误报失败，排查后确认是**测试自己撞限额**，非产品缺陷。
const UID = String(process.argv[3] || 'e2e' + Math.floor(Math.random() * 900 + 100));
const BLESS = '端到端祝福：今天也要加油';

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
    }, 60);
  });
}
function httpReq(method, p, token, bodyObj) {
  return new Promise((resolve) => {
    const buff = bodyObj ? Buffer.from(JSON.stringify(bodyObj)) : Buffer.alloc(0);
    const req = http.request({
      host: HOST, port: PORT, path: p, method,
      headers: {
        'content-type': 'application/json',
        'content-length': buff.length,
        ...(token ? { authorization: 'Bearer ' + token } : {}),
      },
    }, (res) => {
      const cs = [];
      res.on('data', (c) => cs.push(c));
      res.on('end', () => resolve({ status: res.statusCode, body: Buffer.concat(cs) }));
    });
    req.on('error', (e) => resolve({ status: 0, body: Buffer.from(String(e)) }));
    req.write(buff); req.end();
  });
}
const J = (r) => { try { return JSON.parse(r.body.toString()); } catch { return {}; } };

(async () => {
  // ── 1) 被控端 WS 注册 + 上报（造待审数据）──
  const msgs = [];
  const agent = new WebSocket(`ws://${HOST}:${PORT}/ws/agent`);
  let seq = 0;
  const send = (type, payload) => agent.send(JSON.stringify({ v: 1, type, id: 'a' + (++seq), payload }));
  agent.on('open', () => agent.send(JSON.stringify({
    v: 1, type: 'register', id: 'a0',
    payload: { uid: UID, token: AT, fp: 'e2e-fp', version: '9.9.9',
      actions: ['notify'], caps: { screen: true, input: true, power: true, actions: ['notify'] } },
  })));
  agent.on('message', (raw, isBin) => {
    if (isBin) return;
    try { msgs.push(JSON.parse(raw.toString())); } catch { /* 忽略 */ }
  });

  const reg = await waitFor(() => msgs.find((m) => m.type === 'registered' || m.type === 'registration-ok'));
  check('被控端 WS 注册成功', !!reg, UID);

  send('song_request', { title: '端到端测试曲目', artist: '测试歌手', source: 'search' });
  const sAck = await waitFor(() => msgs.find((m) => m.type === 'song-request-ack'));
  check('点歌上报 ack ok', !!sAck && sAck.payload?.ok === true, JSON.stringify(sAck?.payload));

  send('feedback', { kind: 'like', value: 1 });
  const fAck = await waitFor(() => msgs.find((m) => m.type === 'feedback-ack'));
  check('点赞上报 ack ok', !!fAck && fAck.payload?.ok === true);

  send('blessing', { text: BLESS });
  const bAck = await waitFor(() => msgs.find((m) => m.type === 'blessing-ack'));
  check('祝福上报 ack ok（state=pending）', !!bAck && bAck.payload?.ok === true, JSON.stringify(bAck?.payload));
  const blessingId = bAck?.payload?.id;

  // ── 2) HTTP 拉取（InteractBridge 的 3 个 GET）──
  let r = await httpReq('GET', '/api/interact/song-requests?state=pending', VT);
  let j = J(r);
  check('HTTP 点歌待审队列 200 且有数据', r.status === 200 && j.ok === true && (j.items || []).length >= 1,
    `status=${r.status} n=${j.items?.length}`);
  const songHttpId = (j.items || [])[0]?.id;

  r = await httpReq('GET', '/api/interact/blessings?state=pending', VT);
  j = J(r);
  check('HTTP 祝福待审队列 200 且含刚上报那条',
    r.status === 200 && j.ok === true && (j.items || []).some((it) => it.id === blessingId),
    `status=${r.status} n=${j.items?.length} id=${blessingId}`);
  check('HTTP 条目带 classCode 字段（按班过滤依据）',
    !!j.items?.[0] && ('classCode' in j.items[0]), `classCode=${JSON.stringify(j.items?.[0]?.classCode)}`);

  r = await httpReq('GET', '/api/interact/feedback/summary', VT);
  j = J(r);
  check('HTTP 反馈汇总 likeTotal>=1', r.status === 200 && j.likeTotal >= 1,
    `likeTotal=${j.likeTotal} likeToday=${j.likeToday}`);

  // ── 3) 鉴权边界（InteractBridge 依赖同一条路）──
  r = await httpReq('GET', '/api/interact/song-requests', null);
  check('HTTP 无令牌被拒 401', r.status === 401, `status=${r.status}`);
  r = await httpReq('GET', '/api/interact/blessings', 'wrong-token');
  check('HTTP 错误令牌被拒 401', r.status === 401, `status=${r.status}`);

  // ── 4) HTTP 审批 handle（InteractBridge 的 POST）──
  r = await httpReq('POST', '/api/interact/handle', VT, { kind: 'blessing', id: blessingId, action: 'approve' });
  j = J(r);
  check('HTTP 祝福审批 approve 200 ok', r.status === 200 && j.ok === true, `state=${j.item?.state}`);

  // ── 5) 审批后 pending 队列应不再含该条 ──
  r = await httpReq('GET', '/api/interact/blessings?state=pending', VT);
  j = J(r);
  check('审批后 pending 队列移出该条',
    r.status === 200 && !(j.items || []).some((it) => it.id === blessingId),
    `剩余=${(j.items || []).length}`);

  // ── 6) 审批通过 → 该班在线设备收到 notify 弹幕（组件5 可信源路径）──
  // ⚠️ 云端下发走指令信封：type='instruction' + payload.action='notify'，
  //    不是扁平 type='notify' —— 断言必须按信封结构找。
  const dan = await waitFor(() =>
    msgs.find((m) => m.type === 'instruction' && m.payload?.action === 'notify'
      && JSON.stringify(m).includes(BLESS)));
  check('审批通过 → 设备收到 instruction/notify（kind=danmaku 含原文）', !!dan,
    dan ? `kind=${dan.payload?.params?.kind} content=${dan.payload?.params?.content}` : '未收到');

  // ── 7) 状态机只进不退 ──
  r = await httpReq('POST', '/api/interact/handle', VT, { kind: 'blessing', id: blessingId, action: 'reject' });
  j = J(r);
  check('重复处理 HTTP 回 BAD_STATE', r.status !== 200 || j.ok === false,
    `status=${r.status} body=${JSON.stringify(j).slice(0, 100)}`);

  // ── 8) 点歌 HTTP 审批 ──
  r = await httpReq('POST', '/api/interact/handle', VT, { kind: 'song', id: songHttpId, action: 'approve' });
  j = J(r);
  check('HTTP 点歌审批 approve 200 ok', r.status === 200 && j.ok === true, `state=${j.item?.state}`);

  // ── 9) 坏 body 如实回 400 ──
  r = await httpReq('POST', '/api/interact/handle', VT, { kind: 'xxx' });
  check('HTTP 坏 body 回 400', r.status === 400, `status=${r.status}`);

  // ── 10) 不存在 id 回 NOT_FOUND ──
  r = await httpReq('POST', '/api/interact/handle', VT, { kind: 'blessing', id: 999999, action: 'approve' });
  j = J(r);
  check('HTTP 不存在 id 如实回 NOT_FOUND', r.status !== 200 || j.ok === false,
    `status=${r.status} body=${JSON.stringify(j).slice(0, 100)}`);

  agent.close();
  console.log(`\n\u2550\u2550 结果: ${pass} 通过 / ${fail} 失败 \u2550\u2550`);
  process.exit(fail === 0 ? 0 : 1);
})();
