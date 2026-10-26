// 录制回放「按班」权限回归（2026-10-26 · 大范围找活批次）
//
// 背景（为什么单开一个文件，而不是塞进 regression-recordings.cjs）：
//   `/api/recordings`（列表）与 `/recordings/*`（下载）此前只有「有没有令牌」这一层鉴权，
//   **没有 canAccess 按班这一层**。录像 = 教室画面 + 声音，是本项目最敏感的一类数据：
//   受限老师票能列出并下载全校任意班的录像。与 /api/frame（实时画面）、
//   /api/instructions（下发指令）早有的口径不一致 —— 那两条都过 canAccess。
//
// 本文件固化修复后的判定：
//   ① 全权身份（静态令牌）：无班级归属的设备和有班级的设备都可见
//   ② 受限票据（scopes=[{type:'class',id:CLS}]）：只见本班设备；无归属设备不可见
//   ③ 下载同理：本班 200、非本班 403（PV4002）
//
// 用法（先起隔离云端，再联测）：
//   CLOUD_WS_PORT=8991 CLOUD_VIEWER_SECRET=it-secret ... node src/index.js
//   node tests/regression-recordings-scope.mjs [port]
// ⚠️ CLOUD_VIEWER_SECRET 必须与实例一致（config.js 在模块加载时固化密钥）。
// 返回码：0 全过；非 0 有失败。自清理：删掉自己上传的两个 uid 目录。

import http from 'node:http';
import path from 'node:path';
import fs from 'node:fs';
import { mintViewerTicket } from '../src/ticket.js';

const PORT = Number(process.argv[2] || 8991);
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';
const CLS = 'SCOPEC1';                 // 7 位字母数字，符合 isClassCodeValid
const RND = Math.floor(Math.random() * 900 + 100);
const UID_A = 'TESTSCPA' + RND;        // 故意不绑班（无班级归属）
const UID_B = 'TESTSCPB' + RND;        // 绑到 CLS

let pass = 0, fail = 0;
const check = (name, ok, detail) => {
  if (ok) { pass++; console.log(`  ✅ ${name}${detail ? ' — ' + detail : ''}`); }
  else { fail++; console.log(`  ❌ ${name}${detail ? ' — ' + detail : ''}`); }
};

function req(method, p, token, body, ctype) {
  return new Promise((resolve) => {
    const buff = body == null ? Buffer.alloc(0)
      : (Buffer.isBuffer(body) ? body : Buffer.from(JSON.stringify(body)));
    const r = http.request({
      host: HOST, port: PORT, path: p, method,
      headers: {
        'content-type': ctype || (body == null ? 'application/octet-stream' : 'application/json'),
        'content-length': buff.length,
        ...(token ? { authorization: 'Bearer ' + token } : {}),
      },
    }, (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => {
        const b = Buffer.concat(chunks);
        let j = null;
        try { j = b.length ? JSON.parse(b.toString()) : null; } catch {}
        resolve({ status: res.statusCode, json: j, bytes: b.length });
      });
    });
    r.on('error', (e) => resolve({ status: 0, json: null, bytes: 0, err: String(e) }));
    r.write(buff);
    r.end();
  });
}

(async () => {
  const fake = Buffer.alloc(1500, 0xab);

  // 0) 签一张受限票（只带本班 scope）
  const minted = mintViewerTicket('t-scope-rec', 600000, { scopes: [{ type: 'class', id: CLS }] });
  if (!minted.ok) {
    console.log(`  ❌ 签受限票失败（需 CLOUD_VIEWER_SECRET，且与实例一致）：${minted.reason}`);
    process.exit(1);
  }
  const LT = minted.ticket;
  check('受限票据签发成功', !!minted.ok && !!LT, `uid=t-scope-rec scope=class:${CLS}`);

  // 1) 两台设备各传一段
  let r = await req('POST', `/api/recordings/upload?uid=${UID_A}`, AGENT_TOKEN, fake);
  const fA = r.json?.fname || '';
  check('A（无班级归属）上传成功', r.status === 200 && !!fA, `status=${r.status}`);
  r = await req('POST', `/api/recordings/upload?uid=${UID_B}`, AGENT_TOKEN, fake);
  const fB = r.json?.fname || '';
  check('B 上传成功', r.status === 200 && !!fB, `status=${r.status}`);

  // 2) 把 B 绑到 CLS（A 保持无归属）
  r = await req('POST', '/api/device/bind-class', VIEWER_TOKEN, { uid: UID_B, classCode: CLS });
  check('B 已绑到本班', r.status === 200 && r.json?.ok === true, `status=${r.status} ${r.json?.error || ''}`);

  // 3) 全权（静态令牌）列表：A、B 都在
  r = await req('GET', '/api/recordings', VIEWER_TOKEN);
  const full = r.json?.recordings || {};
  check('全权列表含 A', (full[UID_A] || []).length > 0, `A=${(full[UID_A] || []).length}`);
  check('全权列表含 B', (full[UID_B] || []).length > 0, `B=${(full[UID_B] || []).length}`);

  // 4) 受限票列表：只有 B（A 无班级归属，按设备表同口径谁都不该看）
  r = await req('GET', '/api/recordings', LT);
  const lim = r.json?.recordings || {};
  check('受限列表含本班 B', (lim[UID_B] || []).length > 0, `B=${(lim[UID_B] || []).length}`);
  check('受限列表不含无归属 A', !lim[UID_A], `A=${lim[UID_A] ? '出现了（越权）' : '已过滤'}`);

  // 5) 下载：全权两个都能下
  r = await req('GET', `/recordings/${UID_A}/${fA}`, VIEWER_TOKEN);
  check('全权下载 A', r.status === 200, `status=${r.status}`);
  r = await req('GET', `/recordings/${UID_B}/${fB}`, VIEWER_TOKEN);
  check('全权下载 B', r.status === 200, `status=${r.status}`);

  // 6) 下载：受限票本班 200、非本班 403
  r = await req('GET', `/recordings/${UID_B}/${fB}`, LT);
  check('受限下载本班 B 放行', r.status === 200, `status=${r.status}`);
  r = await req('GET', `/recordings/${UID_A}/${fA}`, LT);
  check('受限下载 A 被拒 403', r.status === 403 && r.json?.ecode === 'PV4002',
    `status=${r.status} ecode=${r.json?.ecode}`);

  // 7) 自清理（只删自己这两个 uid）
  const recRoot = process.env.CLOUD_RECORDINGS_DIR
    ? path.resolve(process.env.CLOUD_RECORDINGS_DIR)
    : path.resolve(new URL('..', import.meta.url).pathname.replace(/^\//, ''), 'recordings');
  let cleaned = true;
  for (const uid of [UID_A, UID_B]) {
    try { fs.rmSync(path.join(recRoot, uid), { recursive: true, force: true }); }
    catch { cleaned = false; }
  }
  check('测试数据自清理', cleaned, `删 ${UID_A}/${UID_B}`);

  console.log(`\n══ 结果: ${pass} 通过 / ${fail} 失败 ══`);
  process.exit(fail === 0 ? 0 : 1);
})();
