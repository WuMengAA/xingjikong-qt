// 祝福弹幕词表端点回归（2026-11-05）。
//
// 三层：
//   L1 纯逻辑：sanitizeWords/loadDanmakuWords/danmakuWordsPayload（直接 import 模块，零服务）。
//   L2 HTTP：隔离实例（临时数据目录 + 新端口 8883）真起 index.js，验「无鉴权 200」。
//   L3 反向：把真源码复制进工程内临时目录，删掉薄壳 ⇒ 端点消失（非 200），
//           证明 L2 的「200」断言确实在测这层接线、不是别的 handler 假绿。
//
// 端口错开：8855(admin-server)/8860(smoke)/8871(admin 验收)/8883(本脚本)/8788(生产)。

import { mkdtempSync, writeFileSync, rmSync, readFileSync, cpSync, openSync, closeSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { spawn } from 'node:child_process';

const HERE = dirname(fileURLToPath(import.meta.url));
const CLOUD = resolve(HERE, '..');
const SRC_DIR = join(CLOUD, 'src');
const WORDS_JSON = join(SRC_DIR, 'danmaku-words.json');
const PORT = 8883;
let PASS = 0, FAIL = 0;
const ok = (c, msg) => { if (c) { ++PASS; } else { ++FAIL; console.error('  FAIL:', msg); } };

async function fetchJson(url) {
  const r = await fetch(url);
  return { status: r.status, body: await r.json().catch(() => null) };
}

// ── L1 纯逻辑 ────────────────────────────────────────────────
async function testPure() {
  const m = await import(pathToFileURL(join(SRC_DIR, 'danmaku-words.js')).href);
  ok(m.DANMAKU_WORDS_PATH === '/api/danmaku/words', '路径常量');
  ok(m.MAX_WORDS === 200 && m.MAX_WORD_CHARS === 64, '规模常量');
  const s = m.sanitizeWords(['  广告  ', '外挂', '外挂', 42, null, '', '   ', 'x'.repeat(100)]);
  ok(s.length === 3, 'sanitize 去非串/去空白/去重');  // 广告 / 外挂 / x*64
  ok(s.includes('广告') && s.includes('外挂'), 'sanitize 保留干净词');
  ok(s.some(w => w.length === 64), 'sanitize 截断 64');
  ok(m.sanitizeWords(Array.from({ length: 300 }, (_, i) => 'w' + i)).length === 200, 'sanitize 限量 200');
  const p = m.danmakuWordsPayload();
  ok(p.ok === true && Array.isArray(p.words), 'payload 结构');
  ok(p.cacheSeconds === 30, 'payload 缓存秒数');
  // 缺失文件回落空表（不抛异常）
  ok(m.loadDanmakuWords().ok === true, 'loadDanmakuWords 恒 ok');
}

// ── 隔离实例启动 ─────────────────────────────────────────────
// srcDir：index.js 所在工程根（其下须有 src/index.js）；dataDir：库/日志落点（隔离）。
async function startCloud(srcDir, dataDir, port) {
  const e = {
    ...process.env,
    CLOUD_WS_PORT: String(port), CLOUD_WS_HOST: '127.0.0.1',
    CLOUD_EVENTS_FILE: join(dataDir, 'events.log'),
    CLOUD_QUEUE_DB_FILE: join(dataDir, 'queue.db'),
    CLOUD_CLASS_DIR_DB_FILE: join(dataDir, 'class-dir.db'),
    CLOUD_CLASS_ROOM_DB_FILE: join(dataDir, 'class-room.db'),
    CLOUD_INTERACT_DB_FILE: join(dataDir, 'interact.db'),
    CLOUD_DATA_DIR: dataDir,
    CLOUD_TENANTS_FILE: join(dataDir, 'tenants.json'),
  };
  const lf = join(dataDir, 'server.log');
  const log = openSync(lf, 'w');   // 文件而非 PIPE：Fastify 每请求一条日志会灌满管道阻塞事件循环
  const p = spawn(process.execPath, [join(srcDir, 'src', 'index.js')], {
    cwd: srcDir, env: e, stdio: ['ignore', log, log],
  });
  // 现役云端没有 /health 路由（那是 admin-server 的），用公开只读的 /api/plugins 探活。
  for (let i = 0; i < 60; ++i) {
    try { const r = await fetch(`http://127.0.0.1:${port}/api/plugins`); if (r.ok) { p._log = log; return p; } } catch {}
    await new Promise(r => setTimeout(r, 200));
  }
  p.kill('SIGKILL'); closeSync(log);
  throw new Error('云端未就绪，日志：\n' + readFileSync(lf, 'utf8').slice(-1500));
}
async function stopCloud(p) {
  try { p.kill('SIGKILL'); } catch {}
  try { p._log != null && closeSync(p._log); } catch {}
  // 等进程真正退出（SIGKILL 在 Windows 上异步），否则库文件句柄未释放 → rmSync EPERM
  await new Promise(r => { if (p.exitCode !== null) return r(); p.once('exit', r); setTimeout(r, 1500); });
}
function safeRm(dir) {
  for (let i = 0; i < 4; ++i) {
    try { rmSync(dir, { recursive: true, force: true, maxRetries: 5, retryDelay: 300 }); return; }
    catch (e) { if (i === 3) console.error('  [warn] 临时目录残留:', dir, e.code); }
  }
}

// ── L2 隔离实例 HTTP（真源码，数据在临时目录）─────────────────
async function testHttp() {
  const data = mkdtempSync(join(tmpdir(), 'danmaku-data-'));
  const backup = readFileSync(WORDS_JSON, 'utf8');
  let p;
  try {
    writeFileSync(WORDS_JSON, JSON.stringify({ updatedAt: '2026-11-05', words: ['霸凌', '作弊', '  广告  ', 42] }));
    p = await startCloud(CLOUD, data, PORT);
    const r = await fetchJson(`http://127.0.0.1:${PORT}/api/danmaku/words`);
    ok(r.status === 200, '无鉴权 GET 200（在 isManageApi 之前）');
    ok(r.body && r.body.ok === true, '响应 ok=true');
    ok(r.body && Array.isArray(r.body.words) && r.body.words.includes('霸凌'), '词表内容');
    ok(r.body && r.body.words.length === 3 && !r.body.words.includes('42') && !r.body.words.includes(42), '词表已 sanitize');
    ok(r.body && r.body.cacheSeconds === 30, '缓存秒数');
    ok(r.body && r.body.updatedAt === '2026-11-05', 'updatedAt 透传');
    const m = await fetch(`http://127.0.0.1:${PORT}/api/interact/blessings`);
    ok(m.status === 401, '对照：管理面 /api/interact/* 无令牌 401');
  } finally {
    if (p) await stopCloud(p);
    writeFileSync(WORDS_JSON, backup);          // 真源必须还原
    safeRm(data);
  }
}

// ── L3 反向验证 ──────────────────────────────────────────────
async function reverse() {
  const war = mkdtempSync(join(CLOUD, '.tmp-danmaku-'));   // 工程内 ⇒ CLOUD/node_modules 可见
  let p;
  try {
    cpSync(SRC_DIR, join(war, 'src'), { recursive: true });
    const idx = join(war, 'src', 'index.js');
    let t = readFileSync(idx, 'utf8');
    const shell = "  if (req.method === 'GET' && u.pathname === DANMAKU_WORDS_PATH) return send(200, danmakuWordsPayload());";
    if (!t.includes(shell)) throw new Error('薄壳锚点漂移（源码已变，反向验证需同步更新）');
    t = t.replace(shell, '  // [BAD] 薄壳被删：端点应消失');
    writeFileSync(idx, t);
    p = await startCloud(war, war, PORT + 10);
    const r = await fetch(`http://127.0.0.1:${PORT + 10}/api/danmaku/words`);
    console.log(`  [BAD-no-shell] status=${r.status}（期望非 200）`);
    ok(r.status !== 200, '反向：删掉薄壳后端点必须不再 200（证明 L2 非假阴性）');
  } finally {
    if (p) await stopCloud(p);
    safeRm(war);
  }
}

async function main() {
  await testPure();
  await testHttp();
  await reverse();
  console.log(`\nRESULT PASS=${PASS} FAIL=${FAIL}`);
  process.exitCode = FAIL === 0 ? 0 : 1;
}
main().catch(e => { console.error(e); process.exitCode = 1; });
