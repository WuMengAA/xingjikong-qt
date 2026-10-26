// 星集控云端（被控端最小可用后端）
//   ws://host:8788/ws/agent   被控端接入（先 register，token 校验，之后推帧）
//   ws://host:8788/ws/viewer  管理端接入（D4：看画面 + 刷设备表 + 真下发指令）
//   GET  /api/devices         设备在线表
//   POST /api/instructions    下发指令（设备不在线返回 409，绝不谎报 sent）
//   GET  /api/events          事件流水（最近 50 条）
//   GET  /api/frame?uid=...   最近一帧（管理端 D4 用；内存里，不落盘）
//   GET  /api/instructions/notice?notice_id=...  按通知查回执（站点广播对账，2026-10-04）
//   GET  /api/ota/latest?product=agent           该产品最新版本（OTA 清单，2026-10-05）
//   GET  /api/interact/song-requests?state=...   点歌待审队列（管理端面板用，2026-10-15 组件6）
//   GET  /api/interact/blessings?state=...       祝福待审队列（同上）
//   GET  /api/interact/feedback/summary          点赞/反馈汇总（同上）
//   POST /api/interact/handle                    审批动作 {kind,id,action}（祝福 approve 会向该班广播弹幕）
//   指令队列：已下发未回执的落盘（见 store.js）——设备重连按原顺序补发、云端重启不丢
//   互动数据：点歌/反馈/祝福三表落盘（见 interact.js，2026-10-15 组件6）

import http from 'node:http';
import https from 'node:https';   // 接入码兑换：转发到站点（站点在 www.*，走 https）
import { readFileSync, writeFileSync, existsSync, createReadStream, statSync, mkdirSync, readdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';   // resolve：录制根目录支持 CLOUD_RECORDINGS_DIR 覆盖
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import { WebSocketServer } from 'ws';
import { currentAgentPackage, currentViewerPackage, buildProvisionBat, recordProvision,
  recentProvision, resolveAsset, agentWsUrl, viewerWsUrl } from './provision.js';
import { ensureQmlPatch, qmlPatchManifest, qmlPatchZipPath } from './qml-patch.js';
// 设备绑定（2026-10-20）：二维码矩阵生成 + 一次性绑定票据（零依赖，自带单测）
import { makeQrMatrix, issueDeviceBind, issueClassBind, getTicket, markBound, pollDeviceBind } from './bind-qr.js';
import { hitRate, clientIp } from './ratelimit.js';
import { PORT, HOST, DEV_TOKEN, DEV_TOKEN_IS_DEFAULT, HANDSHAKE_TIMEOUT_MS, SWEEP_INTERVAL_MS,
  VIEWER_TOKEN, VIEWER_TOKEN_IS_DEFAULT, VIEWER_SECRET,
  HEARTBEAT_TIMEOUT_MS, HEARTBEAT_INTERVAL_MS, PROTOCOL_MODE, QUEUE_SWEEP_INTERVAL_MS,
  INTERACT_SWEEP_INTERVAL_MS } from './config.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ENV_FILE = join(__dirname, '..', '.env');
const ADMIN_HTML = join(__dirname, 'admin.html');
const BIND_HTML = join(__dirname, 'bind.html');   // 手机扫码落地页（/bind?c=CODE）


/** 管理台「用网站账号登录」的回跳 server（127.0.0.1 临时端口）。没配全站点就一直是 null。 */
let adminOAuth = null;

/** 2026-10-18 · VoiceHub「正在播放」桥实例（只读；启动失败为 null）。 */
let voiceHubBridge = null;
/** 上一次下发的状态签名：只在"有/没有内容"真正变化时才推，避免每 30s 空推刷设备。 */
let voiceHubLastSig = '';

/** 2026-10-18 · 班级点歌凭据（加密库；未配 STE_SONGKEY_SECRET 时为功能禁用态）。 */
let songKeyStore = null;
initSongKeyStore((m) => console.log(m));
songKeyStore = createSongKeyStore({
  read: readSongKeys,
  write: writeSongKeys,
  log: (m) => console.log(m),
});

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
import { createTunnel } from './tunnel.js';   // 2026-10-18：远控隧道（RFB over WS，替代 WebRTC 主路）
import { createUdpRelay } from './udp-relay.js';   // 2026-10-20：UDP 闪传帧中继（U1）
import { UDP_RELAY_HOST, UDP_RELAY_PORT, UDP_RELAY_TOKEN,
  UDP_MAX_FPS_PER_SESSION, UDP_SESSION_IDLE_MS } from './config.js';
import { createTcpRelay } from './tcp-relay.js';   // 2026-10-20：TCP 闪传帧中继（公网主路，用户定「那就 tcp」）
import { TCP_RELAY_HOST, TCP_RELAY_PORT, TCP_RELAY_TOKEN,
  TCP_SESSION_IDLE_MS, TCP_HELLO_GRACE_MS } from './config.js';
import { createAdminOAuthServer, currentRedirectUri, beginAuthorization } from './adminoauth.js';
import { initStore, sweepQueue, storeReady, receiptsByNotice, actionRanking } from './store.js';
import { initInteract, interactReady, sweepInteract,
  submitSongRequest, submitFeedback, submitBlessing,
  listSongRequests, listBlessings, feedbackSummary,
  handleSongRequest, handleBlessing } from './interact.js';
import { latestFor, cmpVersion, manifestFileFor } from './ota.js';
// 2026-10-18 · VoiceHub「正在播放」桥：云端集中轮询一次、缓存在内存、经既有 WS 下发。
// 为什么在云端而不是被控端：60 台教室机各连 VoiceHub = 60 条外网长连接，且教室断网就全瞎。
// 只读（绝不下发写指令），数据源是公开接口 /api/music/broadcast。
import { createVoiceHubBridge } from './voicehub-bridge.js';
// 2026-10-18 · 班级点歌凭据（用户拍板：每台被控端绑定到**班级**，VoiceHub Key 按班级各持一个）。
// 云端集中托管 + AES-256-GCM 加密落库；**Key 明文绝不下发被控端**，
// 点歌由云端代发（端上只能"请求"，不能"直接点"）。
import { createSongKeyStore } from './class-songkey.js';
import { initSongKeyStore, readSongKeys, writeSongKeys } from './class-songkey-db.js';
// 2026-10-18 · 点歌代发：端上只能"请求"，写入由云端用该班 Key 发往 VoiceHub。
import { requestSongViaVoiceHub } from './song-request-proxy.js';
// 2026-10-23 · 广播社成员（云端自管账号）+ VoiceHub 排期写代理
import {
  initBroadcastClub, createClubMember, disableClubMember, listClubMembers,
  verifyClubMember, mintClubToken, verifyClubToken, voicehubConfigured,
  proxyGetSchedules, proxyReorder, auditReorder, readAudit,
} from './broadcast-club.js';
import { parseIncoming, parseFrame, parseAnyFrame, wrapOutgoing, errorPayload, ERR, PROTOCOL_VERSION } from './protocol.js';
import { timingSafeEqual } from 'node:crypto';

/** 常量时间比较（防时序侧信道）。
 *  ⚠️ 2026-10-13 三端审计修复：静态令牌（VIEWER/DEV）此前一律用 `===` —— V8 的字符串比较
 *  在首个不同字节处提前返回 ⇒ 攻击者可按响应耗时逐字节推断令牌（burst 爆破的经典前置）。
 *  票据那条路早就是 timingSafeEqual，这里把静态令牌这条也收口到同一实现。 */
function safeEqual(a, b) {
  const A = Buffer.from(String(a ?? ''), 'utf8');
  const B = Buffer.from(String(b ?? ''), 'utf8');
  if (A.length !== B.length) return false;
  try { return timingSafeEqual(A, B); } catch { return false; }
}

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
  listDevices, getDevice, getDeviceClassCode, sendInstruction, listEvents, listPending, ensureEventsFile,
  relayTerminalFrame, relayToSubscribers,
  addViewer, removeViewer, subscribeViewer, unsubscribeViewer, pinViewer, autoSubscribeViewers,
  broadcastFrame, broadcastDevices, broadcastToViewers, recordResult, getResult, viewerCount,
  touchHeartbeat, sweepStale,
  rtcSignalToAgent, rtcRelayToViewers, rtcRelayToAgent,
  sendToAgentMsg,   // 2026-10-18：远控隧道的 tunnel.open 走这条
  vncDataToAgent, vncDataToViewer,   // 2026-10-18：远控统一 A —— vnc-data 走主 WS 双向中继
  broadcastAudioToAgents, setAudioBroadcasting, isAudioBroadcasting,
  duplexActive, sendAudioToTeacher,
  broadcastScreenToAgents, setScreenBroadcasting, isScreenBroadcasting,
  // 2026-10-08 分班制 + 已知设备表 + 云端存储信息
  setDeviceMeta, getDeviceRole,
  bindDeviceToClass, unbindDeviceFromClass, listClasses, getDevicesOfClass,
  notifyClassBoundIfOnline,
  getStorageInfo, isClassCodeValid, hydrateClassesFromDir,
  recomputeDeviceEnv,
  getVncAccess, setVncAccess, verifyVncPassword,
} from './registry.js';
// 班级身份唯一真源（2026-10-20）：码/名/别名的解析与落盘。所有绑班入口只认它。
import { initClassDir, resolveClassCode, registerExternalCode, listClassDir, classDirSize } from './class-dir.js';
// 班级房间（2026-10-24 持久房间）：消息历史 + 成员表，落 class-room.db。
// 「每班一个永不停息的子服务器」的落地形态，物理上是一个持久房间而非一个进程。
import { initClassRoom } from './class-room.js';
import { initClassData } from './class-data.js';   // 2026-10-25 学生名单 + 班级课表（借 class-room 的连接）
// 环境白名单（2026-10-24）：test 机由云端清单裁决。设备自报 env 一律忽略。
import { initEnvWhitelist, setTestUid, listTestUids } from './env-gate.js';
// 重认证门（2026-10-24）：管理端定期要求验证密码。云端不存密码，只记
// 「上次通过验证的时刻」；过期后敏感操作拒绝但不掐连接（reauth.js 文件头）。
import { noteVerified, isFresh } from './reauth.js';
import { REAUTH_INTERVAL_MS } from './config.js';
// 设备报修（2026-10-07，《设备报修-第一版设计》落地）：内存表 + repairs.json 落盘
import { loadRepairs, useRepairFile, listRepairs, createRepair, updateRepair, deleteRepair } from './repairs.js';
// 权限消费层（2026-10-15 · 契约《多校多身份-scope契约草案》§4.3：云端是唯一权限入口）
import { identityFromAuth, canAccess, filterDevices, maskDevices } from './perms.js';
// 班级域事件通道（2026-10-23 · #458 班级工作台地基）：按班订阅/推送/历史，
// 与 devices 广播、pushEvent 流水、按设备订阅中继三条通道正交（见 class-events.js 头注）
import {
  handleViewerClassMsg, handleAgentClassMsg,
  noteMemberOnline, noteMemberOffline, removeViewerClasses,
} from './class-events.js';
// 班级管理（2026-10-24）：建/改名/删/移出的业务与路由处理。index.js 只挂分发点
import { handleClassAdminApi, adminMyClasses } from './class-admin.js';
// 用户 claims 存储（2026-10-20 · RBAC 第二批）：票无 claims 时按 uid 回填身份，权限真正流动
import { initUserClaims, applyStoredClaims, setUserClaims, deleteUserClaims, listUserClaims } from './user-claims.js';
// CIMS 课表/时间表供给（2026-10-17）：云端拉 CIMS → 经被控端 dispatch 给 ClassIsland 插件
import { onDeviceOnline as cimsOnDeviceOnline, startCimsScheduler, syncDevice as cimsSyncDevice,
  syncAll as cimsSyncAll, cimsStatus } from './cims.js';
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
// 2026-10-26（审计面三 · 测试隔离）：repairs 落盘路径支持 CLOUD_REPAIRS_FILE 覆盖。
// 此前 repairs.js 固定写工程根 repairs.json —— 隔离实例测试会直接把假报修单写进
// 生产同一文件（真实污染事故，见当日归档）。默认值保持与原行为字节一致。
useRepairFile(process.env.CLOUD_REPAIRS_FILE || join(__dirname, '..', 'repairs.json'));
loadRepairs();               // 打开设备报修表（repairs.json），重启后从这里恢复
// 班级目录（2026-10-20 班级真源统一）：班级码/名/别名落盘，重启后班级身份与显示名不丢；
// 同时把落盘的班灌进 registry 的运行时索引，避免"重启后管理台班级列表空掉"。
{
  const r = initClassDir();
  const hydrated = hydrateClassesFromDir();
  console.log(`[cloud] 班级目录：${r.codes} 个班已就绪（class-dir.db），运行时索引灌入 ${hydrated} 条`);
}
// 班级房间（2026-10-24 持久房间）：消息历史 + 成员表落盘。
// 在这之前两者一个是内存环形缓冲、一个根本不存在 ⇒ 重启即清空 / 加不进人。
{
  const r = initClassRoom();
  console.log(`[cloud] 班级房间：消息历史与成员表已就绪（class-room.db），现有成员 ${r.members} 人`);
}
// 班级数据（2026-10-25）：学生名单 + 班级课表落同一个库。用户裁决「云端数据库存储，
// 不要本机了」—— 课表原来每台设备各存一份本地档，学生名单则根本不存在。
{
  const r = initClassData();
  console.log(`[cloud] 班级数据：学生名单与课表表已就绪（同 class-room.db），现有学生 ${r.students} 人 / 课表 ${r.timetables} 份`);
}
// 环境白名单（2026-10-24）：test 设备清单从文件恢复。空白名单 = 全员生产，安全侧收缩。
{
  const r = initEnvWhitelist();
  if (!r.ok) pushEvent('error', '环境白名单不可用：本机全部设备按生产处理', { error: r.error });
  console.log(`[cloud] 环境白名单：${r.testUids.length} 台测试机（不在清单 = 生产设备）`);
}
initInteract();              // 打开互动库（点歌/反馈/祝福三张表，2026-10-15 组件6）
if (!interactReady()) pushEvent('error', '互动数据不可持久化：点歌/点赞/祝福提交将如实回 STORE_UNAVAILABLE');
// 用户 claims 存储（2026-10-20 · RBAC 第二批）：站点签的票是 bare-uid，鉴权时按
// uid 从本存储回填 roles/scopes，权限才真正流动；存储为空时行为=今天（全权，零回归）。
{
  const r = initUserClaims();
  console.log(`[cloud] user-claims：已加载 ${r.loaded} 条（${r.file}）`);
}

// 启动先清一次：把上轮残留的过期未回执/已回执行处理掉，再开始收新指令
{
  const r = sweepQueue();
  if (r.expiredByTtl || r.expiredByCap || r.purged) {
    pushEvent('info', '启动队列清扫完成', r);
  }
}
// 互动数据启动清扫（只扫已处理行，pending 待审内容永不扫）
{
  const r = sweepInteract();
  if (r.songPurged || r.danmakuPurged || r.feedbackPurged) {
    pushEvent('info', '启动互动数据清扫完成', r);
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

  // 录制根目录（⚠️ 2026-10-16 从下方的录音上传分支**上移**至此）。
  // 原因：静态托管（下方数行）/ 录音上传 / 录音列表 三处必须共用同一目录真源；
  // 而 `const` 存在 TDZ，若定义在下面，静态托管处引用它会抛 ReferenceError。
  // ⚠️ 2026-10-09 加 env 覆盖：默认 <cloud-ws>/recordings。
  // 教训（10-08 实测）：隔离实例（起另一个端口测试）没隔离本目录，
  // 测试上传的 mp4 落进了**生产** recordings/（testcam01/prodtest01），
  // 事后手工清理。测试必须设 CLOUD_RECORDINGS_DIR 指到临时目录。
  const RECORDINGS_ROOT = process.env.CLOUD_RECORDINGS_DIR
      ? resolve(process.env.CLOUD_RECORDINGS_DIR)
      : join(__dirname, '..', 'recordings');

  // /recordings/* 静态托管（2026-10-07 · 班级监控回放）：录制 mp4 只读下载/播放。
  // 与管理面鉴权同位置（在鉴权之前）——管理端播放器直接拉 mp4，不带令牌；
  // 文件本身已在 /api/recordings 列表里由管理端令牌保护（列表不可见=URL 不可知）。
  // ⚠️ 2026-10-08 补鉴权：实测枚举 URL（rec-<ISO时间戳>.mp4 + 可猜 uid）可无令牌
  //    直接下载任意班级监控录像，且 cache-control: public 一年 ——「URL 不可知」不成立。
  //    现在回放下载与列表同鉴权（Bearer CLOUD_VIEWER_TOKEN 或票据），无令牌 401。
  if (u.pathname.startsWith('/recordings/')) {
    const rauth = req.headers.authorization || '';
    const rtoken = rauth.toLowerCase().startsWith('bearer ') ? rauth.slice(7).trim() : '';
    const rStatic = !!rtoken && safeEqual(rtoken, VIEWER_TOKEN);
    const rTicket = !!rtoken && !rStatic ? verifyViewerTicket(rtoken).ok : false;
    if (!rtoken || (!rStatic && !rTicket)) {
      pushEvent('warn', '录制回放下载被拒（未授权）', { path: u.pathname });
      return send(401, { ok: false, ecode: 'AU1003', code: 'TOKEN_INVALID', error: '未授权：需要 Bearer <CLOUD_VIEWER_TOKEN> 或网站账号签发的票据' });
    }
    // 2026-10-26 补「按班」这一层（此前只有"有没有令牌"这一层）：
    //   录像 = 教室画面 + 声音，是本项目最敏感的一类数据。此前**任何**持令牌者
    //   （含受限老师票）都能下载全校任意班的录像 —— 与 /api/frame（实时画面）、
    //   /api/instructions（下发指令）早就有的 canAccess 口径不一致。
    //   这里与设备表同口径：受限身份只拿自己班的；设备无班级归属 ⇒ 谁都不该拿。
    req.__auth = rStatic ? { ok: true, mode: 'token', uid: null } : verifyViewerTicket(rtoken);
    applyStoredClaims(req.__auth);
    const rel = u.pathname.replace(/^\/recordings\//, '');
    if (!/^[A-Za-z0-9_-]+\/[A-Za-z0-9_.-]+\.mp4$/.test(rel)) {
      return send(404, { ok: false, error: '路径不合法' });
    }
    const recUid = rel.split('/')[0];
    if (!canAccess(identityFromAuth(req.__auth), getDeviceClassCode(recUid))) {
      pushEvent('warn', '录制回放下载被拒（越权：目标不在其 scopes 内）',
        { path: u.pathname, uid: recUid });
      return send(403, { ok: false, ecode: 'PV4002', code: 'NO_DEVICE_PERMISSION', error: '无权查看该设备的录制' });
    }
    // ⚠️ 2026-10-16 修复：原为硬编码 join(__dirname, '..', 'recordings', rel)，
    //    与 RECORDINGS_ROOT（env CLOUD_RECORDINGS_DIR 可覆盖）不是同一目录 ⇒
    //    设了该 env 后，录像「能上传、能在列表里看到、却点开 404」。
    const file = join(RECORDINGS_ROOT, rel);
    if (!existsSync(file)) return send(404, { ok: false, error: '录制不存在' });
    const st = statSync(file);
    const fname = asciiHeaderName(file.split(/[\\/]/).pop());
    const recHead = {
      'content-type': 'video/mp4',
      'cache-control': 'private, max-age=3600',
      'content-disposition': `inline; filename="${fname}"`,
    };
    // ── HTTP Range / 206（2026-10-26 · P4 回放小窗）────────────────────────
    // 为什么必须有：此前这里恒返回 200 全量、且**不带 Accept-Ranges**。
    //   代价不是"慢一点"，而是播放器根本没法用：
    //   ① 进度条拖不动 —— seek 在网络层无处落脚，MediaPlayer 只能从头再下一次；
    //   ② 首帧要等整个文件下完（几十 MB 的录制 = 几十秒白屏）；
    //   ③ 只想看最后 30 秒，也得把前面几分钟全拉下来。
    // 加 206 后：播放器按需取字节段，seek 退化成一次带 Range 的小请求。
    // ⚠️ 安全：Range 是**外部可控输入**，必须严格正则解析 + 双向钳制；
    //    畸形/越界一律降级为 200 全量（宁可多传，不可崩或读越界）。
    const rangeHdr = (req.headers.range || '').trim();
    let start = 0;
    let end = st.size - 1;
    let partial = false;
    const rm = /^bytes=(\d*)-(\d*)$/.exec(rangeHdr);
    if (rm) {
      if (rm[1] === '' && rm[2] !== '') {
        // bytes=-N：最后 N 字节（播放器探尾部 moov 时常用）
        const n = Number(rm[2]);
        if (Number.isFinite(n) && n > 0) { start = Math.max(0, st.size - n); end = st.size - 1; partial = true; }
      } else {
        const s = Number(rm[1]);
        const hasEnd = rm[2] !== '';
        const e = hasEnd ? Number(rm[2]) : st.size - 1;
        // ⚠️ 这里刻意**不**要求 s <= e：
        //   越界起点（s >= size）必须先置 partial，才能走到下面的 416 分支。
        //   若在此挡掉，越界 Range 会降级成 200 全量 —— 播放器一旦算错偏移，
        //   本该报错却被塞回整个文件（几十 MB 白传），与"省流量"正好相反。
        //   只有**显式**反向区间（bytes=900-100，end 真的比 start 小）才是语义无效，
        //   按 RFC 7233 忽略 Range 降级 200 —— 那时 start 一定 < size，不会误伤 416。
        if (Number.isFinite(s) && Number.isFinite(e) && s >= 0) {
          if (hasEnd && e < s) {
            partial = false;                       // 显式反向区间 → 无效，降级 200
          } else {
            start = s; end = Math.min(e, st.size - 1); partial = true;
          }
        }
      }
    }
    // 起点越过文件尾 ⇒ 416（RFC 7233），必须带 Content-Range: bytes */size
    if (partial && start >= st.size) {
      res.writeHead(416, { ...recHead, 'content-range': `bytes */${st.size}`, 'accept-ranges': 'bytes' });
      return res.end();
    }
    if (partial) {
      res.writeHead(206, {
        ...recHead,
        'content-length': end - start + 1,
        'content-range': `bytes ${start}-${end}/${st.size}`,
        'accept-ranges': 'bytes',
      });
      return createReadStream(file, { start, end }).pipe(res);
    }
    res.writeHead(200, { ...recHead, 'content-length': st.size, 'accept-ranges': 'bytes' });
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

  // ── 公开只读：插件商店清单（P2 · 2026-10-14）─────────────────────────────
  // 被控端「插件市场」窗口来取，渲染可安装的第三方能力（id/名称/版本/作者/说明/下载包）。
  // 公开、不带鉴权——清单本身不含任何令牌；具体插件包下载由各自的 downloadUrl 决定（可走 CDN）。
  if (req.method === 'GET' && u.pathname === '/api/plugins') {
    const pluginsFile = join(__dirname, 'plugins.json');
    let list = [];
    if (existsSync(pluginsFile)) {
      try { list = JSON.parse(readFileSync(pluginsFile, 'utf8')); }
      catch (e) { list = []; }
    }
    const body = JSON.stringify({ apiVersion: API_VERSION, plugins: list });
    res.writeHead(200, {
      'content-type': 'application/json; charset=utf-8',
      'content-length': Buffer.byteLength(body),
      'x-api-version': API_VERSION,
      'cache-control': 'public, max-age=30',
    });
    res.end(body);
    return;
  }

  // ── 公开只读：发布版本信息（2026-10-06 加）──────────────────────────────
  // 给**官网下载中心**用：站点服务端来取，渲染"当前版本 / 下载地址 / sha256"。
  // 为什么单独开一个**不带鉴权**的端点：官网是公网站点，不该持有管理端令牌；
  // 而 /api/ota/latest 归管理面鉴权。这里只回"已经公开"的信息（版本号、公开下载 URL、
  // 大小、sha256、说明），**不含任何令牌**，暴露了也没有额外风险。
  // 与 ota.json 同源 → 发版时只改一处，官网自动跟着变，不需要两处各写一套。
  // ── 2026-10-18 · VoiceHub 广播状态（公开只读诊断）──────────────────────
  // 为什么公开：内容就是"谁在播什么"，教室大屏本来就公开显示，不含任何令牌或个人信息
  // （publisherName 是显示名，契约明确"不要写进任何对外日志"，这里只透传不落日志）。
  // 用途：① 运维确认云端到底有没有拉到状态；② 被控端首屏对齐（比等下一次 WS 推送快）。
  if (req.method === 'GET' && u.pathname === '/api/public/voicehub') {
    const d = voiceHubBridge ? voiceHubBridge.diagnostics() : null;
    const body = JSON.stringify({
      ok: !!voiceHubBridge,
      // 未启用（启动失败/未配置）时如实回 null，不假装"没有广播"
      ...(d ? { pollMs: d.pollMs, staleMs: d.staleMs, lastOkAgoMs: d.lastOkAgoMs, lastError: d.lastError } : {}),
      state: d ? d.state : { status: 'disabled', broadcast: null },
    });
    res.writeHead(200, {
      'content-type': 'application/json; charset=utf-8',
      'content-length': Buffer.byteLength(body),
      // 状态本身 30s 才更新一次，缓存 5s 足够，且避免被 CDN 长期囤。
      'cache-control': 'public, max-age=5',
    });
    return res.end(body);
  }

  // ── 2026-10-23 · 广播社成员（管理端独立身份）────────────────────────────
  // 账号自管在云端；成员令牌 = viewer 票（claims 带 broadcast_club 角色 + station）。
  // 写排期经云端代理到 VoiceHub（密钥云端持有），每次写落审计。
  if (u.pathname.startsWith('/api/broadcast-club/')) {
    const sub = u.pathname.slice('/api/broadcast-club'.length).replace(/^\/+/, '').replace(/\/+$/, '');
    // 登录：无需鉴权
    if (req.method === 'POST' && sub === 'login') {
      let body = '';
      req.on('data', (c) => { body += c; if (body.length > 1e4) req.destroy(); });
      req.on('end', () => {
        let p;
        try { p = JSON.parse(body || '{}'); }
        catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
        const r = verifyClubMember(p.username, p.password);
        if (!r.ok) return send(401, { ok: false, code: 'AUTH_FAILED', error: '用户名或密码错误' });
        const t = mintClubToken(r.username, r.station);
        if (!t.ok) return send(500, { ok: false, error: t.reason || '签发票据失败（未配置 CLOUD_VIEWER_SECRET）' });
        pushEvent('info', '广播社成员登录', { username: r.username, station: r.station });
        return send(200, { ok: true, token: t.token, station: r.station, expiresInMs: t.expiresInMs });
      });
      return undefined;
    }
    // 其余子路由均需成员令牌
    if (sub === 'schedules' || sub === 'schedules/reorder') {
      const auth = req.headers.authorization || '';
      const tok = auth.toLowerCase().startsWith('bearer ') ? auth.slice(7).trim() : '';
      const v = verifyClubToken(tok);
      if (!v.ok) return send(401, { ok: false, code: 'TOKEN_INVALID', error: v.reason || '未授权：需要广播社成员令牌' });
      if (req.method === 'GET' && sub === 'schedules') {
        proxyGetSchedules().then((r) => {
          if (!r.ok) return send(r.code && r.code >= 400 && r.code < 500 ? r.code : 502, { ok: false, error: r.error });
          res.writeHead(200, { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' });
          return res.end(r.data);
        });
        return undefined;
      }
      if (req.method === 'POST' && sub === 'schedules/reorder') {
        let body = '';
        req.on('data', (c) => { body += c; if (body.length > 1e5) req.destroy(); });
        req.on('end', async () => {
          let p;
          try { p = JSON.parse(body || '{}'); }
          catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
          const pairs = Array.isArray(p.schedules) ? p.schedules : (Array.isArray(p) ? p : []);
          const before = Array.isArray(p.before) ? p.before : [];
          const r = await proxyReorder(pairs);
          auditReorder({
            uid: v.uid, station: v.station,
            clientIp: req.socket ? req.socket.remoteAddress : null,
            before, after: pairs, ok: r.ok, error: r.error,
          });
          if (!r.ok) {
            pushEvent('warn', '广播社成员排期保存失败', { username: v.uid, station: v.station, code: r.code || 0, error: r.error });
            return send(r.code && r.code >= 400 && r.code < 500 ? r.code : 502, { ok: false, error: r.error });
          }
          pushEvent('info', '广播社成员排期已保存', { username: v.uid, station: v.station, count: pairs.length });
          return send(200, { ok: true });
        });
        return undefined;
      }
      return send(405, { ok: false, error: '该子路由仅支持 GET/POST' });
    }
    return send(404, { ok: false, error: '未知的 broadcast-club 子路由' });
  }

  if (req.method === 'GET' && u.pathname === '/api/public/ota') {
    // ⚠️ 2026-10-14 测试通道抓到的缺口：本端点此前走通用 send()，**不带 cache-control**
    //    ⇒ 公网 CDN/代理按启发式缓存，发布新版本后官网下载中心可能几分钟不刷新。
    //    版本信息是公开只读内容：private + 60s —— 允许浏览器短暂缓存，不让共享缓存长期囤。
    const sendOta = (obj) => {
      const body = JSON.stringify({ apiVersion: API_VERSION, ...obj });
      res.writeHead(200, {
        'content-type': 'application/json; charset=utf-8',
        'content-length': Buffer.byteLength(body),
        'x-api-version': API_VERSION,
        'cache-control': 'private, max-age=60',
      });
      res.end(body);
    };
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
      return sendOta({ ok: true, product, published: false, latest: null, hasUpdate: false });
    }
    return sendOta({
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
    if (safeEqual(tok, VIEWER_TOKEN)) {
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
    u.pathname.startsWith('/api/admin/provision') ||
    u.pathname.startsWith('/api/admin/cims') ||
    // 2026-10-18 · 班级点歌凭据：能写 Key 等于能冒用该班身份点歌，必须走管理面鉴权。
    // ⚠️ 用 startsWith 覆盖全部子路由（同上方 provision 的教训：精确相等会漏掉子路由）。
    u.pathname.startsWith('/api/admin/songkeys') ||
    // 2026-10-20 · RBAC 第二批·用户权限（user-claims）：能改=能给别人提权，必须管理面鉴权
    u.pathname.startsWith('/api/admin/user-claims') ||
    // 2026-10-23 · 广播社成员管理（仅全权/平台超管）
    u.pathname.startsWith('/api/admin/broadcast-club');
  const isManageApi =
    u.pathname === '/api/instructions' || u.pathname === '/api/devices' ||
    u.pathname === '/api/events' || u.pathname === '/api/frame' ||
    u.pathname === '/api/instructions/pending' || u.pathname === '/api/instructions/notice' ||
    u.pathname === '/api/ota/latest' || u.pathname.startsWith('/api/repairs') ||
    u.pathname === '/api/recordings' || u.pathname === '/api/qml-patch' ||
    u.pathname === '/api/qml-patch/download' || u.pathname.startsWith('/api/interact') ||
    // 2026-10-19 · 动作排行（管理面统计，需鉴权）
    u.pathname === '/api/actions/ranking' ||
    // 2026-10-18 · 点歌请求：教室端发起，但**必须鉴权**——
    // 教室身份由班级代表，不允许公网匿名 POST 刷爆 VoiceHub 的点歌配额。
    u.pathname === '/api/songs/request' || isAdminApi;
  if (isManageApi) {
    const auth = req.headers.authorization || '';
    const token = auth.toLowerCase().startsWith('bearer ') ? auth.slice(7).trim() : '';
    /* 两条通道都认，2026-10-06 一户通：
     *   ① 手敲的长期令牌 CLOUD_VIEWER_TOKEN —— 老机器/离线机房照旧，不能因为加了账号就断人路；
     *   ② 网站账号签出来的管理台票据 —— 有 uid，被抄走也能在事件流水里看到是谁的。
     * 顺序上是"先比静态令牌，再试票据"：静态令牌长得不像票，验它不会有任何副作用。 */
    const isStatic = !!token && safeEqual(token, VIEWER_TOKEN);
    const tver = !isStatic && token ? verifyViewerTicket(token) : { ok: false };
    const isTicket = tver.ok;
    if (!token || (!isStatic && !isTicket)) {
      pushEvent('warn', 'HTTP 管理面鉴权失败，已拒绝', { path: u.pathname });
      // 错误码体系（2026-10-06）：REST 错误同样带 ecode（AU1003 令牌无效），站点/客户端可按码判断
      return send(401, { ok: false, ecode: 'AU1003', code: 'TOKEN_INVALID', error: '未授权：需要 Bearer <CLOUD_VIEWER_TOKEN> 或网站账号签发的票据' });
    }
    // 2026-10-23 · 广播社成员令牌（broadcast_club 角色）只允许 /api/broadcast-club/*，
    // 禁止进入管理面（防越权：成员票若带空 scopes 会被 identityFromAuth 当成全权）。
    if (isTicket && Array.isArray(tver.claims?.roles) && tver.claims.roles.includes('broadcast_club')) {
      pushEvent('warn', '广播社成员令牌尝试访问管理面，已拒绝', { uid: tver.uid });
      return send(401, { ok: false, ecode: 'AU1003', code: 'TOKEN_INVALID', error: '该令牌无权访问管理面' });
    }
    if (isTicket) req.__ticketUid = tver.uid;
    // 2026-10-15（权限契约）：鉴权结果挂到 req，REST 的「返回设备 / 下发指令」路径按 scopes 过滤
    req.__auth = isStatic ? { ok: true, mode: 'token', uid: null } : tver;
    // 2026-10-20（RBAC 第二批）：票无 claims 时按 uid 从云端存储回填，权限真正流动；
    //   存储为空 → 不回填 ⇒ scopes=null ⇒ 全权（行为=今天，零回归）。
    applyStoredClaims(req.__auth);

    // 2026-10-26（大范围找活 · 审计面三）：admin/* 平台级端点统一平台身份闸门。
    // 此前 isManageApi 只认「有效令牌或有效票据」，而 config/ota/cims/provision/songkeys
    // 的 handler 内部都不查 isPlatformIdent —— 于是「只属于某个班的受限教师票据」也能
    // 改写服务端令牌、发全校 OTA、拉装机包（含设备令牌）、下发 CIMS。
    // 现统一要求：平台超管（roles 含 platform_admin）或全权身份（静态令牌 / scopes=null）。
    // 与既有 user-claims / broadcast-club 的守门口径一致。
    if (isAdminApi) {
      const __adminIdent = identityFromAuth(req.__auth);
      const __canAdmin = __adminIdent && (isPlatformIdent(__adminIdent) || __adminIdent.scopes === null);
      if (!__canAdmin) {
        pushEvent('warn', 'HTTP 管理面越权：非平台身份访问 admin 平台端点，已拒绝',
          { path: u.pathname, uid: req.__ticketUid || 'static' });
        return send(403, { ok: false, ecode: 'PV4002', code: 'NO_DEVICE_PERMISSION', error: '需要平台管理员身份' });
      }
    }

  // ── 2026-10-18 · 班级点歌凭据管理（管理面鉴权，见上 isAdminApi）──────────
  // GET  /api/admin/songkeys            → 清单（**只有 tail，无明文**）
  // POST /api/admin/songkeys            → { className, apiKey } 设置某班的 VoiceHub Key
  // POST /api/admin/songkeys/remove     → { className } 删除
  // ⚠️ 任何响应都**不回明文**；Key 只存在于云端加密库与"代发点歌"的内存里。
  // ⚠️ 本 handler 不是 async：请求体必须在 'end' 回调里取（与 /api/repairs 同款）。
  if (u.pathname.startsWith('/api/admin/songkeys')) {
    const sub = u.pathname.slice('/api/admin/songkeys'.length).replace(/^\/+/, '');
    if (req.method === 'GET' && sub === '') {
      return send(200, {
        ok: true,
        enabled: !!songKeyStore && songKeyStore.enabled,
        keys: songKeyStore ? songKeyStore.list() : [],
        // 没配 STE_SONGKEY_SECRET 时明确说"未启用"，而不是给空清单让人以为没配过
        ...(songKeyStore && songKeyStore.enabled ? {} : { hint: '未配置 STE_SONGKEY_SECRET（需 ≥16 字符），该功能未启用' }),
      });
    }
    if (req.method === 'POST' && (sub === '' || sub === 'remove')) {
      let body = '';
      req.on('data', (c) => { body += c; if (body.length > 1e4) req.destroy(); });
      req.on('end', () => {
        let p;
        try { p = JSON.parse(body || '{}'); }
        catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
        if (!songKeyStore) return send(503, { ok: false, error: '凭据库未初始化' });
        const cls = String(p.className || '').trim();
        if (sub === 'remove') {
          if (!songKeyStore.remove(cls)) return send(404, { ok: false, error: '该班级没有配置凭据' });
          pushEvent('info', '班级点歌凭据已删除', { className: cls });
          return send(200, { ok: true });
        }
        const r = songKeyStore.setKey(cls, p.apiKey);
        if (!r.ok) return send(400, { ok: false, error: r.message || r.error });
        // 事件里只记 tail，**绝不记 Key 明文**
        pushEvent('info', '班级点歌凭据已更新', { className: cls, tail: r.tail });
        return send(200, { ok: true, tail: r.tail });
      });
      return undefined;
    }
    return send(404, { ok: false, error: '未知的 songkeys 子路由' });
  }

  // ── 2026-10-18 · 点歌请求（被控端 → 云端代发 → VoiceHub）──────────────
  // 用户拍板 A1：每台被控端绑定到班级，点歌跟随班级账号同步。
  // 端上只发"我要点这首歌"，**不持有 Key**；真正写 VoiceHub 的是本端点，
  // 用该设备所属班级的 Key。
  //
  // 鉴权：走管理面同一套（Bearer 令牌/票据）—— 因为教室里"谁在点"由班级身份代表，
  //   不能让公网匿名 POST 就能刷爆 VoiceHub 的点歌配额。
  // 归属：uid → classCode（registry 的 classIndex 已有）；查不到班级 ⇒ 拒绝，
  //   而不是"随便挑一个班的 Key"（那会点错班）。
  if (req.method === 'POST' && u.pathname === '/api/songs/request') {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 1e4) req.destroy(); });
    req.on('end', async () => {
      let p;
      try { p = JSON.parse(body || '{}'); }
      catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }

      const uid = String(p.uid || '').trim();
      if (!uid) return send(400, { ok: false, error: '缺少 uid（哪台设备点的歌）' });

      // 设备必须在已知设备表里，否则无法确定班级
      const dev = getDevice(uid);
      if (!dev) return send(404, { ok: false, error: `未知设备 ${uid}` });
      const classCode = dev.classCode || getDeviceClassCode(uid);
      if (!classCode) {
        return send(409, { ok: false, error: '该设备尚未绑定班级，无法点歌（请先在管理端完成分班）' });
      }

      if (!songKeyStore || !songKeyStore.enabled) {
        return send(503, { ok: false, error: '班级点歌凭据功能未启用（云端未配置 STE_SONGKEY_SECRET）' });
      }
      const apiKey = songKeyStore.getKey(classCode);
      if (!apiKey) {
        return send(409, { ok: false, error: `该班级（${classCode}）尚未配置 VoiceHub 点歌凭据` });
      }

      const r = await requestSongViaVoiceHub({
        apiKey,
        title: p.title,
        artist: p.artist,
        cover: p.cover,
        extra: p.extra,
      });
      if (!r.ok) {
        pushEvent('warn', '点歌请求被上游拒绝', { uid, classCode, code: r.code || 0, error: r.error });
        // 上游 4xx 原样透传语义（配额/黑名单/参数），5xx/超时归 502
        return send(r.code && r.code >= 400 && r.code < 500 ? r.code : 502,
                    { ok: false, error: r.error, code: r.code || 0 });
      }
      pushEvent('info', '点歌请求已提交 VoiceHub', {
        uid, classCode, title: String(p.title || '').slice(0, 60),
      });
      return send(200, { ok: true, upstream: r.data });
    });
    return undefined;
  }
  }

  if (req.method === 'GET' && u.pathname === '/api/devices') {
    // 2026-10-15（权限契约 §4.3）：列表按本请求 scopes 过滤，不吐全班/全校给没权限的人。
    const ident = identityFromAuth(req.__auth);
    const visible = filterDevices(ident, listDevices());
    // 2026-10-20（RBAC 第二批）：字段脱敏 —— 受限身份一律剥 ip/mac 系字段
    // （老师/助教不拿敏感字段；当前 listDevices 本就不含 ip/mac，此为防御性守卫）。
    return send(200, { ok: true, count: visible.length, devices: maskDevices(ident, visible) });
  }

  // ── 2026-10-20 · RBAC 第二批：用户 claims 管理（云端回填存储的运维面）────────
  // 权限真源在站点；这里只是「站点签不了 claims 票期间的云端回填存储」。
  // GET    /api/admin/user-claims        → 全量清单（管理台对账 / 展示用）
  // PUT    /api/admin/user-claims        → { uid, schoolId?, roles?, scopes? } 写入/覆盖
  // DELETE /api/admin/user-claims?uid=x  → 删除（删除后回到「无 claims ⇒ 全权」）
  // ⚠️ 权限：仅「平台超管」或「全权身份」（scopes===null：静态令牌/无 claims 旧票）可写；
  //    受限身份（teacher/assistant 的 scopes 数组票）一律 403 —— 能改 claims = 能给别人提权。
  if (u.pathname === '/api/admin/user-claims') {
    const uident = identityFromAuth(req.__auth);
    const canAdmin = uident && (isPlatformIdent(uident) || uident.scopes === null);
    if (!canAdmin) {
      return send(403, { ok: false, code: 'PERMISSION_DENIED', error: '仅平台超管可管理用户权限' });
    }
    if (req.method === 'GET') {
      const users = listUserClaims();
      return send(200, { ok: true, count: users.length, users });
    }
    if (req.method === 'PUT') {
      let body = '';
      req.on('data', (c) => { body += c; if (body.length > 1e5) req.destroy(); });
      req.on('end', () => {
        let p;
        try { p = JSON.parse(body || '{}'); }
        catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
        const uid = String(p.uid || '').trim();
        if (!uid) return send(400, { ok: false, error: '缺少 uid（给谁设权限）' });
        if (p.roles !== undefined && !Array.isArray(p.roles)) return send(400, { ok: false, error: 'roles 必须是数组' });
        if (p.scopes !== undefined && !Array.isArray(p.scopes)) {
          return send(400, { ok: false, error: 'scopes 必须是数组（空数组 = 显式零权限，与无 claims 的全权严格区分）' });
        }
        const who = req.__auth?.uid ?? req.__auth?.mode ?? '?';
        const r = setUserClaims(uid, { schoolId: p.schoolId, roles: p.roles, scopes: p.scopes }, who);
        // 日志只记尾/条数，不记完整 scopes（对齐「最小化回显」纪律）
        pushEvent('info', '用户权限已更新（user-claims）', {
          uid, who, roleCount: (r.saved?.roles || []).length, scopeCount: (r.saved?.scopes || []).length,
          persisted: r.persisted,
        });
        return send(200, { ok: true, user: r.saved });
      });
      return undefined;
    }
    if (req.method === 'DELETE') {
      const uid = String(u.searchParams.get('uid') || '').trim();
      if (!uid) return send(400, { ok: false, error: '缺少 uid 参数（?uid=…）' });
      const r = deleteUserClaims(uid);
      const who = req.__auth?.uid ?? req.__auth?.mode ?? '?';
      pushEvent('info', '用户权限已删除（user-claims）', { uid, existed: r.existed, who });
      return send(200, { ok: true, existed: r.existed });
    }
    return send(405, { ok: false, error: '仅支持 GET / PUT / DELETE' });
  }

  // ── 2026-10-23 · 广播社成员管理（管理面鉴权，仅全权/平台超管）────────────
  // GET    /api/admin/broadcast-club          → 成员清单 + VoiceHub 写配置状态
  // POST   /api/admin/broadcast-club          → { username, password, station } 创建成员
  // POST   /api/admin/broadcast-club/disable  → { username } 禁用成员
  if (u.pathname.startsWith('/api/admin/broadcast-club')) {
    const uident = identityFromAuth(req.__auth);
    const canAdmin = uident && (isPlatformIdent(uident) || uident.scopes === null);
    if (!canAdmin) return send(403, { ok: false, code: 'PERMISSION_DENIED', error: '仅平台超管可管理广播社成员' });
    const sub = u.pathname.slice('/api/admin/broadcast-club'.length).replace(/^\/+/, '').replace(/\/+$/, '');
    if (req.method === 'GET' && sub === '') {
      return send(200, { ok: true, configured: voicehubConfigured(), members: listClubMembers() });
    }
    if (req.method === 'POST' && sub === '') {
      let body = '';
      req.on('data', (c) => { body += c; if (body.length > 1e4) req.destroy(); });
      req.on('end', () => {
        let p;
        try { p = JSON.parse(body || '{}'); } catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
        const who = req.__auth?.uid ?? req.__auth?.mode ?? '?';
        const r = createClubMember(p.username, p.password, p.station, who);
        if (!r.ok) return send(400, { ok: false, error: r.error });
        pushEvent('info', '广播社成员已创建', { username: r.username, station: r.station, by: who, persisted: r.persisted });
        return send(200, { ok: true, username: r.username, station: r.station });
      });
      return undefined;
    }
    if (req.method === 'POST' && sub === 'disable') {
      let body = '';
      req.on('data', (c) => { body += c; if (body.length > 1e4) req.destroy(); });
      req.on('end', () => {
        let p;
        try { p = JSON.parse(body || '{}'); } catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
        const r = disableClubMember(p.username);
        if (!r.ok) return send(404, { ok: false, error: r.error });
        pushEvent('info', '广播社成员已禁用', { username: p.username });
        return send(200, { ok: true });
      });
      return undefined;
    }
    return send(405, { ok: false, error: '该子路由仅支持 GET / POST' });
  }

  if (req.method === 'GET' && u.pathname === '/api/events') {
    return send(200, { ok: true, events: listEvents(Number(u.searchParams.get('limit') || 50)) });
  }

  // 2026-10-19 · 动作执行排行（用户指令②方向 A：基于云端 instructions 表）
  //   GET /api/actions/ranking?days=7&limit=20 → 按执行次数降序的动作排行
  if (req.method === 'GET' && u.pathname === '/api/actions/ranking') {
    const days = Math.max(1, Math.min(90, Number(u.searchParams.get('days') || 7)));
    const limit = Math.max(1, Math.min(50, Number(u.searchParams.get('limit') || 20)));
    return send(200, { ok: true, ranking: actionRanking({ days, limit }) });
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
  // 2026-10-15 修：这条路上原来**只认静态令牌**（CLOUD_VIEWER_TOKEN），登录票据 401。
  // viewer 用「站点登录票据」访问 /api/classes /api/storage 时被拒（Host requires
  // authentication），画面 0 帧的黑屏根因之一。改成与 isManageApi 一致：先静态后票据。
  // 票据带 claims/scopes（权限契约 §4.3），是权限唯一入口 —— 只认静态令牌既漏票据用户
  // 又绕开权限层，与「云端任何路径必须过 can()」冲突。
  // ⚠️ `/api/classes/dir`（班级目录，2026-10-20）必须一起走鉴权：它吐出别名映射，
  //    能看出「哪些站点 class_id 归到哪个班」，属于管理面信息。
  const isViewerApi = u.pathname === '/api/storage' || u.pathname === '/api/classes'
    || u.pathname === '/api/classes/dir'
    || u.pathname === '/api/device/bind-class' || u.pathname === '/api/me/classes'
    || u.pathname === '/api/env/test-whitelist'   // 2026-10-24 环境白名单（viewer 面板可看可改）
    || u.pathname.startsWith('/api/class/');   // 2026-10-24 班级管理四路由（/api/class/ 前缀不撞 /api/classes）
  if (isViewerApi) {
    const vauth = req.headers.authorization || '';
    const vtoken = vauth.toLowerCase().startsWith('bearer ') ? vauth.slice(7).trim() : '';
    const vStatic = !!vtoken && safeEqual(vtoken, VIEWER_TOKEN);
    const vTicket = !!vtoken && !vStatic ? verifyViewerTicket(vtoken) : { ok: false };
    if (!vtoken || (!vStatic && !vTicket.ok)) {
      return send(401, { ok: false, ecode: 'AU1001', code: 'TOKEN_INVALID', error: '未授权：需要管理端令牌或登录票据' });
    }
    req.__auth = vStatic ? { ok: true, mode: 'token', uid: null } : vTicket;
    // 2026-10-20（RBAC 第二批）：同 isManageApi 一家的回填 —— 票无 claims 按 uid 从存储取。
    applyStoredClaims(req.__auth);
  }
  if (req.method === 'GET' && u.pathname === '/api/storage') {
    return send(200, { ok: true, ...getStorageInfo() });
  }
  if (req.method === 'GET' && u.pathname === '/api/classes') {
    const all = listClasses();
    const who = req.__auth || {};
    // 2026-10-25（用户点单「接 my-classes 过滤」）：登录票据者只返回**自己带的班**。
    // 静态令牌（系统身份 / 运维）与未登录 → 仍给全量（运维要看得见全校）。
    // 兜底：该用户一条 membership 都没有时返回全量 + filtered:false ——
    //   照字面过滤会让"还没加成员"的部署看到空列表，那不是安全，是把人锁在门外。
    if (who.mode === 'ticket' && who.uid) {
      const mine = adminMyClasses({ uid: who.uid });
      if (mine.ok && mine.classes.length > 0) {
        const roleMap = new Map(mine.classes.map((x) => [x.classCode, x.role]));
        const own = all.filter((c) => roleMap.has(c.code))
                       .map((c) => ({ ...c, myRole: roleMap.get(c.code) }));
        return send(200, { ok: true, classes: own, filtered: true });
      }
    }
    return send(200, { ok: true, classes: all, filtered: false });
  }
  if (req.method === 'GET' && u.pathname === '/api/classes/dir') {
    // 班级目录（身份真源，2026-10-20）：每个班的 canonical 码 / 显示名 / 别名 / 来源。
    // 用途：① 排障「同一个班为什么长出两个码」；② 站点对账，确认它的 class_id 已作为别名登记。
    return send(200, { ok: true, size: classDirSize(), classes: listClassDir() });
  }
  if (req.method === 'GET' && u.pathname === '/api/me/classes') {
    // 管理端（登录账号）可见的班级列表。
    // 2026-10-25：与 /api/classes 同口径 —— 票据身份只回**自己带的班**（并带 myRole），
    //   静态令牌（系统身份 / 运维）或该账号还没有任何成员记录时回全量 + filtered:false。
    //   两个端点各写一份过滤是下策，但它们的返回结构不同（这里要给 myRole 供界面显示身份），
    //   合并成公共函数反而要维护两套形状 —— 所以显式重复，并在两处注释里互相指向。
    const allMine = listClasses();
    const whoMine = req.__auth || {};
    if (whoMine.mode === 'ticket' && whoMine.uid) {
      const mine = adminMyClasses({ uid: whoMine.uid });
      if (mine.ok && mine.classes.length > 0) {
        const roleMap = new Map(mine.classes.map((x) => [x.classCode, x.role]));
        const own = allMine.filter((c) => roleMap.has(c.code))
                           .map((c) => ({ ...c, myRole: roleMap.get(c.code) }));
        return send(200, { ok: true, classes: own, filtered: true });
      }
    }
    return send(200, { ok: true, classes: allMine, filtered: false });
  }
  if (u.pathname === '/api/env/test-whitelist') {
    // 环境白名单（2026-10-24）：谁是测试机由云端清单裁决，设备自报 env 一律忽略。
    // 查看：任何已鉴权 viewer。修改：静态令牌（全权）或平台超管 ——
    // 白名单 = 调试特权发放，不是普通老师能干的事。
    if (req.method === 'GET') {
      return send(200, { ok: true, testUids: listTestUids() });
    }
    if (req.method === 'POST') {
      const uident = identityFromAuth(req.__auth);
      const allowed = req.__auth?.mode === 'token' || isPlatformIdent(uident);
      if (!allowed) return send(403, { ok: false, ecode: 'AU1004', error: '只有平台管理员可修改测试机白名单' });
      // 重认证门：加减测试机 = 发放调试特权，票据者过期必须重新验证。
      const who = req.__auth;
      if (who && who.mode === 'ticket' && !isFresh(who.uid, REAUTH_INTERVAL_MS)) {
        return send(403, { ok: false, ecode: 'AU1002', code: 'REAUTH_REQUIRED', error: '需要重新验证密码后再操作' });
      }
      let raw = '';
      req.on('data', (c) => { raw += c; if (raw.length > 1e4) req.destroy(); });
      req.on('end', () => {
        let body = {};
        try { body = JSON.parse(raw || '{}'); } catch { return send(400, { ok: false, error: '请求体不是合法 JSON' }); }
        const uid = String(body.uid ?? '').trim();
        if (!uid) return send(400, { ok: false, error: '缺 uid' });
        const r = setTestUid(uid, !!body.test);
        if (!r.ok) return send(500, { ok: false, error: r.error });
        const changed = recomputeDeviceEnv();
        pushEvent('info', r.test ? '测试机白名单增加设备' : '测试机白名单移除设备', { uid, changed });
        return send(200, { ok: true, uid, test: r.test, changed });
      });
      return;
    }
    return send(405, { ok: false, error: '仅支持 GET/POST' });
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
      // 2026-10-20（班级真源统一）：允许**只给班级名**——管理台手里常常只有「高二(3)班」，
      // 没有内部码。统一解析器负责同码/同名/别名归一，管理台不必先查一遍码再下发。
      const resolved = resolveClassCode({ code: classCode, name: className, source: 'admin' });
      if (!resolved.ok) {
        const msg = (classCode || className)
          ? '班级码格式不合法（需 6-16 位字母数字或 -_）'
          : '缺班级（班级码或班级名至少给一个）';
        return send(400, { ok: false, error: msg });
      }
      const r = bindDeviceToClass(resolved.code, uid, className);
      // G4（2026-10-24）：设备在线就立刻下发 class-bound，被控端更新 classcode.txt
      //   ⇒ 缩略图上报随即开工（它要求 classCode 非空）。否则要等设备重连才知道。
      if (r.ok) notifyClassBoundIfOnline(uid, r.classCode, r.class.name);
      return send(r.ok ? 200 : 400, { ok: r.ok, ...r });
    });
    return;
  }

  // ── 被控端访问密码（2026-10-26）：设置/清除 + 查询状态 ──
  // 设备访问密码 = 远端接管（VNC 远控）前的第二道闸，由云端统一存储与下发修改。
  // 鉴权：viewer 令牌 + 按设备班级 canAccess（与远控同一口径）。
  if (u.pathname === '/api/device/vnc-access') {
    const who = req.__auth;
    if (!who) return send(401, { ok: false, error: '未鉴权' });
    const ident = identityFromAuth(who);
    if (req.method === 'GET') {
      const uid = String(u.searchParams.get('uid') || '').trim();
      if (!uid) return send(400, { ok: false, error: '缺 uid' });
      if (!canAccess(ident, getDeviceClassCode(uid)))
        return send(403, { ok: false, error: '无权限访问该设备' });
      return send(200, { ok: true, enabled: getVncAccess(uid).enabled });
    }
    if (req.method === 'POST') {
      let raw = '';
      req.on('data', (c) => { raw += c; if (raw.length > 1e4) req.destroy(); });
      req.on('end', () => {
        let body = {};
        try { body = JSON.parse(raw || '{}'); } catch { /* 非法 JSON → 走 query */ }
        const uid = String(body.uid ?? u.searchParams.get('uid') ?? '').trim();
        const password = String(body.password ?? u.searchParams.get('password') ?? '').valueOf();
        if (!uid) return send(400, { ok: false, error: '缺 uid' });
        if (!canAccess(ident, getDeviceClassCode(uid)))
          return send(403, { ok: false, error: '无权限访问该设备' });
        if (password.length > 64) return send(400, { ok: false, error: '密码过长（≤64 字符）' });
        const r = setVncAccess(uid, password);   // password 空 = 清除（关闭）
        if (!r.ok) return send(400, { ok: false, error: r.error || '设置失败' });
        // 下发到被控端本地持久化（明文只在已鉴权的设备 WS 上走，云端不存明文）。
        if (r.enabled) {
          const okPush = sendToAgentMsg(uid, 'vnc-access', { enabled: true, password });
          if (!okPush.ok)
            pushEvent('warn', '访问密码已存云端，但下发被控端失败（设备可能不在线）', { uid });
        } else {
          sendToAgentMsg(uid, 'vnc-access', { enabled: false, password: '' });
        }
        pushEvent('info', '被控端访问密码已' + (r.enabled ? '设置' : '清除'), { uid, who: who.uid ?? who.mode ?? '?' });
        return send(200, { ok: true, enabled: r.enabled });
      });
      return;
    }
    return send(405, { ok: false, error: '方法不支持' });
  }

  // 班级管理（2026-10-24）：POST /api/class/{create,rename,delete,unbind,
  // member-add,member-remove,members,my-classes}。
  // 鉴权在上面 isViewerApi 组（startsWith '/api/class/'）。业务全在 class-admin.js。
  // 传 ident：成员管理要做「谁能管这个班」的第二道判定（票里 scopes ∪ 本班 owner/teacher）。
  if (req.method === 'POST' && u.pathname.startsWith('/api/class/')) {
    // 2026-10-24 重认证门：班级写操作（建/删/改名/绑设备/加人）都敏感，票据者过期即拒。
    // 静态令牌（系统身份）不适用。403 → viewer 端弹密码框重登后重试。
    const who = req.__auth;
    if (who && who.mode === 'ticket' && !isFresh(who.uid, REAUTH_INTERVAL_MS)) {
      pushEvent('warn', '管理端重认证拦截：HTTP 班级写操作需重新验证',
        { uid: who.uid, path: u.pathname });
      return send(403, { ok: false, ecode: 'AU1002', code: 'REAUTH_REQUIRED', error: '需要重新验证密码后再操作' });
    }
    return handleClassAdminApi(req, res, u, send, identityFromAuth(req.__auth));
  }

  // ── 设备绑定：二维码 + 绑定码（2026-10-20）────────────────────────────────
  //
  // 背景：OOBE 向导（被控端首次启动引导）有两条绑定路，**云端一条都没有** ——
  //   ① 扫码路 GET /api/device/bind-qr 与 GET /api/device/bind-status
  //   ② 输码路 POST /api/device/activate
  //   于是向导只能降级到"输入绑定码"，而输码那条打的是站点（www.*），不在本服务内。
  //   本段把两条路都建在云端：**设备侧与云端同一台机器**，少一跳、少一处口径。
  //
  // 两种票（见 bind-qr.js 头注释）：
  //   kind='device'：设备自助开码（码里没班级，手机落地页选）—— 无人值守场景
  //   kind='class' ：管理员按班开码（码里带班级，老师直接输/扫确认）—— 有管理员场景
  //
  // 鉴权口径：
  //   · 设备侧三个端点**开放**（此时设备还没绑班、也没有管理端身份，只能开放）；
  //     防滥用靠票的 TTL + 一次性 + 码表上限 + uid 绑定校验；
  //   · 管理端按班发码走 POST /api/class/bindcode（班级域：viewer 令牌 + 按班权限，
  //     见 class-admin.js 的 adminIssueBindCode）。2026-10-25 收归我方后，
  //     原 /api/admin/bindcode 与「站点 XJK 码代兑换」两条旧路已删除。

  // 扫出来的地址必须是**手机能访问**的公网地址。优先用请求自己的 Host（反代会带
  // x-forwarded-proto），必要时用 STE_BIND_PUBLIC_ORIGIN 显式覆盖（自建/内网调试）。
  function bindOrigin() {
    const explicit = String(process.env.STE_BIND_PUBLIC_ORIGIN || '').trim();
    if (explicit) return explicit.replace(/\/+$/, '');
    const host = String(req.headers['x-forwarded-host'] || req.headers.host || '').trim();
    if (!host) return '';
    const proto = String(req.headers['x-forwarded-proto'] || '').split(',')[0].trim() || 'http';
    return `${proto}://${host}`;
  }

  // 老师手输"班级名称"时，服务端要给它一个稳定的班级码 —— 这件事**只有 class-dir.js 一处实现**
  // （2026-10-20 统一）。这里原来有个内联 deriveClassCode：先查同名班、否则 sha256 派生。
  // 它和站点 class_id 是两套命名体系，于是同一个班能长出 cls-xxx 与 class_01 两个码。
  // 现在一律走 resolveClassCode()：同码/同名/别名都归一到同一个 canonical 班。

  if (req.method === 'GET' && u.pathname === '/api/device/bind-qr') {
    // 2026-10-27（OOBE 洪水加固）：匿名可连续签发绑定码 → 按源 IP 60s/6 次限流。
    // OOBE 正常换码重试 1-3 次/分钟，留足裕量；超限 429 + 人话，不静默。
    if (!hitRate(`bindqr:${clientIp(req)}`, 6, 60_000)) {
      res.writeHead(429, { 'content-type': 'application/json', 'retry-after': '60' });
      return res.end(JSON.stringify({ ok: false, error: '生成太频繁，请 1 分钟后再试' }));
    }
    const uid = String(u.searchParams.get('uid') || '').trim();
    const issued = issueDeviceBind(uid);
    if (!issued.ok) return send(400, { ok: false, error: issued.error });
    const origin = bindOrigin();
    const scanUrl = origin ? `${origin}/bind?c=${issued.ticket.code}` : '';
    let qr;
    try {
      // 内容就是落地页地址：扫码即打开手机页，不需要老师装任何 App。
      // 拿不到 origin 时退化成一个短内容（只放码），保证矩阵仍能生成 —— 但这时
      // 扫码打开不了落地页，所以同时把 scanUrl 留空让调用方知道降级了。
      qr = makeQrMatrix(scanUrl || issued.ticket.code);
    } catch (e) {
      pushEvent('warn', '二维码生成失败', { uid, error: String(e.message || e) });
      return send(500, { ok: false, error: '二维码没能生成出来，请稍后重试或改用「输入绑定码」' });
    }
    return send(200, {
      ok: true,
      code: issued.ticket.code,
      matrix: qr.rows,
      size: qr.size,
      version: qr.version,
      scanUrl,
      expiresAt: issued.ticket.expireAt,
      ttlSec: Math.round((issued.ticket.expireAt - Date.now()) / 1000),
    });
  }

  if (req.method === 'GET' && u.pathname === '/api/device/bind-status') {
    const code = String(u.searchParams.get('code') || '').trim();
    const uid = String(u.searchParams.get('uid') || '').trim();
    const r = pollDeviceBind(code, uid);
    if (!r.ok) return send(410, { ok: false, bound: false, state: r.state, error: r.error });
    return send(200, {
      ok: true,
      // ⚠️ 被控端 OOBE 只认 `bound`（见 oobe_wizard.cpp:242）—— 别改成只回 state
      bound: r.state === 'bound',
      state: r.state,
      classCode: r.classCode || '',
      className: r.className || '',
    });
  }

  if (req.method === 'POST' && u.pathname === '/api/device/activate') {
    // 输码路：老师把 6 位码敲到设备上。两种票都认 ——
    //   · 管理员班级码（kind=class，码里带班级）⇒ 直接绑
    //   · 设备自己的码（kind=device）且手机已确认 ⇒ 幂等回班级（重输不会报错）
    const code = String(u.searchParams.get('code') || '').trim();
    const uid = String(u.searchParams.get('uid') || '').trim();
    if (!code) return send(400, { ok: false, error: '请输入 6 位绑定码' });
    if (!uid) return send(400, { ok: false, error: '缺少设备标识' });

    const t = getTicket(code);
    if (!t) return send(400, { ok: false, error: '绑定码无效或已过期，请向管理员再要一个' });
    if (t.state === 'bound') {
      // 已经确认过（通常是扫码确认后老师又在设备上敲了一次）：直接回结果
      if (t.uid && t.uid !== uid) return send(409, { ok: false, error: '这个绑定码已经用在别的设备上了' });
      return send(200, { ok: true, classCode: t.classCode || '', className: t.className || '' });
    }
    if (!t.classCode) {
      return send(409, {
        ok: false,
        error: '这个码需要在手机上扫码确认（或者请管理员在管理台按班级生成绑定码）',
      });
    }
    if (t.uid && t.uid !== uid) return send(409, { ok: false, error: '这个绑定码属于另一台设备' });
    const bind = bindDeviceToClass(t.classCode, uid, t.className);
    if (!bind.ok) return send(400, { ok: false, error: bind.error });
    markBound(code, { classCode: t.classCode, className: bind.class.name, uid });
    return send(200, { ok: true, classCode: t.classCode, className: bind.class.name || t.className || '' });
  }

  // 手机落地页
  if (req.method === 'GET' && u.pathname === '/bind') {
    if (!existsSync(BIND_HTML)) return send(404, { ok: false, error: 'bind.html 不存在' });
    const html = readFileSync(BIND_HTML, 'utf8');
    res.writeHead(200, {
      'content-type': 'text/html; charset=utf-8',
      'content-length': Buffer.byteLength(html),
      'cache-control': 'no-store',
    });
    return res.end(html);
  }

  if (req.method === 'GET' && u.pathname === '/api/bind/info') {
    // 2026-10-27（OOBE 洪水加固）：凭码查班级清单 + 在线数，匿名可重复调用
    // （码就挂在教室大屏上）→ 按源 IP 60s/60 次限流，防把低敏清单当无限数据泵。
    // 手机选班页正常打开 <5 次/分钟，60 次/分钟满足任何正常操作。
    if (!hitRate(`bindinfo:${clientIp(req)}`, 60, 60_000)) {
      res.writeHead(429, { 'content-type': 'application/json', 'retry-after': '60' });
      return res.end(JSON.stringify({ ok: false, error: '查询太频繁，请稍后再试' }));
    }
    const code = String(u.searchParams.get('c') || '').trim();
    const t = getTicket(code);
    if (!t) {
      return send(410, { ok: false, expired: true, error: '绑定码已过期，请回到教室电脑上刷新二维码' });
    }
    if (t.state === 'bound') {
      return send(200, { ok: true, alreadyBound: true, classCode: t.classCode, className: t.className });
    }
    return send(200, {
      ok: true,
      code: t.code,
      kind: t.kind,
      classCode: t.classCode || '',
      className: t.className || '',
      deviceUid: t.kind === 'device' ? t.uid : '',
      // 管理员码（kind=class）没有设备身份可用 ⇒ 手机页不提供"确认绑定"，
      // 改为把 6 位码显示出来让老师回大屏敲（见 bind.html）。
      needsDeviceInput: t.kind === 'class',
      expiresAt: t.expireAt,
      // 只回老师点名要选的东西：班级名与在线数，不回设备清单
      classes: listClasses().map((c) => ({ code: c.code, name: c.name, onlineCount: c.onlineCount })),
    });
  }

  if (req.method === 'POST' && u.pathname === '/api/bind/confirm') {
    let raw = '';
    req.on('data', (c) => { raw += c; if (raw.length > 4e3) req.destroy(); });
    req.on('end', () => {
      let body = {};
      try { body = JSON.parse(raw || '{}'); } catch { /* 非法 JSON → 走 query */ }
      const code = String(body.code ?? u.searchParams.get('c') ?? '').trim();
      const t = getTicket(code);
      if (!t) return send(410, { ok: false, error: '绑定码已过期，请回到教室电脑上刷新二维码' });
      if (t.state === 'bound') {
        return send(200, { ok: true, alreadys: true, classCode: t.classCode, className: t.className });
      }
      if (t.kind === 'class') {
        // 管理员码是"给设备敲的"：手机扫它没有设备身份可用（票里没 uid），
        // 所以这里**不能**假装绑定成功（那也是本文件反复禁止的假成功）。
        // 手机页会直接显示这 6 位码，让老师回教室电脑上敲。
        return send(400, {
          ok: false,
          error: '这个绑定码要在教室电脑上输入，请回到大屏按提示操作',
        });
      }
      // kind='device'：班级由老师在手机上选/填
      let classCode = String(body.classCode ?? '').trim();
      const className = String(body.className ?? '').trim();
      // 统一解析（2026-10-20）：选了已有班 / 输了新班名 / 带了个外部码，都归一到同一个 canonical。
      // 不再有"派生码与站点 class_id 并行"的第二个发行方。
      const resolved = resolveClassCode({ code: classCode, name: className, source: 'device' });
      if (!resolved.ok) {
        const msg = (!classCode && !className) ? '请选择一个班级，或输入班级名称' : resolved.error;
        return send(400, { ok: false, error: msg });
      }
      classCode = resolved.code;
      const bind = bindDeviceToClass(classCode, t.uid, className);
      if (!bind.ok) return send(400, { ok: false, error: bind.error });
      const finalName = bind.class.name || className || classCode;
      markBound(code, { classCode, className: finalName });
      return send(200, { ok: true, classCode, className: finalName });
    });
    return;
  }


  // ── 互动数据（2026-10-15 · 灵动岛扩展组件6）──
  // 点歌/祝福待审队列 + 反馈汇总 + 审批动作（HTTP 版）。管理端面板（候选，等用户点单）
  // 与站点看板将来接这些；WS 上另有同数据的实时版（song-requests.list 等消息
  // + song-request-new/blessing-new 广播）。两条路同一份 interact 库、同一套权限口径。
  if (req.method === 'GET' && u.pathname === '/api/interact/song-requests') {
    const ident = identityFromAuth(req.__auth);
    const items = listSongRequests({
      state: u.searchParams.get('state') || '',
      limit: Number(u.searchParams.get('limit') || 100),
    }).filter((it) => canAccess(ident, it.classCode || null));
    return send(200, { ok: true, items });
  }
  if (req.method === 'GET' && u.pathname === '/api/interact/blessings') {
    const ident = identityFromAuth(req.__auth);
    const items = listBlessings({
      state: u.searchParams.get('state') || '',
      limit: Number(u.searchParams.get('limit') || 100),
    }).filter((it) => canAccess(ident, it.classCode || null));
    return send(200, { ok: true, items });
  }
  if (req.method === 'GET' && u.pathname === '/api/interact/feedback/summary') {
    const ident = identityFromAuth(req.__auth);
    const s = feedbackSummary();
    if (ident && ident.scopes !== null && !isPlatformIdent(ident)) {
      s.byClass = s.byClass.filter((c) => canAccess(ident, c.classCode || null));
    }
    return send(200, s);
  }
  if (req.method === 'POST' && u.pathname === '/api/interact/handle') {
    // 审批动作。body: { kind:'song'|'blessing', id, action, reason? }。
    // 与 WS 版同口径：先探条目验权限，再动状态机；approve 祝福顺带广播弹幕。
    let raw = '';
    req.on('data', (c) => { raw += c; if (raw.length > 1e4) req.destroy(); });
    req.on('end', () => {
      let body = {};
      try { body = JSON.parse(raw || '{}'); } catch { /* 落 400 */ }
      const kind = String(body.kind || '');
      const id = Number(body.id || 0);
      const action = String(body.action || '');
      if ((kind !== 'song' && kind !== 'blessing') || !id || !action) {
        return send(400, { ok: false, error: 'body 需要 { kind: "song"|"blessing", id, action }' });
      }
      const ident = identityFromAuth(req.__auth);
      const probe = (kind === 'song' ? listSongRequests({ limit: 500 }) : listBlessings({ limit: 500 }))
        .find((it) => it.id === id);
      if (!probe) {
        return send(404, { ok: false, error: `${kind === 'song' ? '点歌' : '祝福'} #${id} 不存在` });
      }
      if (!canAccess(ident, probe.classCode || null)) {
        pushEvent('warn', '管理端越权：HTTP 审批被云端拒绝（目标不在其 scopes 内）',
          { kind, id, path: u.pathname });
        return send(403, { ok: false, ecode: 'PV4002', code: 'NO_DEVICE_PERMISSION', error: '无权限处理该内容' });
      }
      const by = req.__ticketUid || 'http-viewer';
      const r = kind === 'song'
        ? handleSongRequest(id, action, by)
        : handleBlessing(id, action, by, body.reason);
      if (!r.ok) return send(409, { ok: false, code: r.code, error: r.message });
      pushEvent('info', `HTTP 审批：${kind === 'song' ? '点歌' : '祝福'} #${id} ${action}`,
        { uid: r.item.uid, by: r.item.handledBy });
      if (kind === 'blessing' && action === 'approve') broadcastBlessingDanmaku(r.item);
      broadcastToViewers(kind === 'song' ? 'song-request-handled' : 'blessing-handled', { ok: true, item: r.item });
      return send(200, { ok: true, item: r.item });
    });
    return;
  }

  // ── 班级监控（设计文档 3.6 第一版，2026-10-07）──
  // 录制 = 被控端 ffmpeg DirectShow 本地录制（既有 camera_record_*）→ record_stop 后
  // 上传到云端 recordings/<uid>/<时间戳>.mp4 → 管理端列表 + 直接播放（HTTP 托管，第一版不切 HLS）。
  // 上传鉴权 = 设备令牌（DEV_TOKEN 或 agent ticket）；列表鉴权 = 管理端令牌。
  // RECORDINGS_ROOT 已在文件上方（/recordings/* 静态托管之前）定义 ——
  // 上传 / 列表 / 静态托管 三处共用同一目录真源（2026-10-16 修 TDZ + 目录不一致）。
  // ⚠️ 2026-10-13 三端审计修复：上传此前只有**下限**（<1KB 拒绝）、**没有上限** ——
  //    持设备令牌者可发无限大 body，chunks 全攒在内存里 ⇒ 直接把云端 OOM 掉。
  //    默认 300MB（与 OTA 护栏同量级），可用 env 覆盖。
  const MAX_RECORDING_BYTES = Number(process.env.CLOUD_MAX_RECORDING_BYTES) || 300 * 1024 * 1024;
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
    const okTok = token && (safeEqual(token, DEV_TOKEN) || !!ticketUid);
    if (!okTok || !uid) {
      return send(401, { ok: false, ecode: 'AU1003', code: 'TOKEN_INVALID', error: '上传录制需设备令牌' });
    }
    // 接收原始字节（mp4）→ 存 recordings/<uid>/<时间戳>.mp4
    const chunks = [];
    let recvBytes = 0;
    let overflowed = false;
    req.on('data', (c) => {
      if (overflowed) return;
      recvBytes += c.length;
      if (recvBytes > MAX_RECORDING_BYTES) {
        overflowed = true;
        pushEvent('warn', '录制上传超过上限，已中止', { uid, limit: MAX_RECORDING_BYTES, got: recvBytes });
        res.writeHead(413, { 'content-type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: `录制超过上限（${MAX_RECORDING_BYTES} 字节）` }));
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on('end', () => {
      if (overflowed) return;
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
    // 2026-10-26：与上面的静态下载同口径 —— 受限身份只列自己班设备的录像。
    //   此前列表不过 can()，受限老师在 viewer「监控中心 → 录制回放」里能直接看到
    //   全校各班的录像清单（画面/声音，最敏感的一类数据）。
    const listIdent = identityFromAuth(req.__auth);
    const out = {};
    if (existsSync(RECORDINGS_ROOT)) {
      for (const uid of readdirSync(RECORDINGS_ROOT)) {
        const dir = join(RECORDINGS_ROOT, uid);
        if (!statSync(dir).isDirectory()) continue;
        if (!canAccess(listIdent, getDeviceClassCode(uid))) continue;
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
    // 2026-10-26（大范围找活 · 审计面三副产物）：报修列表按班过滤 ——
    // 此前任何已登录者可见全校报修单（uid/症状/备注），与 interact* 的按班口径不一致。
    // 无班级归属的设备（classCode=null）：受限身份一律不可见（与 recordings 同口径）。
    const st = u.searchParams.get('status') || '';
    const ident = identityFromAuth(req.__auth);
    const repairs = listRepairs(st).filter((t) => canAccess(ident, getDeviceClassCode(t.uid)));
    return send(200, { ok: true, repairs });
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
        if (!t) return send(404, { ok: false, error: `报修单不存在：${rid}` });
        // 2026-10-26（副产物同批）：单条读取按目标设备班级过滤
        if (!canAccess(identityFromAuth(req.__auth), getDeviceClassCode(t.uid))) {
          return send(403, { ok: false, ecode: 'PV4002', code: 'NO_DEVICE_PERMISSION', error: '无权限查看该报修单' });
        }
        return send(200, { ok: true, ticket: t });
      }
      if (req.method === 'PATCH') {
        const t = listRepairs().find((x) => x.id === rid);
        if (!t) return send(404, { ok: false, error: `报修单不存在：${rid}` });
        // 2026-10-26（副产物同批）：状态变更按目标设备班级过滤
        if (!canAccess(identityFromAuth(req.__auth), getDeviceClassCode(t.uid))) {
          return send(403, { ok: false, ecode: 'PV4002', code: 'NO_DEVICE_PERMISSION', error: '无权限处理该报修单' });
        }
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
        const t = listRepairs().find((x) => x.id === rid);
        if (!t) return send(404, { ok: false, error: `报修单不存在：${rid}` });
        // 2026-10-26（副产物同批）：删除按目标设备班级过滤
        if (!canAccess(identityFromAuth(req.__auth), getDeviceClassCode(t.uid))) {
          return send(403, { ok: false, ecode: 'PV4002', code: 'NO_DEVICE_PERMISSION', error: '无权限删除该报修单' });
        }
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
    // 2026-10-15（权限契约 §4.3）：实时画面最敏感，读取同样过 can()
    const ident = identityFromAuth(req.__auth);
    if (!canAccess(ident, d.classCode ?? null)) {
      return send(403, { ok: false, ecode: 'PV4002', code: 'NO_DEVICE_PERMISSION', error: '无权限查看该设备画面' });
    }
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
      // 2026-10-15（权限契约 §4.3）：下发指令前必须过 can()——REST 入口同样防越权
      const ident = identityFromAuth(req.__auth);
      const tgtClass = getDeviceClassCode(uid);   // 离线设备也在 knownDevices 里有归属
      if (!canAccess(ident, tgtClass)) {
        pushEvent('warn', 'HTTP 管理面越权：指令被云端拒绝（目标不在其 scopes 内）',
          { uid, action: String(action).slice(0, 48), who: req.__auth?.uid ?? req.__auth?.mode ?? '?' });
        return send(403, { ok: false, ecode: 'PV4002', code: 'NO_DEVICE_PERMISSION', error: `无权限向设备 ${uid} 下发指令` });
      }
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
  // POST /api/admin/ota     → 写 ota.json（或带 channel 写 ota-<channel>.json）发布新版本

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
      const { product = 'agent', version, url, sha256, size, notes, mandatory, allowDowngrade, channel } = payload;
      if (!version || !url) return send(400, { ok: false, error: '缺 version 或 url' });
      // 2026-10-20：版本正则对齐 scripts/publish-release.mjs（X.Y.Z[-alpha.N|-rc.N]）。
      // 此前这里是 /^\d+(\.\d+)*$/ —— 既放行 '1' / '0.6' 这种非三段式，
      // 又**拒收 rc/alpha 候选版**：管理台发不了灰度，而灰度机制（ota-<channel>.json +
      // register channel）本来就为候选版服务 ⇒ 能力倒挂，运维只能手工编辑 ota-gray.json。
      // 阶段号从 1 起：alpha.0 曾让被控端 compareVersion 判成"正式版"⇒ 静默不升级。
      if (!/^\d+\.\d+\.\d+(?:-(?:alpha|rc)\.[1-9]\d*)?$/.test(String(version))) {
        return send(400, {
          ok: false,
          error: 'version 形如 0.6.1（正式）/ 0.6.1-rc.1（候选）/ 0.6.1-alpha.1（内测）；见 docs/版本号命名规范-2026-10-07.md',
        });
      }
      // 2026-10-20：更新通道。默认 stable 写 ota.json；灰度（如 gray）写 ota-<channel>.json。
      // 白名单与 ota.js manifestFileFor() 同一道（防 ../ 路径穿越），写哪个文件也由它算 ——
      // 读方 latestFor 与写方这里必须是同一个文件名，否则"发布成功、设备不升级"。
      const ch = String(channel || '').trim() || 'stable';
      if (ch !== 'stable' && !/^[a-z0-9_-]{1,16}$/.test(ch)) {
        return send(400, {
          ok: false,
          error: 'channel 只允许小写字母/数字/下划线/横线（1-16 位），留空表示 stable',
        });
      }
      const targetFile = manifestFileFor(ch);
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

      // 读取现有清单（有则改，无则建）。灰度通道读自己那份（ota-gray.json 等），
      // 与 stable 清单互不覆盖 —— 灰度是显式发布，不能顺手改到全网清单上。
      // ⚠️ 这里**故意不再 catch 成空对象**：旧写法遇到带 BOM 的清单（PowerShell 的
      //    Set-Content -Encoding UTF8 会写 BOM）就会 JSON.parse 抛错 → 被 catch 吞掉 →
      //    manifest 重置为 {products:{}} → **把另一个产品的条目整条抹掉**。
      //    文件坏掉应该吵出来，而不是静默清空别人的发布。
      let manifest = { products: {} };
      if (existsSync(targetFile)) {
        const raw = readFileSync(targetFile);
        if (raw[0] === 0xef && raw[1] === 0xbb && raw[2] === 0xbf) {
          return send(400, {
            ok: false,
            error: `${targetFile} 带 UTF-8 BOM，JSON.parse 会失败（云端会当成"没发布过版本"）。请另存为「UTF-8 无 BOM」后重试。`,
          });
        }
        try {
          manifest = JSON.parse(raw.toString('utf8'));
        } catch (e) {
          // 解析不了就拒绝，**不要**用空清单覆盖它
          return send(400, { ok: false, error: `现有 ${targetFile} 不是合法 JSON（${e.message}），已拒绝写入以免覆盖其它产品条目` });
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
        writeFileSync(targetFile, Buffer.from(text, 'utf8'));
        JSON.parse(readFileSync(targetFile, 'utf8')); // 写完回读，坏 JSON 当场炸
        pushEvent('info', '管理面板发布了 OTA', { product, version, channel: ch });
        return send(200, {
          ok: true, product, version, channel: ch, file: targetFile,
          sha256: sha, size: sizeNum,
          note: ch === 'stable'
            ? 'OTA 清单已写入，立即生效'
            : `灰度清单已写入，仅 register 带 channel=${ch} 的设备拿得到，立即生效`,
        });
      } catch (e) {
        return send(500, { ok: false, error: `写入 ${targetFile} 失败: ` + e.message });
      }
    });
    return undefined;
  }

  // ── CIMS 课表供给管理端（2026-10-17）────────────────────────────────────
  // GET  /api/admin/cims/status          配置（脱敏）+ 各设备缓存/最近同步结果
  // POST /api/admin/cims/pull   {uid?}   只拉取刷缓存，不下发
  // POST /api/admin/cims/apply  {uid?}   拉取并立即下发（无视"有无变化"）；不带 uid = 全部在线设备
  // ⚠️ 管理 API 不受 CIMS_ENABLED 门控——持令牌的人显式点的动作就是意图；
  //    CIMS_ENABLED 只管自动路径（设备上线钩子 + 周期同步）。
  if (req.method === 'GET' && u.pathname === '/api/admin/cims/status') {
    return send(200, { ok: true, cims: cimsStatus() });
  }

  if (req.method === 'POST' && (u.pathname === '/api/admin/cims/pull' || u.pathname === '/api/admin/cims/apply')) {
    const wantApply = u.pathname.endsWith('/apply');
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 1e5) req.destroy(); });
    req.on('end', async () => {
      let payload;
      try { payload = JSON.parse(body || '{}'); }
      catch (e) { return send(400, { ok: false, error: 'JSON 解析失败: ' + e.message }); }
      const uid = payload.uid ? String(payload.uid).trim() : '';
      pushEvent('info', `管理面板触发 CIMS 课表${wantApply ? '下发' : '拉取'}`, { uid: uid || '(全部在线)' });
      try {
        if (uid) {
          const r = await cimsSyncDevice(uid, { apply: wantApply, force: wantApply });
          return send(r.ok ? 200 : 502, { ok: r.ok, result: r });
        }
        const r = await cimsSyncAll({ apply: wantApply, force: wantApply });
        return send(200, { ok: true, result: r });
      } catch (e) {
        return send(500, { ok: false, error: e.message });
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

// 2026-10-18：远控隧道（RFB over WS）。两条专用端点，只搬字节，不解析。
// 权限口径与"看画面"一致；目标地址由 agent 决定（它只连 127.0.0.1:5900）。
const tunnel = createTunnel({
  authorizeViewer, identityFromAuth, canAccess, getDeviceClassCode,
  getDevice, sendToAgentMsg, pushEvent,
});

// 2026-10-20 · UDP 闪传通道（U1）：两端直连的 UDP 帧中继。
// 密钥经本文件里的 WS「udp-session」消息协商（服务端随机 32B）；中继按帧头 session
// 路由、原样转发，只亲自解 HELLO（验令牌）与 PING（打时间戳）。
// ⚠️ 令牌未配 ⇒ configured=false，listen 回调里只告警不拉 socket（新攻击面 fail-closed）。
// ⚠️ 可达范围仅回环/局域网：cloudflared 只转 TCP，公网 UDP 不可达。
const udpRelay = createUdpRelay({
  host: UDP_RELAY_HOST, port: UDP_RELAY_PORT, token: UDP_RELAY_TOKEN,
  maxFps: UDP_MAX_FPS_PER_SESSION, idleMs: UDP_SESSION_IDLE_MS, pushEvent,
});
let udpBound = null;   // start() 成功后回填（host/port 可能因传 0 而变）

// 2026-10-20 · TCP 闪传通道（公网主路，用户定「那就 tcp」）：与 UDP 同一套帧/加密/
// 会话存储（flash-core.js），换 TCP 管道。为什么它是主路：cloudflared 只转 TCP；
// 且专用连接把「实时/大批数据」与 WS 控制+心跳彻底分离——200KB/s 背压闸（保心跳的
// 粗暴手段，却把远控全屏首帧丢光）和文件传输停等协议的病根一起去掉。
// 严格单调闸门 + 一端断即收口 + 背压暂停源读取（不丢帧），详见 tcp-relay.js 文件头。
const tcpRelay = createTcpRelay({
  host: TCP_RELAY_HOST, port: TCP_RELAY_PORT, token: TCP_RELAY_TOKEN,
  idleMs: TCP_SESSION_IDLE_MS, helloGraceMs: TCP_HELLO_GRACE_MS, pushEvent,
});
let tcpBound = null;   // start() 成功后回填

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
  // 远控隧道：viewer 侧 / agent 侧各一条（只搬字节）
  if (u.pathname === '/ws/tunnel') {
    tunnel.handleViewerUpgrade(req, socket, head);
    return;
  }
  if (u.pathname === '/ws/tunnel-agent') {
    tunnel.handleAgentUpgrade(req, socket, head);
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
    if (safeEqual(token, VIEWER_TOKEN)) return { ok: true, mode: 'token', uid: null };
    return { ok: false, reason: '管理端令牌不对' };
  }
  const ticket = typeof msg.ticket === 'string' ? msg.ticket.trim() : '';
  if (ticket) return verifyViewerTicket(ticket);
  return { ok: false, reason: 'auth 消息里既没有 token 也没有 ticket' };
}

/** 管理端重认证门：人（账号）的验证已过期 → 拒绝本次敏感操作并回 REAUTH_REQUIRED。
 *  系统身份（静态令牌，mode='token'）永不拦截；连接不掐（ERR.REAUTH_REQUIRED close=0）。
 *  客户端收到后弹密码框 → 重新登录 → 新连接新票据 → noteVerified 刷新 → 重发操作。 */
function reauthCheck(ws, action) {
  const who = ws.__who;
  if (!who || who.mode !== 'ticket') return true;
  if (isFresh(who.uid, REAUTH_INTERVAL_MS)) return true;
  pushEvent('warn', '管理端重认证拦截：敏感操作需重新验证密码',
    { uid: who.uid, action: String(action).slice(0, 48) });
  sndErr(ws, 'REAUTH_REQUIRED',
    `距离上次验证已超过 ${Math.round(REAUTH_INTERVAL_MS / 3600000)} 小时，请重新登录后再试（连接未断开）`);
  return false;
}

/** 管理端通过鉴权之后才做的事：登记 + 补订阅 + 推初始设备表。 */
function onViewerAuthed(ws, who) {
  addViewer(ws);
  // 重认证门：人（账号）通过验证的时刻刷新点 = 每一次 auth 成功。
  // 静态令牌（who.uid=null）是系统身份，noteVerified 内部直接跳过。
  noteVerified(who.uid);
  pushEvent('info', '管理端已接入（已鉴权）', { viewers: viewerCount(), mode: who.mode, uid: who.uid || undefined });

  // 两条补订阅路径都要有，缺一条就黑屏：
  //   ① 管理端后到（教室机早就在线）→ 这里立刻订阅第一台；
  //   ② 教室机后到（管理端先开机）  → autoSubscribeViewers 在设备上线时补（见 markConnected 之后那步）。
  // 2026-10-11 修：必须选**在线**设备（listDevices 含离线 known 表，取 [0] 可能订到离线 → rtc-start 白发）。
  // 2026-10-15（权限契约 §4.3）：初始设备表按本连接 scopes 过滤；自动订阅也只挑
  // **能看**的第一台在线设备（否则把越权画面推到没权限的连接上，等于白加鉴权）。
  const ident = identityFromAuth(who);
  const visible = filterDevices(ident, listDevices());
  const firstOnline = visible.find((d) => d.online)?.uid;
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
  // 2026-10-20（RBAC 第二批）：字段脱敏 —— 受限身份剥 ip/mac 系（当前模型本无此二字段，防御性守卫）。
  snd(ws, 'devices', { devices: maskDevices(ident, visible), at: new Date().toISOString() });
}

/**
 * 画面帧转发的日志聚合（2026-10-18 · 与「语音广播」同一条降噪裁决）。
 *
 * 被控端 5fps 连推画面，原来**逐帧** pushEvent ⇒ 实测 events.log 51138 行里
 * 「帧已推给管理端」37810 条 +「帧推了但没人订阅」5966 条 = 85%，把真正有用的事件
 * （设备上下线、指令、回执）全埋了。
 * 现在：有订阅者时按 15s 窗口聚合成一条「画面帧推送中」；没人订阅时 60s 提示一次
 * （那是"管理端没连/没选这台"的配置问题，不是每帧都需要喊）。
 */
const frameStat = new Map();   // uid -> { frames, bytes, lastAt, idle }
function noteFrameRelay(uid, viewers, bytes) {
  const now = Date.now();
  const s = frameStat.get(uid) || { frames: 0, bytes: 0, lastAt: 0, idle: false };
  s.frames += 1;
  s.bytes += bytes;
  if (viewers > 0) {
    s.idle = false;
    if (now - s.lastAt >= 15000) {
      const windowSec = s.lastAt ? Math.round((now - s.lastAt) / 1000) : 0;
      pushEvent('info', '画面帧推送中', {
        uid, viewers, frames: s.frames,
        kib: Math.round(s.bytes / 1024), windowSec,
      });
      s.lastAt = now;
      s.frames = 0;
      s.bytes = 0;
    }
  } else if (!s.idle || now - s.lastAt >= 60000) {
    pushEvent('warn', '帧推了但没人订阅（管理端没连或没选这台）', { uid });
    s.idle = true;
    s.lastAt = now;
    s.frames = 0;
    s.bytes = 0;
  }
  frameStat.set(uid, s);
}

/** 平台级超管判定（perms.js 的 isPlatform 未导出，这里按同一口径复述一遍——
 *  口径必须与 perms.js 逐字一致：schoolId=null 且 roles 含 platform_admin）。 */
function isPlatformIdent(ident) {
  return !!ident && ident.schoolId == null && Array.isArray(ident.roles) && ident.roles.includes('platform_admin');
}

/**
 * 审批通过的祝福 → 该班在线设备的通知弹幕（kind=danmaku，组件5 的可信源路径）。
 *
 * 为什么走 notify 指令而不是新协议：被控端 notifyFromParams 已完整支持 kind=danmaku
 * （DanmakuWidget::push：80 字/120px/s/颜色可指定/无过滤无冷却）——管理端审批过的
 * 内容天然是可信源，与端侧 sendBlessingLocal（30 字/100px/s/过滤/冷却，学生不可信源）
 * 形成「学生投稿 → 管理端审批 → 可信广播」的完整闭环，零新协议、零新端侧代码。
 *
 * 班级归属空（设备没绑班）→ 只发提交者那台（online 才发）；广播范围绝不默认"全班"，
 * 否则一条漏绑班的祝福会滚到全校屏幕上。
 * 设备不在线：sendInstruction 如实回 offline，这里只记流水不重试——祝福是即时内容，
 * 隔天补播没有意义；要重放是管理端面板将来的事（组件6 范围外）。
 */
function broadcastBlessingDanmaku(item) {
  const targets = item.classCode ? getDevicesOfClass(item.classCode) : [item.uid];
  let hits = 0, offline = 0;
  for (const t of targets) {
    const r = sendInstruction(t, 'notify', {
      kind: 'danmaku',
      title: '祝福',
      content: item.text,
    });
    if (r.state === 'sent') hits++;
    else offline++;
  }
  pushEvent('info', `祝福 #${item.id} 已广播（${hits} 台在线${offline ? `，${offline} 台离线未发` : ''}）`,
    { uid: item.uid, classCode: item.classCode || null, hits, offline });
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
      // 管理端二进制帧 = 语音 PCM / 屏幕广播 JPEG / 远控 vnc-data（2026-10-18）。
      const buf = Buffer.from(data);
      // ── 远控统一 A：vnc-data 帧 → 目标设备主 WS（原样信封，云端只按 uid 中继）──
      // 权限与「订阅画面」同一口径：看得到这台才能控它。语音/广播的裸字节走
      // parseAnyFrame 的 legacy 分支（header=null），不会误进这条。
      let f = null;
      if (authed) {
        f = parseAnyFrame(buf);
        if (f.ok && f.header?.type === 'vnc-data') {
          const uid = String(f.header.uid ?? '').trim();
          if (!uid) { pushEvent('warn', 'vnc-data 帧缺 uid，已丢弃', { bytes: buf.length }); return; }
          // 2026-10-24：远程控制 = 直接操纵教室机器（最高危），加重认证门。
          if (!reauthCheck(ws, 'vnc-data')) return;
          if (!canAccess(identityFromAuth(ws.__who), getDeviceClassCode(uid))) {
            pushEvent('warn', 'vnc-data 被拒（无权限远控该设备）', { uid });
            sndErr(ws, 'NO_DEVICE_PERMISSION', `无权限远控设备 ${uid}`);
            return;
          }
          // 2026-10-26：被控端访问密码门控——设了密码的设备，未过鉴权的管理端一帧都不转发。
          if (getVncAccess(uid).enabled && !(ws.__vncAuthed && ws.__vncAuthed.has(uid))) {
            pushEvent('warn', 'vnc-data 被拒（未过访问密码鉴权）', { uid });
            sndErr(ws, 'VNC_AUTH_REQUIRED', `设备 ${uid} 已启用访问密码，请先鉴权`);
            return;
          }
          const r = vncDataToAgent(uid, buf);
          if (!r.ok) {
            pushEvent('warn', 'vnc-data 未送达设备（可能不在线）', { uid, bytes: buf.length });
            snd(ws, 'vnc-relayed', { uid, ok: false, to: 0, detail: r.detail || '设备不在线' });
          }
          return;
        }
      }
      // 2026-10-21 · 上行协议对称化：管理端改发带 mime 的 v1 帧，云端按 header.mime 路由，
      // 不再依赖 isAudioBroadcasting()/isScreenBroadcasting() 会话态推断（更健壮、解耦）。
      // 剥掉 v1 头后把裸 PCM/JPEG 交给 broadcast*ToAgents（它再 wrap 成带 mime 的帧下发被控端）。
      if (f && f.ok && f.header && f.header.mime) {
        // 2026-10-21（P1）：audio/opus 与 audio/pcm 同路 fan-out，**mime 原样透传** ——
        // 云端不做编解码，只按 header.mime 路由；端侧按 mime 决定解不解 Opus。
        // ⚠️ 仍必须过 isAudioBroadcasting() 闸门：带 mime 的帧是"新信封"，不该因此
        //   绕过「audio.start 才收、audio.stop 就停」的既有契约（否则老师停讲后
        //   还在途中的帧会继续广播给全教室）。
        if ((f.header.mime === 'audio/pcm' || f.header.mime === 'audio/opus')
            && isAudioBroadcasting()) {
          broadcastAudioToAgents(f.payload, f.header.mime, ++audioSeq);
          return;
        }
        if (f.header.mime === 'image/jpeg') {
          broadcastScreenToAgents(f.payload, ++audioSeq);
          return;
        }
      }
      // 向后兼容：旧管理端仍发裸帧（parseAnyFrame 回 header=null），靠会话态推断（legacy 兜底）
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
      // 2026-10-20（RBAC 第二批）：票没带 claims（站点现在签的 bare-uid 票）但云端
      // user-claims 存储登记了该 uid ⇒ 回填进 r.claims，下面的 identityFromAuth 才能
      // 拿到 roles/scopes。存储为空 ⇒ 不回填 ⇒ scopes=null ⇒ 全权（行为=今天，零回归）。
      applyStoredClaims(r);
      ws.__who = r;   // 2026-10-15：鉴权结果挂到连接上，后面每个分支都用它做权限判断（perms.js）
      // 2026-10-20（RBAC 第二批）：auth-ok 补发 identity —— viewer 端据此驱动前端权限镜像层
      // （scopes=null ⇒ 全权；scopes=[] ⇒ 显式零权限；scopes=[...] ⇒ 按 scope 裁剪）。
      const authedIdent = identityFromAuth(r);
      // 旧客户端认的是 auth-ok，v1 客户端收 authed（统一命名，旧名进兼容清单）
      snd(ws, ws.__v1 === true ? 'authed' : 'auth-ok', {
        mode: r.mode, uid: r.uid ?? null,
        identity: authedIdent ? Object.assign({}, authedIdent) : null,
      });
      onViewerAuthed(ws, r);
      return;
    }

    switch (p.type) {
      // WebRTC 信令（P1-A 实时画面）：云端只转发、不解析内容；对方不在线/没订阅都如实回执，不静默
      case 'rtc-offer':
      case 'rtc-answer':
      case 'rtc-ice': {
        const rawUid = String(p.payload.uid ?? '').trim();
        // 2026-10-16（兜底）：viewer 回 answer/ice 曾不带 uid → rtcRelayToAgent('') →「设备不在线」
        // → answer 到不了被控端 → 0 帧。viewer 侧已修（带 uid）；这里再做一层：没带时回退到
        // 本连接最后涉及的那台设备（首次带 uid 的信令会记下），防再犯。
        if (rawUid) ws.__rtcLastUid = rawUid;
        const uid = rawUid || ws.__rtcLastUid || '';
        // 管理端主动建 offer 时用（from='viewer'）；收到 from='cloud' 说明是订阅开关，不该走信令分支
        const r = rtcRelayToAgent(uid, p.type, p.payload);
        snd(ws, 'rtc-relayed', { uid, ok: r.ok, to: r.to, type: p.type, detail: r.detail || '' });
        return;
      }
      case 'subscribe': {
        // 2026-10-15（权限契约 §4.3）：订阅 = 看画面，同样走 can()——越权订阅直接拒。
        const ident = identityFromAuth(ws.__who);
        const tgtUid = p.payload.uid;
        const tgtClass = getDeviceClassCode(tgtUid);   // 离线设备也在 knownDevices 里有归属
        if (!canAccess(ident, tgtClass)) {
          sndErr(ws, 'NO_DEVICE_PERMISSION', `无权限订阅设备 ${tgtUid}`);
          return;
        }
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
      // 2026-10-26：被控端访问密码鉴权。管理端在远端接管前先发这条带上设备密码；
      // 云端校验通过后，本连接 ws.__vncAuthed 记下该 uid，之后的 vnc-data 双向中继与
      // remote_control_start 指令才放行。未设密码的设备不用这步（中继按 enabled 跳过）。
      case 'vnc-auth': {
        const uid = String(p.payload?.uid || '').trim();
        const password = String(p.payload?.password || '');
        if (!uid) { sndErr(ws, 'BAD_PAYLOAD', 'vnc-auth 缺 uid'); return; }
        const ident = identityFromAuth(ws.__who);
        if (!canAccess(ident, getDeviceClassCode(uid))) {
          sndErr(ws, 'NO_DEVICE_PERMISSION', `无权限访问设备 ${uid}`);
          return;
        }
        if (!verifyVncPassword(uid, password)) {
          pushEvent('warn', '被控端访问密码核验失败', { uid, who: ws.__who?.uid ?? ws.__who?.mode ?? '?' });
          snd(ws, 'vnc-auth-fail', { uid, reason: '密码错误' });
          return;
        }
        if (!ws.__vncAuthed) ws.__vncAuthed = new Set();
        ws.__vncAuthed.add(uid);
        snd(ws, 'vnc-auth-ok', { uid });
        pushEvent('info', '被控端访问密码核验通过', { uid, who: ws.__who?.uid ?? ws.__who?.mode ?? '?' });
        return;
      }
      // 班级域（2026-10-23 · #458）：按班订阅/退订。权限判断（canAccess）在
      // class-events 模块内做，与 subscribe/instruction 同一口径（契约 §4.3）。
      // 2026-10-24：订阅班级=拉班级设备清单（敏感），加重认证门。
      case 'subscribe_class':
      case 'unsubscribe_class':
        if (!reauthCheck(ws, p.type)) return;
        handleViewerClassMsg(ws, p, { ident: identityFromAuth(ws.__who), snd, sndErr });
        return;
      case 'devices': {
        // 2026-10-15（权限契约 §4.3）：设备列表按本连接 scopes 过滤，不把全班/全校吐给没权限的人。
        const ident = identityFromAuth(ws.__who);
        const visible = filterDevices(ident, listDevices());
        // 2026-10-20（RBAC 第二批）：字段脱敏（同上，防御性守卫）。
        snd(ws, 'devices', { devices: maskDevices(ident, visible), at: new Date().toISOString() });
        return;
      }
      // 语音对讲（2026-10-07，设计文档 3.3）：老师开始/停止讲话。
      // 开始后云端接受该管理端的二进制音频帧并 fan-out；停止后拒绝新帧。
      // 第一版目标 = 全部在线设备（班级映射在站点侧）。
      // 2026-10-09：params.duplex=true → 语音通话（双向）——记录发起者 ws，
      //   学生上行音频单播回它（sendAudioToTeacher），不 fan-out 全教室。
      case 'audio.start': {
        // 2026-10-24：语音对讲=影响全部在线设备（敏感），加重认证门。
        if (!reauthCheck(ws, p.type)) return;
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
        // 2026-10-24：屏幕广播=影响全部在线设备（敏感），加重认证门。
        if (!reauthCheck(ws, p.type)) return;
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
        // 2026-10-24：下发指令（含关机/重启/开终端）= 最高危动作，加重认证门。
        if (!reauthCheck(ws, `instruction:${action}`)) return;
        // 2026-10-15（权限契约 §4.3 + 不变量③）：**下发指令前必须过 can()**——
        // 目标设备不在本连接 scopes 内（或设备无班级归属）直接拒，绝不下发。
        // 这是本轮「云端全权无阻止」的核心补位：此前任何持票者能对任何设备做任何动作。
        const ident = identityFromAuth(ws.__who);
        const tgtClass = getDeviceClassCode(uid);   // 离线设备也在 knownDevices 里有归属
        if (!canAccess(ident, tgtClass)) {
          pushEvent('warn', '管理端越权：指令被云端拒绝（目标不在其 scopes 内）',
            { uid, action: String(action).slice(0, 48), who: ws.__who?.uid ?? ws.__who?.mode ?? '?' });
          sndErr(ws, 'NO_DEVICE_PERMISSION', `无权限向设备 ${uid} 下发指令`);
          return;
        }
        // 2026-10-26：被控端访问密码门控——远端接管需先过密码鉴权（设了密码的设备）。
        // 落在 instruction 通道：remote_control_start 未过鉴权时直接拒发，让管理端弹密码框。
        if (action === 'remote_control_start' && getVncAccess(uid).enabled
            && !(ws.__vncAuthed && ws.__vncAuthed.has(String(uid)))) {
          pushEvent('warn', '远控被访问密码拦截（未过鉴权）', { uid, who: ws.__who?.uid ?? ws.__who?.mode ?? '?' });
          sndErr(ws, 'VNC_AUTH_REQUIRED', `设备 ${uid} 已启用访问密码，请先在管理端输入密码`);
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
      // ── 互动审批（2026-10-15 · 灵动岛扩展组件6）──
      // 点歌/祝福待审队列 + 审批动作 + 反馈汇总。权限口径与 subscribe/instruction 一致
      // （契约 §4.3：云端任何「返回内容 / 触发设备动作」的路径都必须过 can()）：
      // 列表按本连接 scopes 过滤，审批动作先验目标条目的班级归属。
      case 'song-requests.list': {
        const ident = identityFromAuth(ws.__who);
        const items = listSongRequests({ state: p.payload?.state, limit: p.payload?.limit })
          .filter((it) => canAccess(ident, it.classCode || null));
        snd(ws, 'song-requests.list', { ok: true, items });
        return;
      }
      case 'blessings.list': {
        const ident = identityFromAuth(ws.__who);
        const items = listBlessings({ state: p.payload?.state, limit: p.payload?.limit })
          .filter((it) => canAccess(ident, it.classCode || null));
        snd(ws, 'blessings.list', { ok: true, items });
        return;
      }
      case 'feedback.summary': {
        const ident = identityFromAuth(ws.__who);
        const s = feedbackSummary();
        // byClass 同样按 scope 过滤（班级计数也是数据，不漏给没权限的连接）
        if (ident && ident.scopes !== null && !isPlatformIdent(ident)) {
          s.byClass = s.byClass.filter((c) => canAccess(ident, c.classCode || null));
        }
        snd(ws, 'feedback.summary', s);
        return;
      }
      case 'song-request.handle': {
        const id = Number(p.payload?.id);
        const action = String(p.payload?.action || '');
        const ident = identityFromAuth(ws.__who);
        // 先按 id 探一次班级归属做权限判定（handle* 内部还会再校验状态机）
        const probe = listSongRequests({ limit: 500 }).find((it) => it.id === id);
        if (!probe) { snd(ws, 'song-request-handled', { ok: false, id, code: 'NOT_FOUND', message: `点歌 #${id} 不存在` }); return; }
        if (!canAccess(ident, probe.classCode || null)) {
          pushEvent('warn', '管理端越权：点歌审批被云端拒绝（目标不在其 scopes 内）', { id, who: ws.__who?.uid ?? ws.__who?.mode ?? '?' });
          sndErr(ws, 'NO_DEVICE_PERMISSION', `无权限处理点歌 #${id}`);
          return;
        }
        const r = handleSongRequest(id, action, ws.__who?.uid || ws.__who?.mode || 'viewer');
        if (!r.ok) { snd(ws, 'song-request-handled', { ok: false, id, code: r.code, message: r.message }); return; }
        pushEvent('info', `点歌 #${id} ${action}：${r.item.title}`, { uid: r.item.uid, by: r.item.handledBy });
        broadcastToViewers('song-request-handled', { ok: true, item: r.item });
        return;
      }
      case 'blessing.handle': {
        const id = Number(p.payload?.id);
        const action = String(p.payload?.action || '');
        const ident = identityFromAuth(ws.__who);
        const probe = listBlessings({ limit: 500 }).find((it) => it.id === id);
        if (!probe) { snd(ws, 'blessing-handled', { ok: false, id, code: 'NOT_FOUND', message: `祝福 #${id} 不存在` }); return; }
        if (!canAccess(ident, probe.classCode || null)) {
          pushEvent('warn', '管理端越权：祝福审批被云端拒绝（目标不在其 scopes 内）', { id, who: ws.__who?.uid ?? ws.__who?.mode ?? '?' });
          sndErr(ws, 'NO_DEVICE_PERMISSION', `无权限处理祝福 #${id}`);
          return;
        }
        const r = handleBlessing(id, action, ws.__who?.uid || ws.__who?.mode || 'viewer', p.payload?.reason);
        if (!r.ok) { snd(ws, 'blessing-handled', { ok: false, id, code: r.code, message: r.message }); return; }
        pushEvent('info', `祝福 #${id} ${action}`, { uid: r.item.uid, by: r.item.handledBy });
        broadcastToViewers('blessing-handled', { ok: true, item: r.item });
        // approve → 向该班在线设备广播弹幕（走组件5 的可信通知路径，见 broadcastBlessingDanmaku）
        if (action === 'approve') broadcastBlessingDanmaku(r.item);
        return;
      }
      // ── UDP 闪传通道（2026-10-20 · U1）：协商会话密钥 + 中继地址 ──
      // 权限与「订阅画面/远控」同一口径（看得到这台才能跟它建快速通道）。
      // 密钥只走这条已鉴权 WS（服务端随机 32B），UDP 帧道上不传任何长期秘密。
      case 'udp-session': {
        const action = p.payload?.action;
        if (action === 'open') {
          const uid = String(p.payload?.uid || '').trim();
          if (!uid) { sndErr(ws, 'BAD_PARAMS', 'udp-session.open 缺 uid'); return; }
          if (!canAccess(identityFromAuth(ws.__who), getDeviceClassCode(uid))) {
            sndErr(ws, 'NO_DEVICE_PERMISSION', `无权限与设备 ${uid} 建 UDP 会话`);
            pushEvent('warn', 'udp-session 被拒（无权限远控该设备）', { uid });
            return;
          }
          if (!udpRelay.configured) {
            sndErr(ws, 'UDP_DISABLED', 'UDP 闪传通道未配置（UDP_RELAY_TOKEN 为空），请走 WS 通道');
            return;
          }
          if (!udpBound) {
            sndErr(ws, 'UDP_NOT_READY', 'UDP 中继尚未就绪，请稍候重试');
            return;
          }
          const dev = getDevice(uid);
          if (!dev || !dev.ws || dev.ws.readyState !== 1) {
            sndErr(ws, 'DEVICE_OFFLINE', `设备 ${uid} 不在线`);
            return;
          }
          const { sessionId, key } = udpRelay.createSession({
            uid, viewerWs: ws,
            sendToAgent: (m) => sendToAgentMsg(uid, 'udp-session', m),
            sendToViewer: (m) => snd(ws, 'udp-session', m),
          });
          const udp = { host: udpBound.host, port: udpBound.port };
          snd(ws, 'udp-session', { action: 'opened', sessionId, key: key.toString('base64'), role: 'viewer', udp });
          sendToAgentMsg(uid, 'udp-session',
            { action: 'opened', sessionId, key: key.toString('base64'), role: 'agent', udp });
          pushEvent('info', `UDP 会话 #${sessionId} 已协商（管理端 → ${uid}）`, { uid, sessionId });
          return;
        }
        if (action === 'close') {
          const sid = Number(p.payload?.sessionId);
          if (Number.isFinite(sid)) udpRelay.closeSession(sid, 'viewer-close');
          snd(ws, 'udp-session', { action: 'closed', sessionId: sid, reason: 'viewer-close' });
          return;
        }
        sndErr(ws, 'BAD_PARAMS', `未知 udp-session action: ${action}`);
        return;
      }
      // ── TCP 闪传通道（2026-10-20 · 公网主路）：与 udp-session 同一套协商，换 TCP 管道 ──
      // 权限口径一致（看得到这台才能跟它建快速通道）；密钥同样只走已鉴权 WS。
      case 'tcp-session': {
        const action = p.payload?.action;
        if (action === 'open') {
          const uid = String(p.payload?.uid || '').trim();
          if (!uid) { sndErr(ws, 'BAD_PARAMS', 'tcp-session.open 缺 uid'); return; }
          if (!canAccess(identityFromAuth(ws.__who), getDeviceClassCode(uid))) {
            sndErr(ws, 'NO_DEVICE_PERMISSION', `无权限与设备 ${uid} 建 TCP 会话`);
            pushEvent('warn', 'tcp-session 被拒（无权限远控该设备）', { uid });
            return;
          }
          if (!tcpRelay.configured) {
            sndErr(ws, 'TCP_DISABLED', 'TCP 闪传通道未配置（TCP_RELAY_TOKEN 为空），请走 WS 通道');
            return;
          }
          if (!tcpBound) {
            sndErr(ws, 'TCP_NOT_READY', 'TCP 中继尚未就绪，请稍候重试');
            return;
          }
          const dev = getDevice(uid);
          if (!dev || !dev.ws || dev.ws.readyState !== 1) {
            sndErr(ws, 'DEVICE_OFFLINE', `设备 ${uid} 不在线`);
            return;
          }
          const { sessionId, key } = tcpRelay.createSession({
            uid, viewerWs: ws,
            sendToAgent: (m) => sendToAgentMsg(uid, 'tcp-session', m),
            sendToViewer: (m) => snd(ws, 'tcp-session', m),
          });
          const tcp = { host: tcpBound.host, port: tcpBound.port };
          snd(ws, 'tcp-session', { action: 'opened', sessionId, key: key.toString('base64'), role: 'viewer', tcp });
          sendToAgentMsg(uid, 'tcp-session',
            { action: 'opened', sessionId, key: key.toString('base64'), role: 'agent', tcp });
          pushEvent('info', `TCP 会话 #${sessionId} 已协商（管理端 → ${uid}）`, { uid, sessionId });
          return;
        }
        if (action === 'close') {
          const sid = Number(p.payload?.sessionId);
          if (Number.isFinite(sid)) tcpRelay.closeSession(sid, 'viewer-close');
          snd(ws, 'tcp-session', { action: 'closed', sessionId: sid, reason: 'viewer-close' });
          return;
        }
        sndErr(ws, 'BAD_PARAMS', `未知 tcp-session action: ${action}`);
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
    // 班级域（2026-10-23 · #458）：摘掉这个管理端的全部班级订阅（防漏内存 + 往死连接投递）
    removeViewerClasses(ws);
    // 管理端走了 ⇒ 它经手的所有闪传会话收口（防会话泄漏：远控泄漏那次的同一病）
    udpRelay.closeSessionsWhere((s) => s.viewerWs === ws);
    tcpRelay.closeSessionsWhere((s) => s.viewerWs === ws);
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
    if (stale.length > 0) {
      broadcastDevices();   // 立刻把"它没了"推给管理端，别等下一次轮询
      // 班级域（2026-10-23 · #458）：心跳超时的设备 → 所在班订阅者收 member.offline
      for (const u of stale) noteMemberOffline(u, 'heartbeat-timeout');
    }
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

// 互动数据清扫（2026-10-15 组件6）：已处理的点歌/祝福超 7 天删、反馈超 30 天删。
// pending 永不扫 —— 那是等管理端审批的学生内容，不是垃圾。
setInterval(() => {
  try {
    const r = sweepInteract();
    if (r.songPurged || r.danmakuPurged || r.feedbackPurged) pushEvent('info', '互动数据清扫完成', r);
  } catch (e) {
    pushEvent('error', '互动数据清扫异常', { error: e.message });
  }
}, INTERACT_SWEEP_INTERVAL_MS);

// CIMS 课表周期同步（2026-10-17）：CIMS_ENABLED=true 且 CIMS_PULL_INTERVAL_SEC>0 才启。
startCimsScheduler();

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
        if (!safeEqual(token, DEV_TOKEN)) {
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
        // 2026-10-25：设备用途（classroom / broadcast）。只在新登记时被采纳，
        // 之后以云端 knownDevices 为准 —— 归一逻辑在 registry.normalizeDeviceRole。
        role: (p.payload && typeof p.payload.role === 'string') ? p.payload.role : null,
      };
      markConnected(uid, String(version || ''), ws, regCaps, regMeta);
      // 班级域（2026-10-23 · #458）：该设备所在班的订阅者收到 member.online
      // （班级归属取云端 knownDevices 表，设备没绑班时模块内静默跳过）
      noteMemberOnline(uid);
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
      // 2026-10-24 修复：回执补带 classCode/className（取 knownDevices 表，含离线期
      // 绑班的重连设备）—— 否则「离线期被绑班」的设备 register 自报空码、云端也不回
      // 带，端侧 classcode.txt 永不落盘 ⇒ 缩略图上报 readyCheck 门（!g_classCode.isEmpty()）
      // 永不开 ⇒ 班级工作台设备网格「暂无画面」。端侧收到后 saveClassCode 落盘。
      // ⚠️ 只能走 registry 导出的 getDeviceClassCode / getDevice —— knownDevices 是
      //     registry 模块私有 Map，index.js 直接引用会 ReferenceError（实测踩过）。
      const cac = getDeviceClassCode(uid);
      const cknownd = getDevice(uid);
      snd(ws, ws.__v1 === true ? 'registered' : 'registration-ok', {
        uid,
        server: 'stelarith-cloud-ws/0.4.0',
        heartbeatMs: HEARTBEAT_INTERVAL_MS,
        timeoutMs: HEARTBEAT_TIMEOUT_MS,
        classCode: cac,
        className: (cknownd && cknownd.name) || null,
        // 2026-10-25：用途回执 —— 与 classCode 同款「云端权威、端侧落盘」，
        // 后台把一台教室机改成广播机后，设备重连即切外壳（不需要单独推送通道）。
        role: getDeviceRole(uid),
        latestVersion: agentOta ? agentOta.version : null,
        updateUrl: agentOta ? agentOta.url : null,
        updateSha256: agentOta ? agentOta.sha256 : null,
        updateNotes: agentOta ? agentOta.notes : null,
        updateMandatory: agentOta ? agentOta.mandatory : null,
      });
      // 重连补发：这台机器掉线期间"已下发未回执"的指令，按原顺序重发给它（内部无待补发则不动作）。
      replayPending(ws, uid);
      // CIMS 课表上线同步（2026-10-17）：后台拉取，按配置决定是否立即下发。
      // ⚠️ 不 await：注册路径上多一次 CIMS 往返会把握手拖慢；模块内部自带异常兜底。
      cimsOnDeviceOnline(uid);
      return;
    }

    if (isBinary) {
      const f = parseAnyFrame(Buffer.from(data));
      if (!f.ok) { sndErr(ws, f.code, f.detail); return; }
      // ── 远控统一 A：设备 vnc-data 帧 → 订阅该 uid 的管理端主 WS（原样信封）──
      if (f.header?.type === 'vnc-data') {
        const hUid = String(f.header.uid ?? '').trim();
        // 原样转发整个信封（含 header 的 uid/direction/seq），云端不重写。
        const r2 = vncDataToViewer(hUid || uid, Buffer.from(data));
        if (r2.to === 0) {
          pushEvent('warn', 'vnc-data 未送达任何管理端（无人订阅该设备）',
            { uid: hUid || uid, bytes: data.length });
        }
        return;
      }
      const seq = f.header?.seq ?? 0;
      // ── 语音通话上行（2026-10-09）：学生 mic 音频帧 → 单播回发起老师 ──
      // 帧头 mime=audio/* 时走 duplex 通道，不走画面帧的 broadcastFrame。
      // 2026-10-21（P1）：audio/opus 与 audio/pcm 同路，mime 原样带给管理端（云端不解码）。
      if (f.header?.mime === 'audio/pcm' || f.header?.mime === 'audio/opus') {
        const r = sendAudioToTeacher(f.payload, seq, uid, f.header.mime);
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
      noteFrameRelay(uid, r.hits, r.bytes);
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

    // ── 互动上报（2026-10-15 · 灵动岛扩展组件6）──
    // 点歌 / 反馈点赞 / 祝福弹幕：学生不可信输入，云端落库（interact.js）+ 限额闸门，
    // 再广播给管理端（待审队列实时可见）。班级归属以云端 knownDevices 表为准
    // （设备自报的 classCode 只是兜底 —— 自报值不可信）。
    // ⚠️ 与噪音不同，这三类**要落库**：它们是等审批的内容，不是实时遥测。
    // ⚠️ 不走事件流水（一节课几十条会把运维事件挤掉）——审计走 interact 库本身。
    if (p.type === 'song_request' || p.type === 'feedback' || p.type === 'blessing') {
      const classCode = getDeviceClassCode(uid) || String(p.payload?.classCode || '').slice(0, 32);
      const base = { uid, classCode };
      let r;
      if (p.type === 'song_request') {
        r = submitSongRequest({ ...base, title: p.payload?.title, artist: p.payload?.artist, source: p.payload?.source });
      } else if (p.type === 'feedback') {
        r = submitFeedback({ ...base, kind: p.payload?.kind, value: p.payload?.value });
      } else {
        r = submitBlessing({ ...base, text: p.payload?.text });
      }
      if (r.ok) {
        const ackType = p.type === 'song_request' ? 'song-request-ack'
          : p.type === 'feedback' ? 'feedback-ack' : 'blessing-ack';
        snd(ws, ackType, { ok: true, id: r.id, state: r.state || 'pending' });
        // 待审内容实时推给管理端（新请求进审批面板，不用轮询）
        const newType = p.type === 'song_request' ? 'song-request-new'
          : p.type === 'feedback' ? 'feedback-new' : 'blessing-new';
        broadcastToViewers(newType, { uid, classCode, id: r.id });
      } else {
        // 任何拒绝都如实回错（纪律：不许静默丢弃）——端侧看到 ok:false 只记日志，
        // 本地体验已是第一公民（端侧先行），云端拒收不影响学生已完成的本地操作。
        snd(ws, `${p.type}-ack`, { ok: false, code: r.code, message: r.message });
      }
      return;
    }

    // ── 班级域事件上报（2026-10-23 · #458）──
    // 设备往自己所属班推一条事件（成员动态、将来的缩略图 kind='thumbnail' 走这条）。
    // 班级归属只认云端表（class-events 内 getDeviceClassCode），载荷里自报的
    // classCode 一律忽略——设备自报是不可信输入（interact.js 同款教训）。
    if (p.type === 'class-event') {
      handleAgentClassMsg(ws, p, uid, snd);
      return;
    }

    if (p.type === 'receipt') {
      // 被控端真执行完了，回的是 done/failed（不是 received）。必须如实记、如实转。
      const { id, action, result, error, data } = p.payload;
      const rec = recordResult(id, uid, action, result, error, data);
      // 2026-10-27（WS 审计）：回执归属不符 → recordResult 拒收返回 null。
      // 明说给设备（不许静默吞），不记流水、不转发、不动补发队列。
      if (!rec) {
        sndErr(ws, 'BAD_RECEIPT', `回执与下发设备不符，已拒绝（id=${id}）`);
        return;
      }
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

    if (p.type === 'udp-session') {
      // 被控端对 UDP 闪传会话的回应（2026-10-20 · U1）：目前只支持主动关闭。
      // （「已 HELLO / 链路就绪」由中继从 UDP 帧侧直接感知，不必再走 WS 回一趟。）
      const action = p.payload?.action;
      if (action === 'close') {
        const sid = Number(p.payload?.sessionId);
        if (Number.isFinite(sid)) udpRelay.closeSession(sid, 'agent-close');
        return;
      }
      sndErr(ws, 'BAD_PARAMS', `未知 udp-session action: ${action}`);
      return;
    }

    if (p.type === 'tcp-session') {
      // 被控端对 TCP 闪传会话的回应（2026-10-20）：只支持主动关闭；链路就绪由中继侧感知。
      const action = p.payload?.action;
      if (action === 'close') {
        const sid = Number(p.payload?.sessionId);
        if (Number.isFinite(sid)) tcpRelay.closeSession(sid, 'agent-close');
        return;
      }
      sndErr(ws, 'BAD_PARAMS', `未知 tcp-session action: ${action}`);
      return;
    }

    // 未知类型**不关闭连接**：允许新客户端试探新能力（协议规范第十节）
    sndErr(ws, 'UNKNOWN_TYPE', p.type);
  });

  ws.on('close', () => {
    finish();
    if (registered) {
      // markDisconnected 回 uid（这台连接对应的设备）；班级域据此发 member.offline
      const off = markDisconnected(ws);
      if (off) noteMemberOffline(off, 'disconnect');
    }
    // 设备走了 ⇒ 它参与的闪传会话全部收口（防中继侧会话/连接泄漏）
    if (uid) {
      udpRelay.closeSessionsWhere((s) => s.uid === uid);
      tcpRelay.closeSessionsWhere((s) => s.uid === uid);
    }
  });

  ws.on('error', (e) => {
    pushEvent('error', 'WS 连接出错', { uid: uid || '(未知)', error: e.message });
  });
});

server.listen(PORT, HOST, () => {
  pushEvent('info', '云端已启动，等被控端接入', { host: HOST, port: PORT });
  // 2026-10-20 · UDP 闪传中继：令牌配了才拉 socket（没配 = 通道关闭，只告警）。
  // 失败不阻塞 WS 主通道——闪传是加速项，不是生死项。
  if (udpRelay.configured) {
    udpRelay.start()
      .then((b) => { udpBound = b; })
      .catch((e) => pushEvent('error', 'UDP 中继启动失败（不影响 WS 通道）', { message: String(e?.message || e) }));
  } else {
    pushEvent('warn', 'UDP_RELAY_TOKEN 未配置：UDP 闪传通道不启动（WS 通道不受影响）');
  }
  // 2026-10-20 · TCP 闪传中继（公网主路）：同样 fail-closed、同样不阻塞主通道。
  if (tcpRelay.configured) {
    tcpRelay.start()
      .then((b) => { tcpBound = b; })
      .catch((e) => pushEvent('error', 'TCP 中继启动失败（不影响 WS 通道）', { message: String(e?.message || e) }));
  } else {
    pushEvent('warn', 'TCP_RELAY_TOKEN 未配置：TCP 闪传通道不启动（WS 通道不受影响）');
  }
  // 2026-10-11：启动时预打包 QML 补丁（手机热更用；失败不阻塞启动）
  try { const m = ensureQmlPatch(); if (m) console.log(`[cloud] qml-patch 就绪 v${m.version}（${m.files} 文件）`); }
  catch (e) { console.error(`[cloud] WARN qml-patch 预打包失败: ${e.message}`); }
  // 回跳服务要等 listen 完成才知道真端口（listen(0) 是让系统分配），所以在回调里建。
  const srv = createAdminOAuthServer((m) => console.log(m));
  if (srv) adminOAuth = { server: srv };
  // 2026-10-18 · VoiceHub「正在播放」桥启动（只读）。
  // 推给**所有在线被控端**：岛据此显示 talking/broadcast 那条任务。
  // ⚠️ 失败不阻塞启动：VoiceHub 挂了不该把整个云端带下去。
  try {
    voiceHubBridge = createVoiceHubBridge({
      log: (m) => console.log(m),
      onUpdate: (state) => {
        // 只在"有内容或无内容"发生变化时才下发，避免每 30s 空推一次把设备刷一遍。
        const sig = state.status === 'playing'
          ? `playing:${state.broadcast?.songId ?? ''}:${state.broadcast?.isPlaying ? 1 : 0}`
          : state.status;
        if (sig === voiceHubLastSig) return;
        voiceHubLastSig = sig;
        const payload = {
          status: state.status,                    // playing | idle | stale | unknown
          broadcast: state.broadcast,              // null 或 {title,artist,position,duration,progress,nextUp,...}
          serverTime: Date.now(),
        };
        for (const d of listDevices()) {
          if (!d || !d.ws) continue;
          // 用 wrapOutgoing 自动按对端版本选格式（与 snd 同口径），失败只吞单台。
          try { d.ws.send(wrapOutgoing(d.ws.__v1 === true, 'voicehub_nowplaying', payload)); } catch { /* 单台失败不影响其他 */ }
        }
        if (state.status === 'playing') {
          pushEvent('info', 'VoiceHub 广播状态已更新', {
            title: state.broadcast?.title || '', artist: state.broadcast?.artist || '',
          });
        }
      },
    });
    // ⚠️ start() 返回布尔（是否真的启动了），**不是** bridge 本身 ——
    //    曾经写成 createVoiceHubBridge({...}).start() 赋给 voiceHubBridge ⇒ 存进去的是 true，
    //    于是 /api/public/voicehub 调 diagnostics() 时 TypeError（实测抓到并修）。
    voiceHubBridge.start();
  } catch (e) {
    console.error(`[cloud] WARN VoiceHub 桥启动失败（不影响其他功能）: ${e.message}`);
  }
});

server.on('error', (e) => {
  console.error(`[cloud] FAIL 端口占用或启动失败: ${e.message}`);
  process.exit(1);
});
