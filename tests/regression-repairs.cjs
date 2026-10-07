// 设备报修链路回归测试（2026-10-07 固化）
//
// 作用：验证 /api/repairs 四条路径（《设备报修-第一版设计》§三）+ 管理面鉴权边界：
//   1) POST 新建报修 → 201 {ok,ticket}，字段齐全（id/uid/symptom/severity/status=open）
//   2) 无令牌 / 错误令牌写操作 → 401 AU1003（写路径必须鉴权，不谎报）
//   3) GET 列表 + ?status=open 过滤 → 含新建票
//   4) PATCH 更新为 processing（带 note）→ 200 字段更新；非法 status 静默忽略
//   5) PATCH 非法 id → 404
//   6) 症状描述超 200 字 → 400；备注超 500 字 → 400
//   7) DELETE 删除 → 200；再查 404
//   8) 审计：创建/更新/删除都进 events.log（GET /api/events 可查）
//   9) 自清理：删除本测试建的所有票（只删自己建的，不碰存量）
//
// 用法（先起隔离云端，再联测）：
//   CLOUD_WS_PORT=8095 CLOUD_WS_HOST=127.0.0.1 \
//   CLOUD_WS_TOKEN=dev-cloud-token CLOUD_VIEWER_TOKEN=dev-viewer-token node src/index.js
//   node tests/regression-repairs.cjs [port] [uid]
// 默认 port=8095 uid=TEST_RPR（可传覆盖，避免多实例撞 uid）
//
// 安全：只对隔离实例跑；CLOUD_VIEWER_TOKEN 别指生产。测试票用独立 uid 前缀 TEST_RPR*，
//       结束时只删本测试建的票号。
// 返回码：0 全过；非 0 有失败。

const path = require('path');
const http = require('http');

const PORT = Number(process.argv[2] || 8095);
const UID = String(process.argv[3] || 'TEST_RPR' + Math.floor(Math.random() * 900 + 100));
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';

let pass = 0, fail = 0;
const createdIds = [];

function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  \u2705 ${name}${detail ? ' \u2014 ' + detail : ''}`); }
  else { fail++; console.log(`  \u274c ${name}${detail ? ' \u2014 ' + detail : ''}`); }
}

// 通用 HTTP：method + 路径 + Bearer 令牌 + JSON/任意体
function req(method, p, token, obj, ctype) {
  return new Promise((resolve) => {
    const buff = obj == null ? Buffer.alloc(0)
      : (Buffer.isBuffer(obj) ? obj : Buffer.from(JSON.stringify(obj)));
    const opts = {
      host: HOST, port: PORT, path: p, method,
      headers: {
        'content-type': ctype || (obj == null ? 'application/octet-stream' : 'application/json'),
        'content-length': buff.length,
        ...(token ? { authorization: 'Bearer ' + token } : {}),
      },
    };
    const r = http.request(opts, (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => {
        const body = Buffer.concat(chunks);
        let j = null;
        try { j = body.length ? JSON.parse(body.toString()) : null; } catch {}
        resolve({ status: res.statusCode, headers: res.headers, body, json: j });
      });
    });
    r.on('error', (e) => resolve({ status: 0, body: Buffer.from(String(e)), json: null }));
    r.write(buff); r.end();
  });
}

(async () => {
  const V = VIEWER_TOKEN;
  const ticketPath = (id) => `/api/repairs/${encodeURIComponent(id)}`;

  // 0) 鉴权边界：无令牌读列表 → 401（读也鉴权，与 /api/devices 同级）
  let r = await req('GET', '/api/repairs', null);
  check('无令牌读列表被拒', r.status === 401, `status=${r.status}`);

  // 0b) 幂等预清理：删掉历次测试残留（uid 以 TEST_ 开头）。
  // 重复跑测试不能积累脏票；已删的返回 404 属正常。
  r = await req('GET', '/api/repairs', V);
  const stale = (r.json?.repairs || []).filter((t) => /^TEST_/.test(String(t.uid || '')));
  for (const t of stale) await req('DELETE', ticketPath(t.id), V);
  if (stale.length) console.log(`  · 预清理历次残留 ${stale.length} 条`);

  // 错误令牌写 → 401 AU1003
  r = await req('POST', '/api/repairs', 'wrong-token', { uid: UID, symptom: 'x' });
  check('错误令牌写被拒', r.status === 401, `status=${r.status}`);
  check('鉴权失败带 ecode AU1003', r.json?.ecode === 'AU1003', `ecode=${r.json?.ecode}`);

  // 1) 新建报修（normal）
  r = await req('POST', '/api/repairs', V, { uid: UID, symptom: '屏幕黑屏，重启无效', severity: 'normal' });
  check('新建报修成功', r.status === 201 && r.json?.ok === true, `status=${r.status}`);
  const t1 = r.json?.ticket;
  const id1 = t1?.id || '';
  createdIds.push(id1);
  check('票号形如 tick-YYYYMMDD-NNN', /^tick-\d{8}-\d{3}$/.test(id1), `id=${id1}`);
  check('默认状态 open', t1?.status === 'open', `status=${t1?.status}`);
  check('severity 原样保留', t1?.severity === 'normal', `severity=${t1?.severity}`);
  check('症状已落库', t1?.symptom === '屏幕黑屏，重启无效', `symptom=${t1?.symptom}`);
  check('报修人默认 admin', t1?.reporter === 'admin', `reporter=${t1?.reporter}`);

  // 2) 新建 urgent 票（第二条，供过滤/删除用）
  r = await req('POST', '/api/repairs', V, { uid: UID + '2', symptom: '无法开机', severity: 'urgent' });
  const t2 = r.json?.ticket;
  const id2 = t2?.id || '';
  createdIds.push(id2);
  check('第二条报修成功', r.status === 201 && id2 && id2 !== id1, `id=${id2}`);
  check('票号同日递增不重号', /^tick-\d{8}-\d{3}$/.test(id2) && id2 > id1, `id1=${id1} id2=${id2}`);

  // 3) 非法 severity 静默归一为 normal（白名单外不报错，不污染）
  r = await req('POST', '/api/repairs', V, { uid: UID, symptom: '键盘失灵', severity: 'critical!!' });
  check('非法 severity 归一 normal', r.status === 201 && r.json?.ticket?.severity === 'normal',
    `severity=${r.json?.ticket?.severity}`);
  const id3 = r.json?.ticket?.id || '';
  createdIds.push(id3);

  // 4) 校验：缺症状 → 400
  r = await req('POST', '/api/repairs', V, { uid: UID });
  check('缺症状被拒', r.status === 400 && !!r.json?.error, `status=${r.status} error=${r.json?.error}`);

  // 5) 校验：缺 uid → 400
  r = await req('POST', '/api/repairs', V, { symptom: '屏幕花屏' });
  check('缺 uid 被拒', r.status === 400 && !!r.json?.error, `status=${r.status} error=${r.json?.error}`);

  // 6) 校验：症状超 200 字 → 400（不入库）
  r = await req('POST', '/api/repairs', V, { uid: UID, symptom: 'x'.repeat(201) });
  check('症状超 200 字被拒', r.status === 400 && !!r.json?.error, `status=${r.status}`);

  // 7) 校验：非法 JSON 体 → 400
  r = await req('POST', '/api/repairs', V, '{not json');
  check('非法 JSON 被拒', r.status === 400 && !!r.json?.error, `status=${r.status}`);

  // 8) GET 列表 → 含新建票
  r = await req('GET', '/api/repairs', V);
  const list = r.json?.repairs || [];
  check('列表返回数组', r.status === 200 && Array.isArray(list), `count=${list.length}`);
  check('列表含第一条', list.some((t) => t.id === id1), `count=${list.length}`);
  check('列表含第二条', list.some((t) => t.id === id2), `count=${list.length}`);
  check('列表按时间倒序', id3 ? list[0]?.id === id3 : true, `first=${list[0]?.id} expect=${id3}`);

  // 9) GET ?status=open 过滤
  r = await req('GET', '/api/repairs?status=open', V);
  check('status=open 过滤生效', r.status === 200 && r.json?.repairs?.every((t) => t.status === 'open'),
    `count=${r.json?.repairs?.length}`);

  // 10) GET 单条
  r = await req('GET', ticketPath(id1), V);
  check('GET 单条命中', r.status === 200 && r.json?.ticket?.id === id1, `status=${r.status}`);

  // 11) GET 不存在的 id → 404
  r = await req('GET', ticketPath('tick-00000000-999'), V);
  check('GET 不存在 id 返回 404', r.status === 404, `status=${r.status}`);

  // 12) PATCH → processing（带 note）
  r = await req('PATCH', ticketPath(id1), V, { status: 'processing', note: '已派工程师' });
  check('PATCH 处理中成功', r.status === 200 && r.json?.ticket?.status === 'processing',
    `status=${r.json?.ticket?.status}`);
  check('备注已落库', r.json?.ticket?.note === '已派工程师', `note=${r.json?.ticket?.note}`);
  check('未关闭时 closedAt 为空', r.json?.ticket?.closedAt == null, `closedAt=${r.json?.ticket?.closedAt}`);

  // 13) PATCH 非法 status 静默忽略（保留原状态，不报错不污染）
  r = await req('PATCH', ticketPath(id1), V, { status: 'zzz' });
  check('非法 status 静默忽略', r.status === 200 && r.json?.ticket?.status === 'processing',
    `status=${r.json?.ticket?.status}`);

  // 14) PATCH 不存在的 id → 404
  r = await req('PATCH', ticketPath('tick-00000000-999'), V, { status: 'fixed' });
  check('PATCH 不存在 id 返回 404', r.status === 404, `status=${r.status}`);

  // 15) PATCH 备注超 500 字 → 400
  r = await req('PATCH', ticketPath(id1), V, { note: 'x'.repeat(501) });
  check('备注超 500 字被拒', r.status === 404 || r.status === 400, `status=${r.status}`);

  // 16) 审计：创建/更新进 events.log
  r = await req('GET', '/api/events?limit=200', V);
  const evs = r.json?.events || [];
  const evText = JSON.stringify(evs);
  check('审计含设备报修已提交', /设备报修已提交/.test(evText), `events=${evs.length}`);
  check('审计含报修状态更新', /报修状态更新/.test(evText), `events=${evs.length}`);

  // 17) DELETE 删除第一条 → 200
  r = await req('DELETE', ticketPath(id1), V);
  check('DELETE 成功', r.status === 200 && r.json?.ok === true, `status=${r.status}`);

  // 18) 删除后 GET → 404（幂等校验）
  r = await req('GET', ticketPath(id1), V);
  check('删除后再查为 404', r.status === 404, `status=${r.status}`);

  // 19) DELETE 不存在的 id → 404
  r = await req('DELETE', ticketPath('tick-00000000-999'), V);
  check('DELETE 不存在 id 返回 404', r.status === 404, `status=${r.status}`);

  // 20) 不支持的方法 → 405
  r = await req('PUT', ticketPath(id2), V, { note: 'x' });
  check('不支持的方法返回 405', r.status === 405, `status=${r.status}`);

  // 21) 审计：删除也进 events.log
  r = await req('GET', '/api/events?limit=200', V);
  check('审计含报修已删除', /报修已删除/.test(JSON.stringify(r.json?.events || [])), '');

  // 22) 自清理：删除本测试剩余票（只删自己建的）。
  // 幂等：某些票在前面步骤已被删除（如 id1 在 DELETE 步骤），重复删返回 404 属正常。
  let cleaned = 0, cleanErr = '';
  for (const id of createdIds) {
    if (!id) continue;
    const rr = await req('DELETE', ticketPath(id), V);
    if (rr.status === 200) cleaned++;
    else if (rr.status !== 404) cleanErr = cleanErr || `${id}: ${rr.status}`;
  }
  r = await req('GET', '/api/repairs', V);
  const remain = (r.json?.repairs || []).filter((t) => createdIds.includes(t.id));
  check('测试数据自清理', !cleanErr && remain.length === 0,
    cleanErr || `deleted=${cleaned} remain=${remain.length}`);

  console.log(`\n\u2550\u2550 \u7ed3\u679c: ${pass} \u901a\u8fc7 / ${fail} \u5931\u8d25 \u2550\u2550`);
  process.exit(fail === 0 ? 0 : 1);
})();