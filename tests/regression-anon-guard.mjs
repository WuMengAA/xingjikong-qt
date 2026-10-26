// 匿名访问敏感只读端点 → 必须 401（2026-10-26 · 大范围找活安全审计 · 2026-10-26 晚更正）
//
// 背景：/api/events、/api/actions/ranking、/api/instructions/pending、/api/instructions/notice
//   自 2026-10-05 起即由 isManageApi（src/index.js 649 行 401 闸门）覆盖，匿名访问本就 401；anonGuarded 冗余块已删（见加减归档）。
//   修复后：匿名 → 401 AU1001；带令牌（静态或票据）→ 200。
//   注：/api/instructions/notice 由站点（website）服务端调用，站点统一持 Bearer，已与被告知需回归。
//
// 用法（先起隔离云端，再联测）：
//   node tests/regression-anon-guard.mjs [port]
// 返回码：0 全过；非 0 有失败。
import http from 'node:http';

const PORT = Number(process.argv[2] || 8991);
const HOST = '127.0.0.1';
const TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';

let pass = 0, fail = 0;
const check = (n, ok, d) => {
  if (ok) { pass++; console.log(`  ✅ ${n}${d ? ' — ' + d : ''}`); }
  else { fail++; console.log(`  ❌ ${n}${d ? ' — ' + d : ''}`); }
};

function get(path, token) {
  return new Promise((res) => {
    const r = http.request({
      host: HOST, port: PORT, path, method: 'GET',
      headers: token ? { authorization: 'Bearer ' + token } : {},
    }, (x) => {
      const c = [];
      x.on('data', (d) => c.push(d));
      x.on('end', () => res({ status: x.statusCode, body: Buffer.concat(c).toString() }));
    });
    r.on('error', (e) => res({ status: 0, err: String(e) }));
    r.end();
  });
}

(async () => {
  // 1) 匿名：四个敏感端点必须 401
  let r = await get('/api/events');
  check('匿名 /api/events → 401', r.status === 401, `status=${r.status}`);
  r = await get('/api/actions/ranking');
  check('匿名 /api/actions/ranking → 401', r.status === 401, `status=${r.status}`);
  r = await get('/api/instructions/pending');
  check('匿名 /api/instructions/pending → 401', r.status === 401, `status=${r.status}`);
  r = await get('/api/instructions/notice');
  check('匿名 /api/instructions/notice → 401', r.status === 401, `status=${r.status}`);

  // 2) 带静态令牌：应正常 200（确认没把合法调用方挡在门外）
  r = await get('/api/events', TOKEN);
  check('带令牌 /api/events → 200', r.status === 200, `status=${r.status}`);
  r = await get('/api/actions/ranking', TOKEN);
  check('带令牌 /api/actions/ranking → 200', r.status === 200, `status=${r.status}`);
  r = await get('/api/instructions/pending', TOKEN);
  check('带令牌 /api/instructions/pending → 200', r.status === 200, `status=${r.status}`);
  r = await get('/api/instructions/notice?notice_id=1', TOKEN);
  check('带令牌 /api/instructions/notice → 200/400（鉴权已过，缺参另算）',
    r.status === 200 || r.status === 400, `status=${r.status}`);

  console.log(`\n══ 结果: ${pass} 通过 / ${fail} 失败 ══`);
  process.exit(fail ? 1 : 0);
})();
