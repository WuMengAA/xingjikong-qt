// 星集控云端（被控端最小可用后端）
//   ws://host:8788/ws/agent   被控端接入（先 register，token 校验，之后推帧）
//   ws://host:8788/ws/viewer  管理端接入（D4：看画面 + 刷设备表 + 真下发指令）
//   GET  /api/devices         设备在线表
//   POST /api/instructions    下发指令（设备不在线返回 409，绝不谎报 sent）
//   GET  /api/events          事件流水（最近 50 条）
//   GET  /api/frame?uid=...   最近一帧（管理端 D4 用；内存里，不落盘）
//   GET  /api/instructions/notice?notice_id=...  按通知查回执（站点广播对账，2026-10-04）
//   GET  /api/ota/latest?product=agent           该产品最新版本（OTA 清单，2026-10-05）
//   指令队列：已下发未回执的落盘（见 store.js）——设备重连按原顺序补发、云端重启不丢

import http from 'node:http';
import { readFileSync, writeFileSync, existsSync, createReadStream, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import { WebSocketServer } from 'ws';
import { currentAgentPackage, buildAgentProvisionBat, resolveAsset, agentWsUrl } from './provision.js';
import { PORT, HOST, DEV_TOKEN, DEV_TOKEN_IS_DEFAULT, HANDSHAKE_TIMEOUT_MS, SWEEP_INTERVAL_MS,
  VIEWER_TOKEN, VIEWER_TOKEN_IS_DEFAULT, VIEWER_SECRET,
  HEARTBEAT_TIMEOUT_MS, HEARTBEAT_INTERVAL_MS, PROTOCOL_MODE, QUEUE_SWEEP_INTERVAL_MS,
  OTA_MANIFEST_FILE } from './config.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ENV_FILE = join(__dirname, '..', '.env');
const ADMIN_HTML = join(__dirname, 'admin.html');

/**
 * HTTP 头只允许 latin1。文件名里出现中文/控制字符时，Node 会抛 ERR_INVALID_CHAR
 * 并**终止整个进程**（2026-10-05 实测：一次下载请求就把云端打挂，教室机全体掉线，
 * 要等看门狗 5 分钟才拉回来）。所以凡是写进响应头的名字必须先过这一道。
 */
function asciiHeaderName(name) {
  return String(name).replace(/[^\x20-\x7E]/g, '_').replace(/["\\]/g, '_');
}

/* ---------- .env 读写（管理面板用）---------- */
function readEnvFile() {
  if (!existsSync(ENV_FILE)) return {};
  const lines = readFileSync(ENV_FILE, 'utf8').split(/\r?\n/);
  const out = {};
  for (const line of lines) {
    const m = line.match(/^([A-Z_][A-Z0-9_]*)=(.*)$/);
    if (m) out[m[1]] = m[2];
  }
  return out;
}

function updateEnvFile(updates) {
  if (!existsSync(ENV_FILE)) return false;
  let content = readFileSync(ENV_FILE, 'utf8');
  for (const [key, val] of Object.entries(updates)) {
    if (val === undefined || val === null) continue;
    const regex = new RegExp(`^${key}=.*$`, 'm');
    if (regex.test(content)) {
      content = content.replace(regex, `${key}=${val}`);
    } else {
      content += `\n${key}=${val}`;
    }
  }
  writeFileSync(ENV_FILE, content, 'utf8');
  return true;
}

/** 重启 StelarithCloud 计划任务（写完 .env 后调用）。
 * 用 spawn + detached:true + unref() 产生一个真正脱离父子链的进程，
 * 它等 2 秒（让 HTTP 响应发出去）→ 结束任务 → 等 1 秒 → 重新运行任务。
 * 不用 exec 是因为 exec 的 detached 子进程在父进程退出时仍可能被连带清理，
 * 会导致"面板提示重启但实际没重启、新配置不生效"——这是 2026-10-05 踩过的坑。 */
function restartCloudTask() {
  const script = join(__dirname, '..', '_restart-cloud.cmd');
  const content = [
    '@echo off',
    'timeout /t 2 /nobreak >nul',
    'schtasks /End /TN StelarithCloud >nul 2>&1',
    'timeout /t 1 /nobreak >nul',
    'schtasks /Run /TN StelarithCloud >nul 2>&1',
    'del /q "%~f0"',
  ].join('\r\n');
  try {
    writeFileSync(script, content, 'utf8');
  } catch (e) {
    pushEvent('error', '写重启脚本失败', { error: e.message });
    return;
  }
  const child = spawn('cmd.exe', ['/c', script], {
    detached: true, stdio: 'ignore', windowsHide: true,
  });
  child.unref();
  pushEvent('info', '已触发云端重启（2 秒后执行）');
}
import { verifyViewerTicket } from './ticket.js';
import { initStore, sweepQueue, storeReady, receiptsByNotice } from './store.js';
import { latestFor } from './ota.js';
import { parseIncoming, parseFrame, wrapOutgoing, errPayload, ERR, PROTOCOL_VERSION } from './protocol.js';

/** 发一条协议消息（自动按对端版本选格式），失败不静默。 */
function snd(ws, type, payload, id = '') {
  try { ws.send(wrapOutgoing(ws.__v1 === true, type, payload, id)); } catch (e) {
    pushEvent('error', '给对端发消息失败', { type, error: e.message });
  }
}

/** 发一条协议错误；code 里标了要关连接的，发完就关（并写明原因，不许静默断）。 */
function sndErr(ws, code, detail) {
  const p = errPayload(code, detail);
  snd(ws, 'error', p);                        // v1：统一走 error 通道
  if (ws.__v1 !== true) snd(ws, p.code, p);   // 旧客户端：额外补一条它认得的类型，避免它看不懂 error
  const e = ERR[code];
  if (e?.close) { try { ws.close(e.close, code); } catch { /* 已断就算了 */ } }
  pushEvent('warn', `协议错误 ${code}`, { detail: detail || e?.msg || '' });
}
import {
  pushEvent, markConnected, markDisconnected, addFrame, setRecentFrame,
  listDevices, getDevice, sendInstruction, listEvents, listPending, ensureEventsFile,
  addViewer, removeViewer, subscribeViewer, unsubscribeViewer, pinViewer, autoSubscribeViewers,
  broadcastFrame, broadcastDevices, broadcastToViewers, recordResult, getResult, viewerCount,
  touchHeartbeat, sweepStale,
  rtcSignalToAgent, rtcRelayToViewers, rtcRelayToAgent,
} from './registry.js';

/**
 * 设备重连成功后，把它"已下发但没等到回执"的指令按原下发顺序补发。
 * 这是本功能的核心动作：没有它，教室机离线一次、那条指令就永久漏了。
 * 补发用**原来的 id**（设备回执按 id 配对），状态仍是未回执，等设备真回执了才销账。
 */
function replayPending(ws, uid) {
  const items = listPending(uid);
  if (!items.length) return;
  let sent = 0;
  for (const it of items) {
    const text = (ws.__v1 === true)
      ? wrapOutgoing(true, 'instruction', { action: it.action, params: it.params }, String(it.id))
      : JSON.stringify({ type: 'instruction', action: it.action, params: it.params, id: it.id, at: new Date(it.dispatchedAt).toISOString() });
    try {
      ws.send(text);
      sent++;
    } catch (e) {
      pushEvent('error', '重连补发失败：写 socket 出错', { uid, id: it.id, action: it.action, error: e.message });
      break;
    }
  }
  pushEvent('info', '设备重连：补发未回执指令', { uid, total: items.length, sent });
}

ensureEventsFile();
initStore();                 // 打开指令队列（已下发未回执的落盘文件），重启后从这里恢复
if (!storeReady()) pushEvent('error', '指令队列不可持久化：本次运行期间设备重连补发与重启不丢均失效');

// 启动先清一次：把上轮残留的过期未回执/已回执行处理掉，再开始收新指令
{
  const r = sweepQueue();
  if (r.expiredByTtl || r.expiredByCap || r.purged) {
    pushEvent('info', '启动队列清扫完成', r);
  }
}

if (DEV_TOKEN_IS_DEFAULT) {
  console.warn(`[cloud] ⚠ 正在使用内置开发令牌 "${DEV_TOKEN}"，生产环境请设环境变量 CLOUD_WS_TOKEN`);
}
if (VIEWER_TOKEN_IS_DEFAULT) {
  console.warn('[cloud] ⚠ 管理端令牌未单独配置（CLOUD_VIEWER_TOKEN），正回落到内置开发令牌；生产环境必须设');
}
if (!VIEWER_SECRET) {
  console.warn('[cloud] ⚠ 未配置 CLOUD_VIEWER_SECRET：浏览器票据通道关闭（桌面端长期令牌通道仍可用）');
}
console.log(`[cloud] 云端监听 http://${HOST}:${PORT} （被控端路径 /ws/agent，协议模式 ${PROTOCOL_MODE}）`);

const server = http.createServer((req, res) => {
  // 兜底一层：任何漏网的同步异常都不许掀翻进程。
  // 云端是教室里几十台机器共用的入口，崩一次 = 全体掉线，等看门狗回来就是 5 分钟黑屏。
  try {
  const u = new URL(req.url, `http://127.0.0.1:${PORT}`);
  const send = (code, obj) => {
    const body = JSON.stringify(obj);
    res.writeHead(code, { 'content-type': 'application/json; charset=utf-8', 'content-length': Buffer.byteLength(body) });
    res.end(body);
  };

  // /assets/* 静态托管（2026-10-05 · 面板化装机）：教室机取安装包和装机脚本用。
  // ⚠️ 刻意放在下面那道管理面鉴权**之前** —— 教室机拿不到管理令牌，
  //    但 bat 里本来就带着这枚"一机一密"的 agent 令牌，凑不出第二把门。
  //    所以这里的安全边界只有一条：目录白名单（见 provision.js 的 ASSET_ALLOW_EXT），
  //    保证 /assets 下不存在任何能被拖到公网的秘密文件。
  if (u.pathname.startsWith('/assets/')) {
    const file = resolveAsset(u.pathname);
    if (!file) return send(404, { ok: false, error: 'assets 里没有这个文件或扩展名不允许'});
    const st = statSync(file);
    // ⚠️ HTTP 头只允许 latin1：中文/非 ASCII 字符写进头里，Node 会直接抛
    //    ERR_INVALID_CHAR 并**终止整个进程**（2026-10-05 实测：一次下载把云端打挂了，
    //    教室机全掉线，要等看门狗 5 分钟才拉回来）。所以头里的名字一律 ASCII 化。
    const fname = asciiHeaderName(file.split(/[\\/]/).pop());
    res.writeHead(200, {
      'content-type': file.endsWith('.ps1') ? 'text/plain; charset=utf-8' :
                      file.endsWith('.json') ? 'application/json; charset=utf-8' :
                      file.endsWith('.txt') || file.endsWith('.sha256') ? 'text/plain; charset=utf-8' :
                      'application/octet-stream',
      'content-length': st.size,
      // 装机 bat 每次都该拿到最新的脚本（改了云端脚本要立刻在教室机生效）
      'cache-control': 'no-store',
      'content-disposition': `attachment; filename="${fname}"`,
    });
    return createReadStream(file).pipe(res);
  }

  // HTTP 管理面鉴权（2026-10-04 · 乙阶段收尾 + 补洞）：
  // /api/instructions / /api/devices / /api/events / /api/frame / /api/instructions/pending
  // 是"发指令/读设备表/读画面/读事件"的管理操作，此前完全无鉴权 ——
  // 本机任意进程都能发指令、读教室机画面（被控端是 Windows 单机场景，
  // 可信边界收紧到"持 viewer 令牌"）。/api/frame 是实时画面，最敏感，必须关死。
  // 用 VIEWER_TOKEN（与桌面管理端同一信任级），Bearer 头；缺/错 → 401 fail-closed。
  // ⚠️ 站点广播（broadcast.ts）是唯一合法调用方，已同步带 Bearer 头。
  // ⚠️ 用 startsWith 而不是精确相等：/api/admin/provision 下面还挂着 /status 子路由，
  //    精确相等会把 status 漏在鉴权外面（2026-10-05 实测就是这么漏的，已修）。
  const isAdminApi = u.pathname === '/api/admin/config' || u.pathname === '/api/admin/ota' ||
    u.pathname.startsWith('/api/admin/provision');
  const isManageApi =
    u.pathname === '/api/instructions' || u.pathname === '/api/devices' ||
    u.pathname === '/api/events' || u.pathname === '/api/frame' ||
    u.pathname === '/api/instructions/pending' || u.pathname === '/api/instructions/notice' ||
    u.pathname === '/api/ota/latest' || isAdminApi;
  if (isManageApi) {
    const auth = req.headers.authorization || '';
    const token = auth.toLowerCase().startsWith('bearer ') ? auth.slice(7).trim() : '';
    if (!token || token !== VIEWER_TOKEN) {
      pushEvent('warn', 'HTTP 管理面鉴权失败，已拒绝', { path: u.pathname });
      return send(401, { ok: false, error: '未授权：需要 Bearer <CLOUD_VIEWER_TOKEN>' });
    }
  }

  if (req.method === 'GET' && u.pathname === '/api/devices') {
    return send(200, { ok: true, count: listDevices().length, devices: listDevices() });
  }

  if (req.method === 'GET' && u.pathname === '/api/events') {
    return send(200, { ok: true, events: listEvents(Number(u.searchParams.get('limit') || 50)) });
  }

  if (req.method === 'GET' && u.pathname === '/api/instructions/pending') {
    const uid = u.searchParams.get('uid');
    // 带 uid → 该设备"已下发未回执"的数组（按原顺序）；不带 → 全部设备按 uid 分组
    return send(200, { ok: true, pending: listPending(uid) });
  }

  if (req.method === 'GET' && u.pathname === '/api/instructions/notice') {
    // 站点广播对账：按 notice_id 查这批指令的回执状态（2026-10-04）
    const noticeId = Number(u.searchParams.get('notice_id') || 0);
    if (!noticeId) return send(400, { ok: false, error: '缺 notice_id' });
    return send(200, { ok: true, noticeId, receipts: receiptsByNotice(noticeId) });
  }

  if (req.method === 'GET' && u.pathname === '/api/ota/latest') {
    // OTA：声明某产品的最新版本（供管理端/站点比对在线设备版本，决定是否下发升级）。
    // 未发布过（无 ota.json）→ latest:null，如实相告，不假装"已是最新"。
    const product = (u.searchParams.get('product') || 'agent').trim();
    const latest = latestFor(product);
    return send(200, { ok: true, product, latest });
  }

  if (req.method === 'GET' && u.pathname === '/api/frame') {
    const uid = u.searchParams.get('uid');
    const d = uid ? getDevice(uid) : null;
    if (!d || !d.recentFrame) return send(404, { ok: false, error: '该设备没有可取的帧（可能没推过或已掉线）' });
    res.writeHead(200, { 'content-type': 'image/jpeg', 'content-length': d.recentFrame.length });
    return res.end(d.recentFrame);
  }

  if (req.method === 'POST' && u.pathname === '/api/instructions') {
    let body = '';
    req.on('data', (c) => {
      body += c;
      if (body.length > 1e6) { req.destroy(); } // 防失控上报
    });
    req.on('end', () => {
      let payload;
      try {
        payload = JSON.parse(body || '{}');
      } catch (e) {
        return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` });
      }
      const { uid, action, params } = payload;
      if (!uid || !action) return send(400, { ok: false, error: '缺 uid 或 action' });
      const r = sendInstruction(uid, String(action), params ?? {});
      if (r.state === 'sent') return send(200, { ok: true, state: 'sent', uid, action });
      return send(409, { ok: false, state: r.state, uid, action, detail: r.detail });
    });
    return undefined;
  }

  /* ---------- 管理面板（2026-10-05：让用户告别命令行）---------- */
  // GET /admin  → 返回 admin.html（管理面板单页）
  // GET /api/admin/config  → 读当前 .env 配置（令牌脱敏显示）
  // POST /api/admin/config  → 改 .env（换令牌/放开0.0.0.0）→ 自动重启
  // POST /api/admin/ota     → 写 ota.json 发布新版本

  if (req.method === 'GET' && u.pathname === '/admin') {
    if (!existsSync(ADMIN_HTML)) return send(404, { ok: false, error: 'admin.html 不存在' });
    const html = readFileSync(ADMIN_HTML, 'utf8');
    res.writeHead(200, { 'content-type': 'text/html; charset=utf-8', 'content-length': Buffer.byteLength(html) });
    return res.end(html);
  }

  if (req.method === 'GET' && u.pathname === '/api/admin/config') {
    const env = readEnvFile();
    return send(200, {
      ok: true,
      host: env.CLOUD_WS_HOST || '0.0.0.0',
      port: env.CLOUD_WS_PORT || '8788',
      wsToken: env.CLOUD_WS_TOKEN || '',
      viewerToken: env.CLOUD_VIEWER_TOKEN || '',
      wsTokenIsDefault: (env.CLOUD_WS_TOKEN || 'dev-cloud-token') === 'dev-cloud-token',
    });
  }

  if (req.method === 'POST' && u.pathname === '/api/admin/config') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 1e5) req.destroy(); });
    req.on('end', () => {
      let payload;
      try { payload = JSON.parse(body || '{}'); }
      catch (e) { return send(400, { ok: false, error: 'JSON 解析失败: ' + e.message }); }

      const updates = {};
      if (payload.host !== undefined) {
        if (payload.host !== '127.0.0.1' && payload.host !== '0.0.0.0')
          return send(400, { ok: false, error: 'host 只能是 127.0.0.1 或 0.0.0.0' });
        updates.CLOUD_WS_HOST = payload.host;
      }
      if (payload.wsToken !== undefined) {
        if (payload.wsToken.length < 16)
          return send(400, { ok: false, error: '设备令牌太弱（最少16位随机）' });
        updates.CLOUD_WS_TOKEN = payload.wsToken;
      }
      if (payload.viewerToken !== undefined) {
        if (payload.viewerToken.length < 16)
          return send(400, { ok: false, error: '管理令牌太弱（最少16位随机）' });
        updates.CLOUD_VIEWER_TOKEN = payload.viewerToken;
      }

      const ok = updateEnvFile(updates);
      if (!ok) return send(500, { ok: false, error: '.env 文件不存在或写入失败' });

      pushEvent('info', '管理面板修改了配置', { keys: Object.keys(updates) });
      // 写完 .env → 重启云端（让响应先发出去）
      restartCloudTask();
      return send(200, { ok: true, updated: Object.keys(updates), note: '云端即将自动重启以加载新配置' });
    });
    return undefined;
  }

  if (req.method === 'POST' && u.pathname === '/api/admin/ota') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 1e5) req.destroy(); });
    req.on('end', () => {
      let payload;
      try { payload = JSON.parse(body || '{}'); }
      catch (e) { return send(400, { ok: false, error: 'JSON 解析失败: ' + e.message }); }
      const { product = 'agent', version, url, sha256, size, notes, mandatory } = payload;
      if (!version || !url) return send(400, { ok: false, error: '缺 version 或 url' });

      // 读取现有清单（有则改，无则建）
      let manifest = { products: {} };
      if (existsSync(OTA_MANIFEST_FILE)) {
        try { manifest = JSON.parse(readFileSync(OTA_MANIFEST_FILE, 'utf8')); }
        catch { manifest = { products: {} }; }
      }
      if (!manifest.products) manifest.products = {};
      manifest.products[product] = {
        version, url, sha256: sha256 || '', size: Number(size) || 0,
        notes: notes || '', mandatory: !!mandatory,
      };
      try {
        writeFileSync(OTA_MANIFEST_FILE, JSON.stringify(manifest, null, 2), 'utf8');
        pushEvent('info', '管理面板发布了 OTA', { product, version });
        return send(200, { ok: true, product, version, note: 'OTA 清单已写入，立即生效' });
      } catch (e) {
        return send(500, { ok: false, error: '写入 ota.json 失败: ' + e.message });
      }
    });
    return undefined;
  }

  // GET /api/admin/provision?uid=class_xxx_2028_08  → 一枚这台机器专属的装机 bat
  // 面板点"下载装机包"就走这里；教室机双击 bat 即完成装机（流程见 provision.js 头注释）。
  if (req.method === 'GET' && u.pathname === '/api/admin/provision') {
    const uid = (u.searchParams.get('uid') || '').trim();
    if (!uid) return send(400, { ok: false, error: '缺 uid' });
    if (uid.length > 64 || !/^[\w.-]+$/.test(uid)) {
      return send(400, { ok: false, error: 'uid 只允许字母/数字/下划线/点/横线，最长 64' });
    }
    let bat;
    try {
      bat = buildAgentProvisionBat(uid, DEV_TOKEN);
    } catch (e) {
      return send(500, { ok: false, error: e.message });
    }
    // 中文名字只在前端 blob 下载时用（浏览器认）；HTTP 头必须 ASCII，见上面 /assets 的注释。
    const name = `provision-${uid}.bat`;
    res.writeHead(200, {
      'content-type': 'application/octet-stream',
      'content-length': Buffer.byteLength(bat),
      'cache-control': 'no-store',
      'content-disposition': `attachment; filename="${name}"`,
    });
    pushEvent('info', '生成装机包', { uid });
    return res.end(bat);
  }

  // GET /api/admin/provision/status → 面板要知道"安装包放了没 / 云端对外地址是什么"
  if (req.method === 'GET' && u.pathname === '/api/admin/provision/status') {
    return send(200, {
      ok: true,
      package: currentAgentPackage(),
      script: 'provision/install-agent.ps1',
      agentWsUrl: agentWsUrl(),
    });
  }

  return send(404, { ok: false, error: `未知道路 ${req.method} ${u.pathname}` });
  } catch (e) {
    console.error('[cloud] FAIL HTTP 处理异常：', e);
    try {
      pushEvent('error', 'HTTP 处理异常', { path: String(req.url || '').slice(0, 200), msg: e.message });
      if (res.headersSent) { res.end(); return; }
      const body = JSON.stringify({ ok: false, error: '云端内部异常：' + e.message });
      res.writeHead(500, { 'content-type': 'application/json; charset=utf-8', 'content-length': Buffer.byteLength(body) });
      res.end(body);
    } catch { /* 连兜底都失败就别再挣扎了 */ }
  }
});

const wss = new WebSocketServer({ noServer: true });        // 被控端接入
const viewerWss = new WebSocketServer({ noServer: true });  // 管理端（看的人）

server.on('upgrade', (req, socket, head) => {
  const u = new URL(req.url, 'http://127.0.0.1');
  if (u.pathname === '/ws/agent') {
    wss.handleUpgrade(req, socket, head, (ws) => wss.emit('connection', ws, req));
    return;
  }
  if (u.pathname === '/ws/viewer') {
    viewerWss.handleUpgrade(req, socket, head, (ws) => viewerWss.emit('connection', ws, req));
    return;
  }
  pushEvent('warn', '拒绝了非 agent/viewer 路径的 WS 升级', { path: u.pathname });
  socket.write('HTTP/1.1 400 Bad Request\r\n\r\n');
  socket.destroy();
});

/* ---------- 管理端（viewer）：只看 + 真下发 ---------- */
/**
 * 管理端鉴权：长期令牌 或 站点签发的短票据，任一通过即可。
 * 令牌给桌面端（Qt viewer，守得住密钥）；票据给浏览器（票只有 2 分钟，抄走也很快失效）。
 * @returns {{ok:boolean, mode?:'token'|'ticket', uid?:string|null, reason?:string}}
 */
function authorizeViewer(msg) {
  const token = typeof msg.token === 'string' ? msg.token.trim() : '';
  if (token) {
    if (token === VIEWER_TOKEN) return { ok: true, mode: 'token', uid: null };
    return { ok: false, reason: '管理端令牌不对' };
  }
  const ticket = typeof msg.ticket === 'string' ? msg.ticket.trim() : '';
  if (ticket) return verifyViewerTicket(ticket);
  return { ok: false, reason: 'auth 消息里既没有 token 也没有 ticket' };
}

/** 管理端通过鉴权之后才做的事：登记 + 补订阅 + 推初始设备表。 */
function onViewerAuthed(ws, who) {
  addViewer(ws);
  pushEvent('info', '管理端已接入（已鉴权）', { viewers: viewerCount(), mode: who.mode, uid: who.uid || undefined });

  // 两条补订阅路径都要有，缺一条就黑屏：
  //   ① 管理端后到（教室机早就在线）→ 这里立刻订阅第一台；
  //   ② 教室机后到（管理端先开机）  → autoSubscribeViewers 在设备上线时补（见 markConnected 之后那步）。
  const firstOnline = listDevices()[0]?.uid;
  if (firstOnline) {
    if (subscribeViewer(ws, firstOnline)) snd(ws, 'subscribed', { uid: firstOnline, auto: true });
  }
  snd(ws, 'viewer-ready', { viewers: viewerCount() });
  snd(ws, 'devices', { devices: listDevices(), at: new Date().toISOString() });
}

viewerWss.on('connection', (ws) => {
  // 关键：鉴权通过前绝不 addViewer()。否则设备表和画面帧会被推给没通过鉴权的连接，
  // 加了 auth 也等于没加——这是最容易写歪的一处。
  let authed = false;
  const deadline = setTimeout(() => {
    if (authed) return;
    pushEvent('warn', '管理端握手超时：连上后没在限定时间内鉴权，已踢掉');
    try { ws.close(4002, 'viewer handshake timeout'); } catch { /* 已断就算了 */ }
  }, HANDSHAKE_TIMEOUT_MS);

  ws.on('message', (data, isBinary) => {
    if (isBinary) {
      pushEvent('warn', '管理端发了二进制消息（管理端只该发文本）', { bytes: data.length });
      return;
    }
    // 入向统一走协议层：v1 信封与旧扁平格式都规范化成同一种内部表示
    const p = parseIncoming(data.toString('utf8'));
    if (!p.ok) { sndErr(ws, p.code, p.detail); return; }
    if (!p.legacy) ws.__v1 = true;   // 对端说 v1 → 之后回它 v1 信封

    if (!authed) {
      if (p.type !== 'auth') {
        pushEvent('warn', '管理端未鉴权就发消息，已拒绝', { type: String(p.type).slice(0, 32) });
        if (ws.__v1 === true) sndErr(ws, 'AUTH_REQUIRED', `先发 auth，收到的是 ${p.type}`);
        else { snd(ws, 'auth-fail', { reason: '请先发 {"type":"auth","token":"…"} 或带 ticket' }); try { ws.close(4003, 'auth required'); } catch {} }
        return;
      }
      const r = authorizeViewer(p.payload);
      if (!r.ok) {
        // 鉴权失败必须报出来（不许静默断掉让人以为"网络问题"）
        pushEvent('warn', '管理端鉴权失败，连接已拒绝', { reason: r.reason });
        if (ws.__v1 === true) sndErr(ws, 'AUTH_FAILED', r.reason);
        else { snd(ws, 'auth-fail', { reason: r.reason }); try { ws.close(4006, 'viewer auth failed'); } catch {} }
        return;
      }
      authed = true;
      clearTimeout(deadline);
      // 旧客户端认的是 auth-ok，v1 客户端收 authed（统一命名，旧名进兼容清单）
      snd(ws, ws.__v1 === true ? 'authed' : 'auth-ok', { mode: r.mode, uid: r.uid ?? null });
      onViewerAuthed(ws, r);
      return;
    }

    switch (p.type) {
      // WebRTC 信令（P1-A 实时画面）：云端只转发、不解析内容；对方不在线/没订阅都如实回执，不静默
      case 'rtc-offer':
      case 'rtc-answer':
      case 'rtc-ice': {
        const uid = p.payload.uid;
        // 管理端主动建 offer 时用（from='viewer'）；收到 from='cloud' 说明是订阅开关，不该走信令分支
        const r = rtcRelayToAgent(uid, p.type, p.payload);
        snd(ws, 'rtc-relayed', { uid, ok: r.ok, to: r.to, type: p.type, detail: r.detail || '' });
        return;
      }
      case 'subscribe': {
        pinViewer(ws); // 管理端自己挑了 → 以后不再自动给它补别的设备
        const uid = p.payload.uid;
        const ok = subscribeViewer(ws, uid);
        snd(ws, 'subscribed', { uid: ok ? uid : null, ok });
        // 有人在看了 -> 通知设备把 WebRTC 实时推流起起来（设备端起不来会自己回落到截图像轮播）
        if (ok) rtcSignalToAgent(uid, 'rtc-start', { uid });
        return;
      }
      case 'unsubscribe': {
        // 按需拉流预留：不再看这台就退订，云端将来可据此让设备停推
        const ok = unsubscribeViewer(ws, p.payload.uid);
        snd(ws, 'unsubscribed', { uid: ok ? p.payload.uid : null, ok });
        if (ok) rtcSignalToAgent(p.payload.uid, 'rtc-stop', { uid: p.payload.uid });
        return;
      }
      case 'devices':
        snd(ws, 'devices', { devices: listDevices(), at: new Date().toISOString() });
        return;
      case 'instruction': {
        const { uid, action, params } = p.payload;
        if (!uid || !action) {
          sndErr(ws, 'BAD_PAYLOAD', 'instruction 缺 uid 或 action');
          return;
        }
        const r = sendInstruction(uid, String(action), params ?? {});
        // 云端只说它发送成功与否；到没到机器、有没有真做，是设备回执的事
        snd(ws, 'instruction-result', {
          ok: r.state === 'sent', state: r.state, uid, action,
          detail: r.detail || '云端已写入被控端连接',
        });
        return;
      }
      default:
        // 未知类型**不关闭连接**：允许新客户端试探新能力（协议规范第十节）
        sndErr(ws, 'UNKNOWN_TYPE', p.type);
        return;
    }
  });

  ws.on('close', () => {
    clearTimeout(deadline);
    removeViewer(ws);
    if (authed) pushEvent('info', '管理端断开', { viewers: viewerCount() });
  });
  ws.on('error', (e) => { pushEvent('error', '管理端连接出错', { error: e.message }); });
});

// 管理端刷新设备表：有 viewer 在才推，没人看就不空转
setInterval(() => {
  if (viewerCount() > 0) {
    const n = broadcastDevices();
    if (n === 0) console.log('[cloud] 管理端刷新：推了设备表但没收到回执（连接已失效）');
  }
}, 2000);

// 离线清扫：机器掉电/拔网线时 TCP 不会立刻关，只等 close 事件会出现"永远在线"的假绿。
// 每 SWEEP_INTERVAL_MS 扫一次，把超过 HEARTBEAT_TIMEOUT_MS 没心跳也没帧的设备判离线。
setInterval(() => {
  try {
    const stale = sweepStale();
    if (stale.length > 0) broadcastDevices();   // 立刻把"它没了"推给管理端，别等下一次轮询
  } catch (e) {
    pushEvent('error', '离线清扫异常', { error: e.message });
  }
}, SWEEP_INTERVAL_MS);

// 指令队列清扫：过期未回执作废 / 每台超上限丢最旧 / 已回执超保留期删除 —— 否则队列表只增不减。
setInterval(() => {
  try {
    const r = sweepQueue();
    if (r.expiredByTtl || r.expiredByCap || r.purged) pushEvent('info', '指令队列清扫完成', r);
  } catch (e) {
    pushEvent('error', '指令队列清扫异常', { error: e.message });
  }
}, QUEUE_SWEEP_INTERVAL_MS);

wss.on('connection', (ws) => {
  let registered = false;
  let uid = null;
  const deadline = setTimeout(() => {
    if (!registered) {
      pushEvent('error', '握手超时：连上后没发 register，已踢掉', { uid: uid || '(未知)' });
      ws.close(4002, 'handshake timeout');
    }
  }, HANDSHAKE_TIMEOUT_MS);

  const finish = () => { clearTimeout(deadline); };

  ws.on('message', (data, isBinary) => {
    if (!registered) {
      // 握手：只认 register（v1 信封或旧扁平格式都行），不是就明确拒绝，不重置超时
      const p = parseIncoming(data.toString('utf8'));
      if (!p.ok) { finish(); sndErr(ws, p.code, p.detail); return; }
      if (!p.legacy) ws.__v1 = true;
      if (p.type !== 'register') {
        finish();
        sndErr(ws, 'AUTH_REQUIRED', `握手期只认 register，收到的是 ${p.type}`);
        return;
      }
      const { uid: ruid, token, version } = p.payload;
      if (token !== DEV_TOKEN) {
        finish();
        pushEvent('error', '设备令牌校验失败，连接已拒绝', { uid: String(ruid ?? '') });
        sndErr(ws, 'BAD_TOKEN', '设备令牌与云端 CLOUD_WS_TOKEN 不一致');
        return;
      }
      uid = String(ruid || '').trim();
      if (!uid) {
        finish();
        pushEvent('error', 'register 没带 uid，拒绝');
        sndErr(ws, 'BAD_PAYLOAD', 'register 缺 uid');
        return;
      }
      registered = true;
      finish();
      markConnected(uid, String(version || ''), ws);
      // 设备上线：把还没挑设备的管理端补上订阅（否则老师先开软件 → 永远黑屏）
      const auto = autoSubscribeViewers(uid);
      if (auto > 0) pushEvent('info', '已自动给管理端补上订阅', { uid, viewers: auto });
      // 回执：v1 收 registered（顺带**下发心跳参数**，不再靠两端各猜一个数字）；旧客户端仍收 registration-ok
      snd(ws, ws.__v1 === true ? 'registered' : 'registration-ok', {
        uid,
        server: 'stelarith-cloud-ws/0.4.0',
        heartbeatMs: HEARTBEAT_INTERVAL_MS,
        timeoutMs: HEARTBEAT_TIMEOUT_MS,
      });
      // 重连补发：这台机器掉线期间"已下发未回执"的指令，按原顺序重发给它（内部无待补发则不动作）。
      replayPending(ws, uid);
      return;
    }

    if (isBinary) {
      const f = parseFrame(Buffer.from(data));
      if (!f.ok) { sndErr(ws, f.code, f.detail); return; }
      const seq = f.header?.seq ?? 0;
      const info = addFrame(uid, f.payload.length);
      if (!info) {
        pushEvent('error', '收到帧但设备不在表里（异常状态）', { uid });
        return;
      }
      setRecentFrame(uid, f.payload);
      // 推给订阅了这个 uid 的管理端（谁订阅推谁，不群发）
      const r = broadcastFrame(uid, f.payload, seq);
      if (r.hits > 0) pushEvent('info', '帧已推给管理端', { uid, viewers: r.hits, bytes: r.bytes, seq });
      else pushEvent('warn', '帧推了但没人订阅（管理端没连或没选这台）', { uid });
      return;
    }

    const p = parseIncoming(data.toString('utf8'));
    if (!p.ok) { sndErr(ws, p.code, p.detail); return; }
    if (!p.legacy) ws.__v1 = true;

    if (p.type === 'rtc-offer' || p.type === 'rtc-answer' || p.type === 'rtc-ice') {
      // 设备 -> 管理端 的信令：只投给订阅了这台的管理端（同源的已在 registry 层被防回声挡掉）
      const r = rtcRelayToViewers(uid, p.type, p.payload);
      snd(ws, 'rtc-relayed', { uid, ok: r.ok, to: r.to, type: p.type, detail: r.detail || '' });
      return;
    }

    if (p.type === 'rtc-start' || p.type === 'rtc-stop') {
      // 云端的「有人在看了 / 没人看了」开关：设备端自己起停 WebRTC 推流，这里只记一条事件流水便于排障
      pushEvent('info', '设备收到 WebRTC 开关 ' + p.type, { uid });
      return;
    }

    if (p.type === 'heartbeat') {
      // 存活信号：与帧解耦（按需拉流后没人看的机器不推帧，帧不能当存活依据）
      const ok = touchHeartbeat(uid);
      if (!ok) pushEvent('error', '收到心跳但设备不在在线表里（异常状态）', { uid });
      return;
    }

    if (p.type === 'receipt') {
      // 被控端真执行完了，回的是 done/failed（不是 received）。必须如实记、如实转。
      const { id, action, result, error, data } = p.payload;
      const rec = recordResult(id, uid, action, result, error, data);
      // 转发给所有管理端：状态行从「已下发」变成「真做了 / 真没做成」，
      // 并带上动作的附加数据（进程列表 / 日志行 / 音量实际值 / 截图路径）
      broadcastToViewers('instruction-result', {
        uid,
        action: rec.action,
        id,
        state: 'executed',
        result: rec.result,
        error: rec.error,
        data: rec.data,
      });
      return;
    }

    if (p.type === 'event') {
      // 设备侧事件上报（将来回吐站点入库用；现在先如实记进事件流水，不丢）
      const { level, msg, extra } = p.payload;
      pushEvent(level === 'error' ? 'error' : (level === 'warn' ? 'warn' : 'info'),
        `设备上报：${String(msg ?? '')}`, { uid, ...(extra || {}) });
      return;
    }

    // 未知类型**不关闭连接**：允许新客户端试探新能力（协议规范第十节）
    sndErr(ws, 'UNKNOWN_TYPE', p.type);
  });

  ws.on('close', () => { finish(); if (registered) markDisconnected(ws); });

  ws.on('error', (e) => {
    pushEvent('error', 'WS 连接出错', { uid: uid || '(未知)', error: e.message });
  });
});

server.listen(PORT, HOST, () => {
  pushEvent('info', '云端已启动，等被控端接入', { host: HOST, port: PORT });
});

server.on('error', (e) => {
  console.error(`[cloud] FAIL 端口占用或启动失败: ${e.message}`);
  process.exit(1);
});
