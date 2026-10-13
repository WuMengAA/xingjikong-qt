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
import { readFileSync, writeFileSync, existsSync, createReadStream, statSync, mkdirSync, readdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';   // resolve：录制根目录支持 CLOUD_RECORDINGS_DIR 覆盖
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import { WebSocketServer } from 'ws';
import { currentAgentPackage, currentViewerPackage, buildProvisionBat, recordProvision,
  recentProvision, resolveAsset, agentWsUrl, viewerWsUrl } from './provision.js';
import { ensureQmlPatch, qmlPatchManifest, qmlPatchZipPath } from './qml-patch.js';
import { PORT, HOST, DEV_TOKEN, DEV_TOKEN_IS_DEFAULT, HANDSHAKE_TIMEOUT_MS, SWEEP_INTERVAL_MS,
  VIEWER_TOKEN, VIEWER_TOKEN_IS_DEFAULT, VIEWER_SECRET,
  HEARTBEAT_TIMEOUT_MS, HEARTBEAT_INTERVAL_MS, PROTOCOL_MODE, QUEUE_SWEEP_INTERVAL_MS,
  OTA_MANIFEST_FILE } from './config.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ENV_FILE = join(__dirname, '..', '.env');
const ADMIN_HTML = join(__dirname, 'admin.html');

/** 管理台「用网站账号登录」的回跳 server（127.0.0.1 临时端口）。没配全站点就一直是 null。 */
let adminOAuth = null;

/**
 * HTTP 头只允许 latin1。文件名里出现中文/控制字符时，Node 会抛 ERR_INVALID_CHAR
 * 并**终止整个进程**（2026-10-05 实测：一次下载请求就把云端打挂，教室机全体掉线，
 * 要等看门狗 5 分钟才拉回来）。所以凡是写进响应头的名字必须先过这一道。
 */
function asciiHeaderName(name) {
  return String(name).replace(/[^\x20-\x7E]/g, '_').replace(/["\\]/g, '_');
}

/**
 * 版本号比较已统一到 ota.js 的 cmpVersion（2026-10-08）：本文件不再保留本地副本。
 * 曾有一份本地 split('.') 实现（不认 alpha/rc），与 ota.js 导入的同名函数在本模块
 * 作用域内重复声明 → SyntaxError，云端进程启动即崩（10-08 重启 8788 实测）。
 * 语义统一以 ota.js 为准：parseVersion 解析 x.y.z(-(alpha|rc).n)、解析失败返回 0（不比）。
 */

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
import { verifyViewerTicket, mintViewerTicket, verifyAgentTicket } from './ticket.js';
import { createAdminOAuthServer, currentRedirectUri, beginAuthorization } from './adminoauth.js';
import { initStore, sweepQueue, storeReady, receiptsByNotice } from './store.js';
import { latestFor, cmpVersion } from './ota.js';
import { parseIncoming, parseFrame, wrapOutgoing, errorPayload, ERR, PROTOCOL_VERSION } from './protocol.js';

/** 发一条协议消息（自动按对端版本选格式），失败不静默。 */
function snd(ws, type, payload, id = '') {
  try { ws.send(wrapOutgoing(ws.__v1 === true, type, payload, id)); } catch (e) {
    pushEvent('error', '给对端发消息失败', { type, error: e.message });
  }
}

/** 发一条协议错误；code 里标了要关连接的，发完就关（并写明原因，不许静默断）。 */
function sndErr(ws, code, detail) {
  const p = errorPayload(code, detail);       // 数字码 ecode + 语义码 code（设计文档第七部分）
  snd(ws, 'error', p);                        // v1：统一走 error 通道
  if (ws.__v1 !== true) snd(ws, p.code, p);   // 旧客户端：额外补一条它认得的类型，避免它看不懂 error
  const e = ERR[code];
  if (e?.close) { try { ws.close(e.close, code); } catch { /* 已断就算了 */ } }
  pushEvent('warn', `协议错误 ${p.ecode || code}`, { detail: detail || p.message || '' });
}
import {
  pushEvent, markConnected, markDisconnected, addFrame, setRecentFrame,
  listDevices, getDevice, sendInstruction, listEvents, listPending, ensureEventsFile,
  relayTerminalFrame, relayToSubscribers,
  addViewer, removeViewer, subscribeViewer, unsubscribeViewer, pinViewer, autoSubscribeViewers,
  broadcastFrame, broadcastDevices, broadcastToViewers, recordResult, getResult, viewerCount,
  touchHeartbeat, sweepStale,
  rtcSignalToAgent, rtcRelayToViewers, rtcRelayToAgent,
  broadcastAudioToAgents, setAudioBroadcasting, isAudioBroadcasting,
  duplexActive, sendAudioToTeacher,
  broadcastScreenToAgents, setScreenBroadcasting, isScreenBroadcasting,
  // 2026-10-08 分班制 + 已知设备表 + 云端存储信息
  setDeviceMeta,
  bindDeviceToClass, unbindDeviceFromClass, listClasses, getDevicesOfClass,
  getStorageInfo, isClassCodeValid,
} from './registry.js';
// 设备报修（2026-10-07，《设备报修-第一版设计》落地）：内存表 + repairs.json 落盘
import { loadRepairs, listRepairs, createRepair, updateRepair, deleteRepair } from './repairs.js';
import { expireTerminalHistory } from './store.js';

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
// 止血：历史遗留的关机类未回执指令必须连根拔掉，否则重连补发会让机器反复重启（恶性BUG）
{
  const n = expireTerminalHistory();
  if (n > 0) {
    pushEvent('info', `已作废 ${n} 条历史「未回执的关机/重启/升级」指令（执行即终止进程，补发等于又关一次机）`, { purged: n });
  }
}
loadRepairs();               // 打开设备报修表（repairs.json），重启后从这里恢复

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
  // API 版本管理（《全量设计文档》10.3 落地，2026-10-07）：
  //  REST 接口统一带 apiVersion 字段 + X-API-Version 响应头。客户端可据此判断
  //  "连的是新后端还是旧后端"，旧客户端不会因后端加字段而崩（只加不减的兼容策略）。
  const API_VERSION = '1.0';
  const send = (code, obj) => {
    const body = JSON.stringify({ apiVersion: API_VERSION, ...obj });
    res.writeHead(code, {
      'content-type': 'application/json; charset=utf-8',
      'content-length': Buffer.byteLength(body),
      'x-api-version': API_VERSION,
    });
    res.end(body);
  };

  // /recordings/* 静态托管（2026-10-07 · 班级监控回放）：录制 mp4 只读下载/播放。
  // 与管理面鉴权同位置（在鉴权之前）——管理端播放器直接拉 mp4，不带令牌；
  // 文件本身已在 /api/recordings 列表里由管理端令牌保护（列表不可见=URL 不可知）。
  // ⚠️ 2026-10-08 补鉴权：实测枚举 URL（rec-<ISO时间戳>.mp4 + 可猜 uid）可无令牌
  //    直接下载任意班级监控录像，且 cache-control: public 一年 ——「URL 不可知」不成立。
  //    现在回放下载与列表同鉴权（Bearer CLOUD_VIEWER_TOKEN 或票据），无令牌 401。
  if (u.pathname.startsWith('/recordings/')) {
    const rauth = req.headers.authorization || '';
    const rtoken = rauth.toLowerCase().startsWith('bearer ') ? rauth.slice(7).trim() : '';
    const rStatic = !!rtoken && rtoken === VIEWER_TOKEN;
    const rTicket = !!rtoken && !rStatic ? verifyViewerTicket(rtoken).ok : false;
    if (!rtoken || (!rStatic && !rTicket)) {
      pushEvent('warn', '录制回放下载被拒（未授权）', { path: u.pathname });
      return send(401, { ok: false, ecode: 'AU1003', code: 'TOKEN_INVALID', error: '未授权：需要 Bearer <CLOUD_VIEWER_TOKEN> 或网站账号签发的票据' });
    }
    const rel = u.pathname.replace(/^\/recordings\//, '');
    if (!/^[A-Za-z0-9_-]+\/[A-Za-z0-9_.-]+\.mp4$/.test(rel)) {
      return send(404, { ok: false, error: '路径不合法' });
    }
    const file = join(__dirname, '..', 'recordings', rel);
    if (!existsSync(file)) return send(404, { ok: false, error: '录制不存在' });
    const st = statSync(file);
    const fname = asciiHeaderName(file.split(/[\\/]/).pop());
    res.writeHead(200, {
      'content-type': 'video/mp4',
      'content-length': st.size,
      'cache-control': 'public, max-age=31536000',
      'content-disposition': `inline; filename="${fname}"`,
    });
    return createReadStream(file).pipe(res);
  }

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

  // ── 公开只读：发布版本信息（2026-10-06 加）──────────────────────────────
  // 给**官网下载中心**用：站点服务端来取，渲染"当前版本 / 下载地址 / sha256"。
  // 为什么单独开一个**不带鉴权**的端点：官网是公网站点，不该持有管理端令牌；
  // 而 /api/ota/latest 归管理面鉴权。这里只回"已经公开"的信息（版本号、公开下载 URL、
  // 大小、sha256、说明），**不含任何令牌**，暴露了也没有额外风险。
  // 与 ota.json 同源 → 发版时只改一处，官网自动跟着变，不需要两处各写一套。
  if (req.method === 'GET' && u.pathname === '/api/public/ota') {
    const product = (u.searchParams.get('product') || 'agent').trim();
    // current（可选）= 客户端自报的当前版本。带上它，服务端直接算出 hasUpdate ——
    // 版本比较**只有这一份实现**（见 ota.js 的 cmpVersion 注释：两端各写一遍 C++ 比较
    // 正是 0.6.23 之前"正式版被判成已是最新"那个静默 bug 的来源）。
    // 不带 current 时 hasUpdate 恒 false：官网下载中心只要 version/url/sha256，
    // 它不该因为少了这个参数就以为"有新版"。
    const current = (u.searchParams.get('current') || '').trim();
    const channel = (u.searchParams.get('channel') || 'stable').trim();
    const ota = latestFor(product, channel);
    // 没发布过就如实回 published:false —— 官网据此显示"暂未发布"，不假装有内容。
    if (!ota || !ota.version) {
      return send(200, { ok: true, product, published: false, latest: null, hasUpdate: false });
    }
    return send(200, {
      ok: true,
      product,
      published: true,
      hasUpdate: current ? cmpVersion(current, ota.version) < 0 : false,
      latest: {
        version: ota.version,
        url: ota.url,
        size: ota.size,
        sha256: ota.sha256,
        notes: ota.notes,
        mandatory: ota.mandatory,
      },
    });
  }

  /* ---------- 管理台「用网站账号登录」— 一户通（2026-10-06）----------
   * 这三个端点必须在下面的 isManageApi 之前，否则登录页拿不到发起授权的入口。
   * 它们只做"把浏览器送到站点授权页 / 回显当前是谁"，不碰任何管理操作。 */

  if (req.method === 'GET' && u.pathname === '/api/admin/oauth/begin') {
    // 前端点了「用网站账号登录」才来问；没配全站点就如实说"没有这项"，
    // 让登录页继续保持手敲令牌那一条老路（不是报错，不是静默）。
    return send(200, { ok: true, ...beginAuthorization(adminOAuth && adminOAuth.server, (m) => console.log(m)) });
  }

  if (req.method === 'GET' && u.pathname === '/api/admin/session') {
    const auth = req.headers.authorization || '';
    const tok = auth.toLowerCase().startsWith('bearer ') ? auth.slice(7).trim() : '';
    if (!tok) return send(200, { ok: true, signedIn: false });
    if (tok === VIEWER_TOKEN) {
      return send(200, { ok: true, signedIn: true, mode: 'token', user: '', exp: 0 });
    }
    const v = verifyViewerTicket(tok);
    if (!v.ok) return send(200, { ok: true, signedIn: false, mode: '', reason: v.reason });
    return send(200, { ok: true, signedIn: true, mode: 'ticket', user: v.uid, exp: v.exp });
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
    u.pathname === '/api/ota/latest' || u.pathname.startsWith('/api/repairs') ||
    u.pathname === '/api/recordings' || u.pathname === '/api/qml-patch' ||
    u.pathname === '/api/qml-patch/download' || isAdminApi;
  if (isManageApi) {
    const auth = req.headers.authorization || '';
    const token = auth.toLowerCase().startsWith('bearer ') ? auth.slice(7).trim() : '';
    /* 两条通道都认，2026-10-06 一户通：
     *   ① 手敲的长期令牌 CLOUD_VIEWER_TOKEN —— 老机器/离线机房照旧，不能因为加了账号就断人路；
     *   ② 网站账号签出来的管理台票据 —— 有 uid，被抄走也能在事件流水里看到是谁的。
     * 顺序上是"先比静态令牌，再试票据"：静态令牌长得不像票，验它不会有任何副作用。 */
    const isStatic = !!token && token === VIEWER_TOKEN;
    const isTicket = !!token && !isStatic ? verifyViewerTicket(token).ok : false;
    if (!token || (!isStatic && !isTicket)) {
      pushEvent('warn', 'HTTP 管理面鉴权失败，已拒绝', { path: u.pathname });
      // 错误码体系（2026-10-06）：REST 错误同样带 ecode（AU1003 令牌无效），站点/客户端可按码判断
      return send(401, { ok: false, ecode: 'AU1003', code: 'TOKEN_INVALID', error: '未授权：需要 Bearer <CLOUD_VIEWER_TOKEN> 或网站账号签发的票据' });
    }
    if (isTicket) req.__ticketUid = verifyViewerTicket(token).uid;
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
    const channel = (u.searchParams.get('channel') || 'stable').trim();
    const latest = latestFor(product, channel);
    return send(200, { ok: true, product, channel, latest });
  }

  // ── QML 热更补丁（2026-10-11 · 一源响应式配套）──
  // 手机端定期拉取与桌面一致的 QML 树：清单返回版本号（viewer-qt git HEAD），
  // 客户端比对后下载 zip 解压到本地，加载时本地优先（改 UI 不用重出 APK）。
  if (req.method === 'GET' && u.pathname === '/api/qml-patch') {
    const m = ensureQmlPatch() || qmlPatchManifest();
    if (!m) return send(404, { ok: false, error: 'qml-patch 未就绪（viewer-qt qml/ 目录不可达？）' });
    return send(200, { ok: true, version: m.version, url: m.url, size: m.size, files: m.files, builtAt: m.builtAt });
  }
  if (req.method === 'GET' && u.pathname === '/api/qml-patch/download') {
    const zipPath = qmlPatchZipPath();
    if (!zipPath) return send(404, { ok: false, error: 'qml-patch 未生成' });
    const zip = readFileSync(zipPath);
    res.writeHead(200, {
      'content-type': 'application/octet-stream',
      'content-length': zip.length,
      'cache-control': 'no-store',
      'content-disposition': 'attachment; filename="qml-patch.zip"',
    });
    return res.end(zip);
  }

  // ── 班级监控 / 设备元数据 / 云端存储（2026-10-08）──
  // 这三条都归管理面鉴权（与 /api/ota/latest 同级）：Bearer CLOUD_VIEWER_TOKEN。
  const isViewerApi = u.pathname === '/api/storage' || u.pathname === '/api/classes'
    || u.pathname === '/api/device/bind-class' || u.pathname === '/api/me/classes';
  if (isViewerApi) {
    const vauth = req.headers.authorization || '';
    const vtoken = vauth.toLowerCase().startsWith('bearer ') ? vauth.slice(7).trim() : '';
    if (vtoken !== VIEWER_TOKEN) {
      return send(401, { ok: false, ecode: 'AU1001', code: 'TOKEN_INVALID', error: '未授权：需要管理端令牌' });
    }
  }
  if (req.method === 'GET' && u.pathname === '/api/storage') {
    return send(200, { ok: true, ...getStorageInfo() });
  }
  if (req.method === 'GET' && u.pathname === '/api/classes') {
    return send(200, { ok: true, classes: listClasses() });
  }
  if (req.method === 'GET' && u.pathname === '/api/me/classes') {
    // 管理端（登录账号）可见的班级列表 —— 第一版：云端不分账号，返回全部班级。
    // 在线数现算（不落表，避免"不自刷新"老毛病），管理端按班下发时只发本班在线设备。
    return send(200, { ok: true, classes: listClasses() });
  }
  if (req.method === 'POST' && u.pathname === '/api/device/bind-class') {
    // 管理端把某设备绑到某班（或改绑）。body: { uid, classCode, className? }，也兼容 query。
    // ⚠️ 2026-10-13 修：原来这行写 `JSON.parse(String(data))`，而**该作用域里根本没有 data**
    //    （全文件另一个 data 在 WS 作用域），`String(data)` 抛 ReferenceError 被右侧空 catch
    //    吞掉 ⇒ body 恒为 {}，**JSON body 从来没被读过**，按注释用 body 调用的客户端只会拿到
    //    「缺 uid」而且悄无声息（正是本文件反复强调禁止的 fail-silent）。改为真正读 body。
    let raw = '';
    req.on('data', (c) => { raw += c; if (raw.length > 1e4) req.destroy(); });
    req.on('end', () => {
      let body = {};
      try { body = JSON.parse(raw || '{}'); } catch { /* 非法 JSON → 走 query */ }
      const uid = String(body.uid ?? u.searchParams.get('uid') ?? '').trim();
      const classCode = String(body.classCode ?? u.searchParams.get('classCode') ?? '').trim();
      const className = String(body.className ?? u.searchParams.get('className') ?? '').trim();
      if (!uid) return send(400, { ok: false, error: '缺 uid' });
      if (!isClassCodeValid(classCode)) return send(400, { ok: false, error: '班级码格式不合法（需 6-16 位字母数字或 -_）' });
      const r = bindDeviceToClass(classCode, uid, className);
      return send(r.ok ? 200 : 400, { ok: r.ok, ...r });
    });
    return;
  }

  // ── 班级监控（设计文档 3.6 第一版，2026-10-07）──
  // 录制 = 被控端 ffmpeg DirectShow 本地录制（既有 camera_record_*）→ record_stop 后
  // 上传到云端 recordings/<uid>/<时间戳>.mp4 → 管理端列表 + 直接播放（HTTP 托管，第一版不切 HLS）。
  // 上传鉴权 = 设备令牌（DEV_TOKEN 或 agent ticket）；列表鉴权 = 管理端令牌。
  // 录制根目录。⚠️ 2026-10-09 加 env 覆盖：默认 <cloud-ws>/recordings。
  // 教训（10-08 实测）：隔离实例（起另一个端口测试）没隔离本目录，
  // 测试上传的 mp4 落进了**生产** recordings/（testcam01/prodtest01），
  // 事后手工清理。测试必须设 CLOUD_RECORDINGS_DIR 指到临时目录。
  const RECORDINGS_ROOT = process.env.CLOUD_RECORDINGS_DIR
      ? resolve(process.env.CLOUD_RECORDINGS_DIR)
      : join(__dirname, '..', 'recordings');
  const isRecordingUpload = u.pathname === '/api/recordings/upload';
  if (isRecordingUpload) {
    // 设备令牌验证（与 WS register 同一套：agent ticket 或静态 DEV_TOKEN）
    const auth = req.headers.authorization || '';
    const token = auth.toLowerCase().startsWith('bearer ') ? auth.slice(7).trim() : '';
    let uid = u.searchParams.get('uid') || '';
    let ticketUid = '';
    if (token) {
      const at = verifyAgentTicket(token, String(u.searchParams.get('fp') || ''));
      if (at.ok) {
        // uid 必须等于票里签的 uid（同 WS register）：票验通了就不能再自己报 uid。
        uid = at.uid || uid;
        ticketUid = at.uid || '';
      }
    }
    const okTok = token && (token === DEV_TOKEN || !!ticketUid);
    if (!okTok || !uid) {
      return send(401, { ok: false, ecode: 'AU1003', code: 'TOKEN_INVALID', error: '上传录制需设备令牌' });
    }
    // 接收原始字节（mp4）→ 存 recordings/<uid>/<时间戳>.mp4
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => {
      try {
        const body = Buffer.concat(chunks);
        if (body.length < 1024) {
          pushEvent('error', '录制上传被拒（太小）', { uid, bytes: body.length });
          res.writeHead(400, { 'content-type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: '录制文件太小（<1KB），拒绝' }));
          return;
        }
        mkdirSync(join(RECORDINGS_ROOT, String(uid)), { recursive: true });
        const fname = `rec-${new Date().toISOString().replace(/[:.]/g, '').replace('T', 'T')}.mp4`;
        writeFileSync(join(RECORDINGS_ROOT, String(uid), fname), body);
        pushEvent('info', '录制已上传', { uid, fname, bytes: body.length });
        res.writeHead(200, { 'content-type': 'application/json' });
        res.end(JSON.stringify({ ok: true, uid, fname, bytes: body.length }));
      } catch (e) {
        pushEvent('error', '录制保存失败', { uid, error: e.message });
        res.writeHead(500, { 'content-type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: e.message }));
      }
    });
    return undefined;
  }
  if (req.method === 'GET' && u.pathname === '/api/recordings') {
    // 管理端列表：按 uid 分组返回 recordings/ 下的文件
    const out = {};
    if (existsSync(RECORDINGS_ROOT)) {
      for (const uid of readdirSync(RECORDINGS_ROOT)) {
        const dir = join(RECORDINGS_ROOT, uid);
        if (!statSync(dir).isDirectory()) continue;
        const files = readdirSync(dir)
          .filter((n) => n.endsWith('.mp4'))
          .map((n) => {
            const s = statSync(join(dir, n));
            return { name: n, bytes: s.size, url: `/recordings/${uid}/${n}`, mtimeMs: s.mtimeMs };
          })
          .sort((a, b) => b.mtimeMs - a.mtimeMs);
        if (files.length) out[uid] = files;
      }
    }
    return send(200, { ok: true, recordings: out });
  }

  // 设备报修（2026-10-07，《设备报修-第一版设计》落地）：报修 CRUD。
  //   GET    /api/repairs?status=open             列表（可按状态过滤；按时间倒序）
  //   GET    /api/repairs/{id}                    单条
  //   POST   /api/repairs {uid,symptom,severity}  新建（报修人 = 票据 uid 或 'admin'）
  //   PATCH  /api/repairs/{id} {status,note}      处理中 / 已修复
  //   DELETE /api/repairs/{id}                    关闭 / 删除
  // 报修是资源类操作，不触碰设备控制权限；创建与状态变更都进 events.log（审计）。
  if (req.method === 'GET' && u.pathname === '/api/repairs') {
    const st = u.searchParams.get('status') || '';
    return send(200, { ok: true, repairs: listRepairs(st) });
  }
  if (req.method === 'POST' && u.pathname === '/api/repairs') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 1e4) req.destroy(); });
    req.on('end', () => {
      let p;
      try { p = JSON.parse(body || '{}'); }
      catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
      const r = createRepair({ uid: p.uid, symptom: p.symptom, severity: p.severity, reporter: req.__ticketUid || 'admin' });
      if (r.error) { pushEvent('warn', '报修被拒', { reason: r.error }); return send(r.http || 400, { ok: false, error: r.error }); }
      pushEvent('info', '设备报修已提交', { id: r.ticket.id, uid: r.ticket.uid, severity: r.ticket.severity });
      return send(201, { ok: true, ticket: r.ticket });
    });
    return undefined;
  }
  {
    const m = /^\/api\/repairs\/([^/]+)$/.exec(u.pathname);
    if (m) {
      const rid = decodeURIComponent(m[1]);
      if (req.method === 'GET') {
        const t = listRepairs().find((x) => x.id === rid);
        return t ? send(200, { ok: true, ticket: t }) : send(404, { ok: false, error: `报修单不存在：${rid}` });
      }
      if (req.method === 'PATCH') {
        let body = '';
        req.on('data', (c) => { body += c; if (body.length > 1e4) req.destroy(); });
        req.on('end', () => {
          let p;
          try { p = JSON.parse(body || '{}'); }
          catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
          const r = updateRepair(rid, { status: p.status, note: p.note });
          if (r.error) return send(r.http || 404, { ok: false, error: r.error });
          pushEvent('info', '报修状态更新', { id: r.ticket.id, status: r.ticket.status });
          return send(200, { ok: true, ticket: r.ticket });
        });
        return undefined;
      }
      if (req.method === 'DELETE') {
        const r = deleteRepair(rid);
        if (r.error) return send(r.http || 404, { ok: false, error: r.error });
        pushEvent('info', '报修已删除', { id: rid });
        return send(200, { ok: true });
      }
      return send(405, { ok: false, error: '不支持的方法' });
    }
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
      if (!uid || !action) return send(400, { ok: false, ecode: 'CM1002', code: 'CMD_PARAM_INVALID', error: '缺 uid 或 action' });
      const r = sendInstruction(uid, String(action), params ?? {});
      if (r.state === 'sent') return send(200, { ok: true, state: 'sent', uid, action });
      // 错误码体系（2026-10-06）：设备离线/不存在 → DV3001；其余写失败 → DV3002
      const ecode = r.state === 'offline' ? 'DV3001' : 'DV3002';
      const code = r.state === 'offline' ? 'DEVICE_OFFLINE' : 'HEARTBEAT_TIMEOUT';
      return send(409, { ok: false, state: r.state, uid, action, ecode, code, detail: r.detail });
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
      // ⚠️ 2026-10-13 修：原来明文回吐**设备令牌**。任何持管理端令牌/票据的人都能据此拿到
      //    设备令牌，而设备令牌可以直接调 /api/recordings/upload，或以任意 uid 走静态令牌分支
      //    注册伪设备 —— 等于把"看的人"提权成"被控端身份"。这里只回尾 4 位供比对。
      wsToken: env.CLOUD_WS_TOKEN ? '****' + String(env.CLOUD_WS_TOKEN).slice(-4) : '',
      viewerToken: env.CLOUD_VIEWER_TOKEN ? '****' + String(env.CLOUD_VIEWER_TOKEN).slice(-4) : '',
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
      const { product = 'agent', version, url, sha256, size, notes, mandatory, allowDowngrade } = payload;
      if (!version || !url) return send(400, { ok: false, error: '缺 version 或 url' });
      if (!/^\d+(\.\d+)*$/.test(String(version))) {
        return send(400, { ok: false, error: 'version 形如 0.6.1' });
      }
      if (!/^https?:\/\/.+/i.test(String(url))) {
        return send(400, { ok: false, error: 'url 必须是 http(s) 地址' });
      }
      // 🚨 sha256 必填且必须是 64 位小写十六进制。
      // 以前这里写的是 `sha256: sha256 || ''` —— 忘了在页面上点「计算 SHA256」也能发布成功，
      // 而**被控端对空/非法 sha256 一律拒收**（不做无校验更新），结果是
      // "管理台显示已发布 0.6.2、全校教室机一个都升不了"，且首页下载页也拿不到校验值。
      // 这种"发布了却没人能装"的假成功，比直接报错坏得多。
      const sha = String(sha256 || '').trim().toLowerCase();
      if (!/^[0-9a-f]{64}$/.test(sha)) {
        return send(400, {
          ok: false,
          error: 'sha256 必须是 64 位小写十六进制（页面上点「计算 SHA256」选一次安装包即可）。缺了它被控端会拒绝这次更新，等于没发布。',
        });
      }
      // size 只认「number」或「纯数字字符串」。
      // 直接 Number(size) 会把 true/[]/'' 这种也放过去（Number(true)===1），
      // 结果清单里躺着一个 size:1 —— 看不出错、但谁也别想据此判断下载是否完整。
      let sizeNum = NaN;
      if (typeof size === 'number') sizeNum = size;
      else if (typeof size === 'string' && /^\d{1,15}$/.test(size.trim())) sizeNum = parseInt(size, 10);
      if (!Number.isInteger(sizeNum) || sizeNum <= 0) {
        return send(400, { ok: false, error: 'size 必须是正整数（点「计算 SHA256」时会一并填好）' });
      }

      // 读取现有清单（有则改，无则建）。
      // ⚠️ 这里**故意不再 catch 成空对象**：旧写法遇到带 BOM 的 ota.json（PowerShell 的
      //    Set-Content -Encoding UTF8 会写 BOM）就会 JSON.parse 抛错 → 被 catch 吞掉 →
      //    manifest 重置为 {products:{}} → **把另一个产品的条目整条抹掉**。
      //    文件坏掉应该吵出来，而不是静默清空别人的发布。
      let manifest = { products: {} };
      if (existsSync(OTA_MANIFEST_FILE)) {
        const raw = readFileSync(OTA_MANIFEST_FILE);
        if (raw[0] === 0xef && raw[1] === 0xbb && raw[2] === 0xbf) {
          return send(400, {
            ok: false,
            error: `${OTA_MANIFEST_FILE} 带 UTF-8 BOM，JSON.parse 会失败（云端会当成"没发布过版本"）。请另存为「UTF-8 无 BOM」后重试。`,
          });
        }
        try {
          manifest = JSON.parse(raw.toString('utf8'));
        } catch (e) {
          // 解析不了就拒绝，**不要**用空清单覆盖它
          return send(400, { ok: false, error: `现有 ${OTA_MANIFEST_FILE} 不是合法 JSON（${e.message}），已拒绝写入以免覆盖其它产品条目` });
        }
        if (!manifest || typeof manifest !== 'object') manifest = { products: {} };
      }
      if (!manifest.products || typeof manifest.products !== 'object') manifest.products = {};

      // 版本倒退 = 把全校教室机"升级"到旧版本。与 publish-release.mjs 同一道护栏，
      // 确属回滚才允许（后台传 allowDowngrade:true）。
      const prev = manifest.products[product];
      if (prev && prev.version && cmpVersion(String(version), String(prev.version)) < 0 && allowDowngrade !== true) {
        return send(400, {
          ok: false,
          error: `拒绝发布：${product} 当前最新是 v${prev.version}，这次要发的 v${version} 更低。这会把所有教室机降级；确属回滚请带 allowDowngrade:true。`,
        });
      }

      manifest.products[product] = {
        version, url, sha256: sha, size: sizeNum, notes: notes || '', mandatory: !!mandatory,
      };
      // 与 publish-release.mjs 对齐：同样盖一层生成信息，便于事后分辨是谁写的
      manifest.generatedAt = new Date().toISOString();
      manifest.generatedBy = 'POST /api/admin/ota（云端管理台）';
      try {
        // 显式 Buffer ⇒ 绝不带 BOM（与脚本侧同一条硬要求）
        const text = JSON.stringify(manifest, null, 2) + '\n';
        writeFileSync(OTA_MANIFEST_FILE, Buffer.from(text, 'utf8'));
        JSON.parse(readFileSync(OTA_MANIFEST_FILE, 'utf8')); // 写完回读，坏 JSON 当场炸
        pushEvent('info', '管理面板发布了 OTA', { product, version });
        return send(200, { ok: true, product, version, sha256: sha, size: sizeNum, note: 'OTA 清单已写入，立即生效' });
      } catch (e) {
        return send(500, { ok: false, error: '写入 ota.json 失败: ' + e.message });
      }
    });
    return undefined;
  }

  // GET /api/admin/provision?uid=class_xxx_2028_08  → 一枚这台机器专属的装机 bat
  // 面板点"下载装机包"就走这里；教室机双击 bat 即完成装机（流程见 provision.js 头注释）。
  if (req.method === 'GET' && u.pathname === '/api/admin/provision') {
    const type = (u.searchParams.get('type') || 'agent').trim();
    if (type !== 'agent' && type !== 'viewer') {
      return send(400, { ok: false, error: 'type 只支持 agent（教室机）或 viewer（管理端）' });
    }
    const uid = (u.searchParams.get('uid') || '').trim();
    if (!uid) return send(400, { ok: false, error: '缺 uid' });
    // 教室机的 uid 是设备身份，命名规范要求全小写 ASCII（命名规范 class_<校简称>_<届>_<班号>），
    // 收窄校验；管理端那框是给人看的备注，允许中文，但**只影响 bat 文件名**，不会写进 bat。
    const re = type === 'agent' ? /^[a-z0-9][a-z0-9._-]{0,63}$/ : /^[\w\u4e00-\u9fa5][\w\u4e00-\u9fa5.-]{0,63}$/;
    if (uid.length > 64 || !re.test(uid)) {
      return send(400, {
        ok: false,
        error: type === 'agent'
          ? '教室机 uid 只允许小写字母/数字/下划线/点/横线，最长 64（例 class_xlzx_2028_08）'
          : '备注名只允许中英文/数字/下划线/点/横线，最长 64',
      });
    }
    let bat;
    try {
      bat = buildProvisionBat(type, uid, DEV_TOKEN);
    } catch (e) {
      return send(500, { ok: false, error: e.message });
    }
    // 台账：以前装一台要在纸上记一行 uid/班级/日期，装几十台就对不上；现在面板自己记
    recordProvision(type, uid, type === 'viewer' ? currentViewerPackage() : currentAgentPackage());

    // 中文名字只在前端 blob 下载时用（浏览器认）；HTTP 头必须 ASCII，见上面 /assets 的注释。
    // 管理端备注名允许中文，直插进去照样会触发 ERR_INVALID_CHAR 掀翻进程，所以这里必须先糊一遍。
    const name = `provision-${type === 'viewer' ? 'viewer' : 'agent'}-${asciiHeaderName(uid)}.bat`;
    res.writeHead(200, {
      'content-type': 'application/octet-stream',
      'content-length': Buffer.byteLength(bat),
      'cache-control': 'no-store',
      'content-disposition': `attachment; filename="${name}"`,
    });
    pushEvent('info', '生成装机包', { uid });
    return res.end(bat);
  }

  // GET /api/admin/provision/log?limit=20 → 装机台账（谁/什么类型/哪版包/什么时候下的）
  if (req.method === 'GET' && u.pathname === '/api/admin/provision/log') {
    const n = Math.min(Math.max(Number(u.searchParams.get('limit') || 20), 1), 200);
    return send(200, { ok: true, total: recentProvision(9999).length, log: recentProvision(n) });
  }

  // GET /api/admin/provision/status → 面板要知道"安装包放了没 / 云端对外地址是什么"
  if (req.method === 'GET' && u.pathname === '/api/admin/provision/status') {
    return send(200, {
      ok: true,
      agent: { package: currentAgentPackage(), script: 'provision/install-agent.ps1', wsUrl: agentWsUrl() },
      viewer: { package: currentViewerPackage(), script: 'provision/install-viewer.ps1', wsUrl: viewerWsUrl() },
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

// ⚠️ maxPayload：ws 默认上限是 100MiB，而帧解析发生在**鉴权之前**（viewer 的 authed 判断、
//    agent 的 register 校验都在 message 回调里更靠后）。不设上限 ⇒ 任何未鉴权连接发一条
//    100MB 文本就能让这条"全班共用入口"做一次 toString + JSON.parse（瞬时数百 MB），
//    开几条连接即可打爆云端。这里按两端各自的正常帧量级收紧（2026-10-13）。
const wss = new WebSocketServer({ noServer: true, maxPayload: 4 * 1024 * 1024 });        // 被控端接入（最大是 JPEG 帧）
const viewerWss = new WebSocketServer({ noServer: true, maxPayload: 4 * 1024 * 1024 });  // 管理端（看的人）

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
  // 2026-10-11 修：必须选**在线**设备（listDevices 含离线 known 表，取 [0] 可能订到离线 → rtc-start 白发）。
  const firstOnline = listDevices().find((d) => d.online)?.uid;
  if (firstOnline) {
    if (subscribeViewer(ws, firstOnline)) {
      snd(ws, 'subscribed', { uid: firstOnline, auto: true });
      // 2026-10-11 修复「WebRTC 从未生效」：自动订阅与手动 subscribe 一样
      // 也要通知设备起 RTC 推流（此前只有手动分支 L969 发 rtc-start，自动订阅
      // 只推 WS 帧 → 被控端从不 ldc 推流，RTC 链路永远走不起来）。
      rtcSignalToAgent(firstOnline, 'rtc-start', { uid: firstOnline });
    }
  }
  snd(ws, 'viewer-ready', { viewers: viewerCount() });
  snd(ws, 'devices', { devices: listDevices(), at: new Date().toISOString() });
}

viewerWss.on('connection', (ws) => {
  // 关键：鉴权通过前绝不 addViewer()。否则设备表和画面帧会被推给没通过鉴权的连接，
  // 加了 auth 也等于没加——这是最容易写歪的一处。
  let authed = false;
  let audioSeq = 0; // 语音帧序号（管理端→被控端）
  const deadline = setTimeout(() => {
    if (authed) return;
    pushEvent('warn', '管理端握手超时：连上后没在限定时间内鉴权，已踢掉');
    try { ws.close(4002, 'viewer handshake timeout'); } catch { /* 已断就算了 */ }
  }, HANDSHAKE_TIMEOUT_MS);

  ws.on('message', (data, isBinary) => {
    if (isBinary) {
      // 管理端二进制帧 = 语音 PCM 或屏幕广播 JPEG（2026-10-07）。
      // 按当前活跃会话路由：语音播着就按音频转发，屏幕广播播着就按广播转发；
      // 都没有 → 丢弃并提示（不许静默收下再扔掉）。
      const buf = Buffer.from(data);
      if (authed && isAudioBroadcasting()) {
        broadcastAudioToAgents(buf, 'audio/pcm', ++audioSeq);
        return;
      }
      if (authed && isScreenBroadcasting()) {
        broadcastScreenToAgents(buf, ++audioSeq);
        return;
      }
      pushEvent('warn', '管理端发了二进制消息（无活跃语音/广播会话，已丢弃）', { bytes: data.length });
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
      // 语音对讲（2026-10-07，设计文档 3.3）：老师开始/停止讲话。
      // 开始后云端接受该管理端的二进制音频帧并 fan-out；停止后拒绝新帧。
      // 第一版目标 = 全部在线设备（班级映射在站点侧）。
      // 2026-10-09：params.duplex=true → 语音通话（双向）——记录发起者 ws，
      //   学生上行音频单播回它（sendAudioToTeacher），不 fan-out 全教室。
      case 'audio.start': {
        const duplex = !!p.payload.duplex;
        setAudioBroadcasting(true, duplex ? ws : null);
        snd(ws, 'audio.started', { ok: true, targets: 'all-online', duplex });
        pushEvent('info', duplex ? '管理端开始语音通话（双向，学生可回话）'
                                 : '管理端开始语音对讲（老师讲话，全班听）');
        return;
      }
      case 'audio.stop':
        setAudioBroadcasting(false);
        snd(ws, 'audio.stopped', { ok: true });
        pushEvent('info', '管理端结束语音对讲/通话');
        return;
      // 屏幕广播（2026-10-07，设计文档《屏幕广播-第一版设计》）
      case 'broadcast.start':
        setScreenBroadcasting(true);
        snd(ws, 'broadcast.started', { ok: true, targets: 'all-online' });
        pushEvent('info', '管理端开始屏幕广播（老师屏幕 → 全部在线设备）');
        return;
      case 'broadcast.stop':
        setScreenBroadcasting(false);
        snd(ws, 'broadcast.stopped', { ok: true });
        pushEvent('info', '管理端结束屏幕广播');
        return;
      case 'instruction': {
        const { uid, action, params } = p.payload;
        if (!uid || !action) {
          sndErr(ws, 'BAD_PAYLOAD', 'instruction 缺 uid 或 action');
          return;
        }
        const r = sendInstruction(uid, String(action), params ?? {});
        // ── 远程终端的审计：能跑 shell 就等于能把机器拆了，谁敲了什么必须留得下来 ──
        if (action === 'terminal_open') {
          pushEvent('warn', '管理端请求开远程终端（需被控端本机点头）',
            { uid, sid: params?.sid ?? null, shell: params?.shell ?? null });
        } else if (action === 'terminal_input') {
          // 命令原文进事件流水；事件行有 4096 上限，这里先截到 200 免得把日志撑爆
          const cmd = String(params?.keys ?? '');
          pushEvent('warn', `终端输入：${cmd.slice(0, 200)}`, { uid, sid: params?.sid ?? null, cmdChars: cmd.length });
        } else if (action === 'terminal_close') {
          pushEvent('info', '管理端主动关了远程终端', { uid, sid: params?.sid ?? null });
        }
        // 云端只说它发送成功与否；到没到机器、有没有真做，是设备回执的事
        // 错误码体系（2026-10-06）：离线/不存在 → DV3001，其余写失败 → DV3002
        const isOffline = r.state === 'offline';
        snd(ws, 'instruction-result', {
          ok: r.state === 'sent', state: r.state, uid, action,
          ecode: isOffline ? 'DV3001' : 'DV3002',
          code: isOffline ? 'DEVICE_OFFLINE' : 'HEARTBEAT_TIMEOUT',
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

      // ── 鉴权：设备接入票优先，长期令牌兜底（2026-10-06）──
      // 设备票是站点 /api/device/bind 给登录用户签的（7 段 v1.exp.uid.fp.nonce.owner.sig），
      // 被控端「用星璃账号绑定」走这条；长期令牌（CLOUD_WS_TOKEN）是旧路，保留兼容。
      // 两条路互斥：有 token 就先按票验，验不过再退回去比静态令牌。
      let authedBy = null;   // 'ticket' | 'static' | null
      let ticketUid = '';    // 票里解出的 uid（供审计）
      let ticketOwner = '';  // 票里解出的绑定者（供审计）
      if (token) {
        const at = verifyAgentTicket(token, String(p.payload.fp || ''));
        if (at.ok) {
          authedBy = 'agent-ticket';
          ticketUid = at.uid;
          ticketOwner = at.owner;
        }
      }
      // 不是设备票：退回旧的静态令牌校验（保留原路，不破已有部署）
      if (!authedBy) {
        if (token !== DEV_TOKEN) {
          finish();
          pushEvent('error', '设备令牌校验失败，连接已拒绝', { uid: String(ruid ?? '') });
          sndErr(ws, 'BAD_TOKEN', token ? '票据与静态令牌均校验失败' : '未提供令牌或设备票');
          return;
        }
        authedBy = 'static';
      }

      // uid 的归属由鉴权方式决定：
      //  · 票验通 → uid **必须等于票里签的 uid**。票是"这张票授权这台设备"，
      //    uid 由站点绑定流程签发；客户端改口报别的 uid 就是冒名，直接拒。
      //    （旧写法 `ruid || ticketUid` 让载荷 uid 优先 ⇒ 持 A 机的票可注册成任意 uid。）
      //  · 静态令牌 → 没有票可锚定，uid 只能取载荷。
      if (authedBy === 'agent-ticket') {
        const claimed = String(ruid || '').trim();
        if (!ticketUid) {
          finish();
          pushEvent('error', '设备票里没有 uid，拒绝', { owner: ticketOwner });
          sndErr(ws, 'BAD_TOKEN', '票据未包含设备 uid');
          return;
        }
        if (claimed && claimed !== ticketUid) {
          finish();
          pushEvent('error', 'register 声明的 uid 与设备票不符，已拒绝',
            { claimed, ticketUid: ticketUid, owner: ticketOwner });
          sndErr(ws, 'BAD_PAYLOAD', 'register 的 uid 与设备票不一致');
          return;
        }
        uid = ticketUid;
      } else {
        uid = String(ruid || '').trim();
      }
      if (!uid) {
        finish();
        pushEvent('error', 'register 没带 uid，拒绝');
        sndErr(ws, 'BAD_PAYLOAD', 'register 缺 uid');
        return;
      }
      registered = true;
      finish();
      // 审计：记下这台机器是用哪条路接入的、谁绑的。
      // 账号绑定通道把 owner（绑定者用户名）写进 events.log，兑现站点 /api/device/bind 的
      // 设计承诺「云端注册事件能查到哪台设备是谁绑进来的」——否则这个字段只进控制台，
      // 重启就丢，出问题时没法回溯是哪台设备、被谁绑进来的。
      // 静态令牌路不额外记：markConnected 已记「设备已注册并连上」，这里没有 uid 之外的
      // 新信息，再加一条只是噪音（每台设备上线会多一条重复事件）。
      if (authedBy === 'agent-ticket') {
        console.log(`[cloud] 设备 ${uid} 通过账号绑定接入（owner=${ticketOwner}）`);
        pushEvent('info', '设备通过账号绑定接入',
          { uid, owner: ticketOwner, auth: 'agent-ticket' });
      } else {
        console.log(`[cloud] 设备 ${uid} 通过静态令牌接入`);
      }
      // 能力清单原样转存：被控端 register 里带 caps/actions，云端不解释、不挑选，
      // 由管端按 action 名决定按钮置不置灰（2026-10-06 契合度改造）。
      const regCaps = (p.payload && typeof p.payload.caps === 'object' && p.payload.caps !== null)
        ? p.payload.caps : null;
      // 2026-10-08：register 带 name / classCode → 落到已知设备表（重连沿用、离线保留）。
      // 2026-10-09：再带 className（班级显示名，如「1班」）→ 班级索引不再只显示 class_01。
      const regMeta = {
        name: (p.payload && typeof p.payload.name === 'string' && p.payload.name.trim()) ? p.payload.name.trim().slice(0, 64) : null,
        classCode: (p.payload && typeof p.payload.classCode === 'string' && p.payload.classCode.trim()) ? p.payload.classCode.trim() : null,
        className: (p.payload && typeof p.payload.className === 'string' && p.payload.className.trim()) ? p.payload.className.trim().slice(0, 64) : null,
      };
      markConnected(uid, String(version || ''), ws, regCaps, regMeta);
      // 设备上线：把还没挑设备的管理端补上订阅（否则老师先开软件 → 永远黑屏）
      const auto = autoSubscribeViewers(uid);
      if (auto > 0) pushEvent('info', '已自动给管理端补上订阅', { uid, viewers: auto });
      // 回执：v1 收 registered（顺带**下发心跳参数**，不再靠两端各猜一个数字）；旧客户端仍收 registration-ok
      // 2026-10-06：把 OTA 清单（ota.json）里的最新版本随注册回执一起下发给被控端 ——
      // 被控端据此在托盘/「设置与信息」里提示升级，并复用 self_update 链路一键升。
      // 2026-10-10：版本按更新通道分发 —— 设备 register 带 channel（stable/gray），
      //   据此读 ota-<channel>.json（灰度清单）。灰度设备拿到 rc.4，stable 设备仍拿 stable 的 rc.3，
      //   实现"灰度发布、不惊动全网"。
      // 没发布过版本时 latestFor 返回 null → 这些字段是 null，被控端按"未知"显示
      // （**不是**"已是最新"：这两件事运维必须分得清，否则会以为升级链路是好的）。
      const agentChannel = (p.payload && typeof p.payload.channel === 'string' && p.payload.channel.trim())
        ? p.payload.channel.trim() : 'stable';
      const agentOta = latestFor('agent', agentChannel);
      snd(ws, ws.__v1 === true ? 'registered' : 'registration-ok', {
        uid,
        server: 'stelarith-cloud-ws/0.4.0',
        heartbeatMs: HEARTBEAT_INTERVAL_MS,
        timeoutMs: HEARTBEAT_TIMEOUT_MS,
        latestVersion: agentOta ? agentOta.version : null,
        updateUrl: agentOta ? agentOta.url : null,
        updateSha256: agentOta ? agentOta.sha256 : null,
        updateNotes: agentOta ? agentOta.notes : null,
        updateMandatory: agentOta ? agentOta.mandatory : null,
      });
      // 重连补发：这台机器掉线期间"已下发未回执"的指令，按原顺序重发给它（内部无待补发则不动作）。
      replayPending(ws, uid);
      return;
    }

    if (isBinary) {
      const f = parseFrame(Buffer.from(data));
      if (!f.ok) { sndErr(ws, f.code, f.detail); return; }
      const seq = f.header?.seq ?? 0;
      // ── 语音通话上行（2026-10-09）：学生 mic 音频帧 → 单播回发起老师 ──
      // 帧头 mime=audio/pcm 时走 duplex 通道，不走画面帧的 broadcastFrame。
      if (f.header?.mime === 'audio/pcm') {
        const r = sendAudioToTeacher(f.payload, seq, uid);
        if (r.hits === 0) {
          pushEvent('warn', '学生上行音频未送达（非 duplex 会话）', { uid, bytes: f.payload.length });
        }
        return;
      }
      const info = addFrame(uid, f.payload.length);
      if (!info) {
        pushEvent('error', '收到帧但设备不在表里（异常状态）', { uid });
        return;
      }
      // 2026-10-11 用户裁决「不要云端缓存」：不存 recentFrame（非监控），
      // 帧直接实时转发给订阅的管理端（broadcastFrame），画面实时无陈旧。
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

    if (p.type === 'terminal_opened' || p.type === 'terminal_data'
      || p.type === 'terminal_exit' || p.type === 'terminal_closed') {
      // 被控端远程终端的流式帧：只投给订阅了这台的管理端，不群发（同抓屏那条规矩）。
      // terminal_closed/exit 单独记一条 —— 会话什么时候开、什么时候没，是审计要能查到的。
      const hits = relayTerminalFrame(uid, p.type, p.payload);
      if (p.type === 'terminal_closed' || p.type === 'terminal_exit') {
        pushEvent(p.type === 'terminal_closed' ? 'info' : 'warn',
          `远程终端会话结束：${p.type}`, { uid, sid: p.payload?.sid ?? null, reason: p.payload?.reason ?? null });
      }
      if (hits > 0 && p.type !== 'terminal_data') {
        pushEvent('info', `终端帧 ${p.type} 已推给管理端`, { uid, viewers: hits });
      } else if (hits === 0) {
        pushEvent('warn', `终端帧 ${p.type} 推了但没人订阅（管理端没连或没选这台）`, { uid });
      }
      return;
    }

    if (p.type === 'heartbeat') {
      // 存活信号：与帧解耦（按需拉流后没人看的机器不推帧，帧不能当存活依据）
      const ok = touchHeartbeat(uid);
      if (!ok) pushEvent('error', '收到心跳但设备不在在线表里（异常状态）', { uid });
      return;
    }

    if (p.type === 'noise_sample' || p.type === 'noise_alert') {
      // 噪音检测 / 自习模式（2026-10-13）：只投给订阅了这台的管理端（同抓屏/终端的规矩）。
      // ⚠️ **不走 pushEvent 事件流水** —— 噪音样本 2s 一条，塞进"最近 50 条"的流水会把它
      //    刷爆、把真正的运维事件挤掉。所以走订阅中继、实时直达在看这台的管理端。
      const hits = relayToSubscribers(uid, p.type, { uid, ...(p.payload || {}) });
      if (p.type === 'noise_alert') {
        pushEvent('info', `噪音告警（${p.payload?.event || '?'}）已推给管理端`,
          { uid, viewers: hits, count: p.payload?.count ?? null });
      }
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
  // 2026-10-11：启动时预打包 QML 补丁（手机热更用；失败不阻塞启动）
  try { const m = ensureQmlPatch(); if (m) console.log(`[cloud] qml-patch 就绪 v${m.version}（${m.files} 文件）`); }
  catch (e) { console.error(`[cloud] WARN qml-patch 预打包失败: ${e.message}`); }
  // 回跳服务要等 listen 完成才知道真端口（listen(0) 是让系统分配），所以在回调里建。
  const srv = createAdminOAuthServer((m) => console.log(m));
  if (srv) adminOAuth = { server: srv };
});

server.on('error', (e) => {
  console.error(`[cloud] FAIL 端口占用或启动失败: ${e.message}`);
  process.exit(1);
});
