// 设备接入票 · WS 注册通路回归测试（2026-10-07 固化）
//
// 作用：验证 verifyAgentTicket 在**真实握手链路**上的行为，而不只是单元层往返。
// 覆盖 9f78901 引入的「设备接入票优先、静态令牌兜底」双通道：
//   1) 合法设备票 + fp 匹配        → 注册成功（registration-ok）
//   1a) 持票冒名别的 uid           → BAD_PAYLOAD（uid 必须等于票内 uid）
//   1b) 只报 fp 不报 uid           → 注册成功，uid 取自票
//   1c) 持票但不报 fp              → 拒绝（票绑定了指纹，不报不算数）
//   2) 同一张票拷到 fp 不同的机器  → 票不认、也不退静态令牌 ⇒ 拒绝
//      （关键防线：票被拷到别的机器上不能注册，因为 fp 对不上）
//   3) 静态 DEV_TOKEN 单独注册     → 成功（旧部署不破，向后兼容）
//   4) 伪票（7 段但签名是垃圾）    → BAD_TOKEN 拒绝并断连
//   5) 伪票里夹带 DEV_TOKEN 拼串   → 仍拒绝（令牌是整体精确比对，不能"贴进去"绕过）
//   6) 非 register 首帧            → AUTH_REQUIRED
//   7) 不发首帧                    → 不假注册（超时未收到注册回执）
//
// 票按站点 /api/device/bind 的 signDeviceTicket 原样构造（7 段，签名=前6段点连）。
//   该格式与云端 mintAgentTicket 的往返等价性由 tests/unit-agent-ticket.mjs 断言；
//   本脚本只关心"云端验票端在真实握手上认不认得站点签的票"。
//
// 用法（先起隔离云端，再联测）：
//   CLOUD_WS_PORT=8095 CLOUD_VIEWER_SECRET=it-secret node src/index.js
//   node tests/regression-agent-ticket.mjs [port]
// 返回码：0 全过；非 0 有失败。
//
// 安全：只对隔离实例跑。不要指向生产端口。
// 账号绑定通道的 owner（绑定者用户名）写进 events.log 审计，兑现站点 /api/device/bind 的
//   设计承诺「云端注册事件能查到哪台设备是谁绑进来的」；否则该字段只进控制台、重启即丢。
//   见 1d 断言。

import crypto from 'node:crypto';
import http from 'node:http';
import WebSocket from 'ws';

const PORT = Number(process.argv[2] || 8095);
const HOST = '127.0.0.1';
const SECRET = process.env.CLOUD_VIEWER_SECRET || 'it-secret';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';
// ⚠️ 必须用 Buffer 形式：票的签名段是 HMAC 二进制摘要，不是文本。
//   写成 Buffer.from(String(s),'utf8') 会把非法 UTF-8 序列替换掉，
//   导致签出来的摘要与云端算的不一致（症状：合法票被误判为签名不对）。
const b64u = (b) => Buffer.from(b).toString('base64url');
const s64u = (s) => b64u(Buffer.from(String(s), 'utf8'));   // 字符串 → base64url

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  ✅ ${name}${detail ? ' — ' + detail : ''}`); }
  else { fail++; console.log(`  ❌ ${name}${detail ? ' — ' + detail : ''}`); }
}

// 与站点 signDeviceTicket 同格式（7 段）；ttlSec 默认 30 天
function signDeviceTicket(devUid, fp, owner, ttlSec = 30 * 24 * 3600) {
  const exp = Date.now() + ttlSec * 1000;
  const nonce = crypto.randomBytes(8).toString('hex');
  const payload = `v1.${exp}.${s64u(devUid)}.${s64u(fp)}.${nonce}.${s64u(owner)}`;
  const sig = b64u(crypto.createHmac('sha256', SECRET).update(payload).digest());
  return `${payload}.${sig}`;
}

// 旧版扁平握手（不启用 v1 信封 → 回执类型是 registration-ok）
const REG = (extra) => ({ type: 'register', version: '0.6.23', actions: ['notify'], ...extra });

/**
 * 连一次、发一次握手、等结果。返回：
 *   {ok:true, uid, msgs}        收到注册成功回执
 *   {ok:false, code, msgs}      收到协议错误（code 来自 errorPayload）
 *   {ok:false, reason, msgs}    超时 / 断连 / socket 错误
 * msgs 是**收到的全部服务端消息**（含未识别类型），供"云端保持沉默"这类断言使用。
 *
 * @param {object} [opts]
 * @param {number} [opts.timeoutMs=3500] 等多久判定为静默
 * @param {string|null} [opts.firstFrame] 首帧原文；给了就发它
 * @param {boolean} [opts.noSend] true = 连上后什么都不发（只观测云端是否静默）
 */
function registerOnce(payload, { timeoutMs = 3500, firstFrame = null, noSend = false } = {}) {
  return new Promise((resolve) => {
    let settled = false;
    const msgs = [];
    const sock = new WebSocket(`ws://${HOST}:${PORT}/ws/agent`);
    const finish = (res) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      try { sock.terminate(); } catch { /* 已断 */ }
      resolve({ ...res, msgs });
    };
    const timer = setTimeout(() => finish({ ok: false, reason: `静默 ${timeoutMs}ms（云端既没注册成功也没报协议错误）` }), timeoutMs);

    sock.on('open', () => {
      if (noSend) return;   // 「连上但什么都不发」：必须真的不 send，别发 JSON.stringify(null)
      sock.send(firstFrame ?? JSON.stringify(payload));
    });
    sock.on('message', (raw) => {
      let m;
      try { m = JSON.parse(raw.toString()); } catch { return; }
      msgs.push(m);
      if (m.type === 'registered' || m.type === 'registration-ok') {
        finish({ ok: true, uid: m.payload?.uid ?? m.uid });
        return;
      }
      if (m.type === 'error' || m.payload?.code) {
        finish({ ok: false, code: m.payload?.code || m.code, message: m.payload?.message });
        return;
      }
      // 旧客户端兼容分支（sndErr 会给 legacy 客户端补一条类型为错误码的消息）
      if (m.code && !m.payload?.uid) {
        finish({ ok: false, code: m.code, message: m.message });
      }
    });
    sock.on('error', (e) => finish({ ok: false, reason: `socket error: ${e.message}` }));
    sock.on('close', () => finish({ ok: false, reason: '连接被云端关闭' }));
  });
}

function listDevices() {
  return new Promise((resolve) => {
    const r = http.request(
      { host: HOST, port: PORT, path: '/api/devices', method: 'GET', headers: { authorization: 'Bearer ' + VIEWER_TOKEN } },
      (res) => {
        let d = '';
        res.on('data', (c) => { d += c; });
        res.on('end', () => resolve({ status: res.statusCode, body: d }));
      },
    );
    r.on('error', (e) => resolve({ status: 0, body: String(e) }));
    r.end();
  });
}

function listEvents(limit = 400) {
  return new Promise((resolve) => {
    const r = http.request(
      { host: HOST, port: PORT, path: `/api/events?limit=${limit}`, method: 'GET', headers: { authorization: 'Bearer ' + VIEWER_TOKEN } },
      (res) => {
        let d = '';
        res.on('data', (c) => { d += c; });
        res.on('end', () => resolve({ status: res.statusCode, body: d }));
      },
    );
    r.on('error', (e) => resolve({ status: 0, body: String(e) }));
    r.end();
  });
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const FP = 'it-fp-device01';
  const OWNER = 'it-owner';

  // 1) 合法设备票 + fp 匹配 → 注册成功
  const ticket = signDeviceTicket('it-dev-a', FP, OWNER);
  let r = await registerOnce(REG({ uid: 'it-dev-a', token: ticket, fp: FP }));
  check('设备票 + fp 匹配注册成功', r.ok === true && r.uid === 'it-dev-a',
    r.ok ? `uid=${r.uid}` : `${r.code || ''} ${r.reason || r.message || ''}`);

  // 1a) 冒名防线：持 A 机的票，却想注册成别的 uid → 拒绝。
  //     票是「这张票授权这台设备」，uid 由站点绑定流程签发，客户端改口不算数。
  r = await registerOnce(REG({ uid: 'attacker-claims-this', token: ticket, fp: FP }));
  check('持票冒名别的 uid 被拒', r.ok === false && /BAD_PAYLOAD/.test(String(r.code)), String(r.code || r.reason));

  // 1b) 只报 fp 不报 uid → uid 取自票（客户端无法省略 uid 来绕过）
  r = await registerOnce(REG({ token: ticket, fp: FP }));
  check('未报 uid 时取自票（仍注册成票内 uid）', r.ok === true && r.uid === 'it-dev-a',
    r.ok ? `uid=${r.uid}` : `${r.code || r.reason}`);

  // 1c) 持票但不报 fp → 拒绝（票已绑定指纹，不报不算数）
  r = await registerOnce(REG({ uid: 'it-dev-a', token: ticket }));
  check('持票但不报 fp 被拒', r.ok === false && !!r.code, r.code || r.reason);

  // 1d) 注册进事件审计（设备上线类事件）。
  //     账号绑定通道的 owner（绑定者用户名）也写进审计——兑现站点 /api/device/bind 的
  //     设计承诺「云端注册事件能查到哪台设备是谁绑进来的」，否则该字段只进控制台、重启即丢。
  const ev = await listEvents();
  let evJson = null;
  try { evJson = JSON.parse(ev.body); } catch { /* 非 JSON */ }
  const evText = JSON.stringify(evJson?.events || []);
  check('注册进事件审计（含设备 uid + 绑定者 owner）',
    ev.status === 200 && evText.includes('it-dev-a') && evText.includes(OWNER),
    `events=${evJson?.events?.length || 0}`);

  // 2) 同一张票拷到 fp 不同的机器 → 票不认，也不退静态令牌 ⇒ 拒绝
  r = await registerOnce(REG({ uid: 'it-dev-b', token: ticket, fp: 'someone-else-fp' }));
  check('票拷到别的机器被拒（fp 不匹配）', r.ok === false && !!r.code, r.code || r.reason);

  // 3) 静态 DEV_TOKEN 单独注册 → 成功（向后兼容旧部署）
  r = await registerOnce(REG({ uid: 'it-dev-c', token: AGENT_TOKEN, fp: FP }));
  check('静态令牌单独注册仍成功', r.ok === true, r.ok ? `uid=${r.uid}` : `${r.code || r.reason}`);

  // 4) 伪票（7 段结构完整、签名是垃圾）→ 拒绝并断连
  const forged = ticket.replace(/\.[^.]+$/, '.AAAA');
  r = await registerOnce(REG({ uid: 'it-dev-a', token: forged, fp: FP }));
  check('伪票被拒（签名不符）', r.ok === false && !!r.code, r.code || r.reason);

  // 5) 企图把静态令牌"贴进"票里绕过 → 仍拒绝（令牌整体精确比对，不做子串匹配）
  r = await registerOnce(REG({ uid: 'it-dev-c', token: `${forged}|${AGENT_TOKEN}`, fp: FP }));
  check('票里夹带静态令牌仍被拒', r.ok === false && !!r.code, r.code || r.reason);

  // 6) 非 register 首帧 → AUTH_REQUIRED
  r = await registerOnce(null, { firstFrame: '{"type":"hello","x":1}' });
  check('非 register 首帧被拒', r.ok === false && /AUTH_REQUIRED/.test(String(r.code)), String(r.code || r.reason));

  // 7) 不发首帧 → 不假注册。
  //    断言的是"云端保持沉默"：既不给注册成功回执，也不给协议错误。
  //    （等 HANDSHAKE_TIMEOUT_MS=8s 才会被踢；本脚本只观测 1500ms 内的静默，不空等。）
  //    ⚠️ 必须用 noSend：传 firstFrame:null 会变成 send(JSON.stringify(null))，
  //    那等于发了 "null" 首帧，云端会正确回 BAD_PAYLOAD——测的就不是"不发"了。
  r = await registerOnce(null, { noSend: true, timeoutMs: 1500 });
  check('不发首帧不假注册（云端保持沉默）',
    r.ok === false && !r.code && r.msgs.length === 0,
    `msgs=${r.msgs.length} ${r.reason || ''}`);

  // 8) 在线表含本测试注册的设备。
  //    ⚠️ 必须保持 socket 常连：registerOnce 结束会 terminate 连接，
  //    云端随即把它判掉线（markDisconnected）并从在线表移除，所以要先注册再查表。
  const stay = new WebSocket(`ws://${HOST}:${PORT}/ws/agent`);
  let stayOk = false;
  const stayDone = new Promise((res) => {
    stay.on('open', () => {
      stay.send(JSON.stringify(REG({ uid: 'it-dev-stay', token: AGENT_TOKEN, fp: FP })));
    });
    stay.on('message', (raw) => {
      try {
        const m = JSON.parse(raw.toString());
        if (m.type === 'registered' || m.type === 'registration-ok') { stayOk = true; res(); }
      } catch { /* 非 JSON 忽略 */ }
    });
    stay.on('error', () => res());
    setTimeout(res, 4000);
  });
  await stayDone;
  await sleep(200);   // 给云端一拍，把设备写进表
  const d = await listDevices();
  let dJson = null;
  try { dJson = JSON.parse(d.body); } catch { /* 非 JSON */ }
  check('在线表含本测试注册的设备', stayOk === true && d.status === 200 &&
    JSON.stringify(dJson?.devices || []).includes('it-dev-stay'),
    `stayOk=${stayOk} count=${dJson?.count ?? '?'}`);
  stay.terminate();

  await sleep(200);
  console.log(`\n══ 结果: ${pass} 通过 / ${fail} 失败 ══`);
  process.exit(fail === 0 ? 0 : 1);
})();
