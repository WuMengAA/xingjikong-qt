// 本地 VoiceHub 替身（2026-10-23）
// ─────────────────────────────────────────────────────────────────────────────
// 事由：真 VoiceHub（voicehub.245959623.xyz）当前 402 停机，预计一个月后复活。
//       这一个月里广播社写回链路用本服务顶替，使 cloud → VoiceHub 代理可在本地跑通。
//
// ⚠️ 定位：仅本地开发/联调替身，**不替代**真实 voicehub 工程（红线外工程，不动）。
//   真实 VoiceHub 复活后，把 cloud 的 STE_VOICEHUB_WRITE_BASE 指回真域名即可，本文件可删。
//
// 忠实契约（源自 src/broadcast-club.js）：
//   GET  {base}/api/songs/public           无鉴权，返回排期数组（原样透传给成员端）
//   POST {base}/api/open/schedules/reorder 头带 x-api-key，body {schedules:[{id,sequence}]}
//        成功看 HTTP 状态（cloud 仅判 r.ok），本服务返回 200 {ok:true,updated:n}
//
// 环境变量：
//   STE_VH_STANDIN_PORT      监听端口，默认 8799
//   STE_VOICEHUB_WRITE_KEY  与 cloud 共用同一把写密钥，用于校验 x-api-key；
//                           未设则退化为开发默认键并打印警告（仅本地可用）
//   状态落盘：<cloud>/data/voicehub-standin/state.json（已 gitignore）

import http from 'node:http';
import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DATA_DIR = join(__dirname, '..', 'data', 'voicehub-standin');
const STATE_FILE = join(DATA_DIR, 'state.json');

const PORT = Number(process.env.STE_VH_STANDIN_PORT || 8799);
const EXPECTED_KEY = process.env.STE_VOICEHUB_WRITE_KEY || 'dev-vh-key';
if (!process.env.STE_VOICEHUB_WRITE_KEY) {
  console.warn('[standin] ⚠️ STE_VOICEHUB_WRITE_KEY 未设，使用开发默认键 "dev-vh-key"；仅本地联调可用');
}

// ── 种子数据（与 harness 中 mock 一致，字段形状对齐 control-qt 解析）──────────
function seed() {
  return [
    { id: 1, sequence: 10, title: '晨间新闻', artist: '广播社', station: 'class_xlzx_2028_08', played: true, isPlaying: false, durationSeconds: 180, musicPlatform: 'local', musicId: 'm1', songId: 's1', broadcastPosition: 1, playDate: '2026-10-23' },
    { id: 2, sequence: 20, title: '午间点歌', artist: '各班投稿', station: 'class_xlzx_2028_08', played: false, isPlaying: true, durationSeconds: 240, musicPlatform: 'local', musicId: 'm2', songId: 's2', broadcastPosition: 2, playDate: '2026-10-23' },
    { id: 3, sequence: 30, title: '放学音乐', artist: '广播社', station: 'class_xlzx_2028_08', played: false, isPlaying: false, durationSeconds: 210, musicPlatform: 'local', musicId: 'm3', songId: 's3', broadcastPosition: 3, playDate: '2026-10-23' },
  ];
}

let schedules = [];
function loadState() {
  try {
    if (existsSync(STATE_FILE)) {
      const raw = JSON.parse(readFileSync(STATE_FILE, 'utf8'));
      if (Array.isArray(raw.schedules)) { schedules = raw.schedules; return; }
    }
  } catch { /* 损坏退化种子 */ }
  schedules = seed();
  persist();
}
function persist() {
  try {
    if (!existsSync(DATA_DIR)) mkdirSync(DATA_DIR, { recursive: true });
    writeFileSync(STATE_FILE, JSON.stringify({ v: 1, updatedAt: new Date().toISOString(), schedules }, null, 2), 'utf8');
    return true;
  } catch (e) {
    console.error('[standin] 状态持久化失败:', e.message);
    return false;
  }
}

function sendJSON(res, code, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(code, { 'content-type': 'application/json; charset=utf-8' });
  res.end(body);
}
function readBody(req) {
  return new Promise((resolve, reject) => {
    let data = '';
    req.on('data', (c) => { data += c; if (data.length > 1_000_000) req.destroy(); });
    req.on('end', () => resolve(data));
    req.on('error', reject);
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://127.0.0.1:${PORT}`);
  // GET /api/songs/public —— 无鉴权，返回排期数组
  if (req.method === 'GET' && url.pathname === '/api/songs/public') {
    sendJSON(res, 200, schedules);
    return;
  }
  // POST /api/open/schedules/reorder —— x-api-key 校验 + 应用顺序
  if (req.method === 'POST' && url.pathname === '/api/open/schedules/reorder') {
    const gotKey = req.headers['x-api-key'];
    if (gotKey !== EXPECTED_KEY) {
      sendJSON(res, 401, { ok: false, message: 'invalid x-api-key' });
      return;
    }
    let body;
    try { body = JSON.parse(await readBody(req) || '{}'); }
    catch { sendJSON(res, 400, { ok: false, message: 'bad json' }); return; }
    const incoming = Array.isArray(body.schedules) ? body.schedules : [];
    let updated = 0;
    for (const p of incoming) {
      const id = Number(p && p.id);
      const seq = Number(p && p.sequence);
      if (!Number.isFinite(id) || !Number.isFinite(seq)) continue;
      const it = schedules.find((s) => s.id === id);
      if (!it) continue;
      it.sequence = seq;
      updated++;
    }
    if (updated > 0) persist();
    console.log(`[standin] reorder 收到 x-api-key=${gotKey === EXPECTED_KEY ? 'OK' : 'BAD'} 应用=${updated} 条`);
    sendJSON(res, 200, { ok: true, updated });
    return;
  }
  // 健康检查
  if (req.method === 'GET' && url.pathname === '/healthz') {
    sendJSON(res, 200, { ok: true, schedules: schedules.length });
    return;
  }
  sendJSON(res, 404, { ok: false, message: 'not found' });
});

loadState();
server.listen(PORT, '127.0.0.1', () => {
  console.log(`[standin] 本地 VoiceHub 替身已启动: http://127.0.0.1:${PORT}`);
  console.log(`[standin] GET  /api/songs/public`);
  console.log(`[standin] POST /api/open/schedules/reorder  (x-api-key=${EXPECTED_KEY})`);
  console.log(`[standin] 当前排期 ${schedules.length} 条，状态文件: ${STATE_FILE}`);
});
