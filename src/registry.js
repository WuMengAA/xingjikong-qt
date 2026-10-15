// 设备在线表 + 指令队列 + 事件流水。
// 纪律（从 Rust/NSIS 那几步继承下来的三条）：
//   1. 失败必须报出来，不许静默；
//   2. 不许"报了成功其实没做"——设备不在线就是不在线，不许返回 sent；
//   3. 状态只有两种真值：online / offline，中间没有"大概在线"。

import fs from 'node:fs';
import path from 'node:path';
import { appendFile } from 'node:fs/promises';
import { EV_LIMIT, KEEP_LAST_FRAME, EVENTS_FILE, HEARTBEAT_TIMEOUT_MS,
  EVENTS_MAX_BYTES, EVENTS_KEEP_FILES } from './config.js';
import { wrapOutgoing, makeFrame } from './protocol.js';
import { enqueueDispatch, dropDispatch, settle as settleQueue, pendingFor, listPendingAll, paramsOf } from './store.js';

/** 出向统一走协议层：对端是 v1 就发信封，是旧客户端就发旧扁平格式（过渡期不断链）。 */
function sendTo(ws, type, payload, id = '') {
  return ws.send(wrapOutgoing(ws.__v1 === true, type, payload, id));
}

/** uid -> { uid, version, ws, connectedAt, lastSeen, framesIn, bytesIn, lastError, recentFrame } */
const devices = new Map();

/**
 * 持久「已知设备」表：无论在线离线都留着。
 * 根因修复（2026-10-08）：原先 devices 只在在线时存在，markDisconnected / sweepStale 直接 delete，
 * 离线设备从列表里彻底消失 —— 管理端「离线状态不显示」就是这么来的。
 * 现在 online 由 `devices.has(uid)` 派生，name / classCode / version 落在这张表里，重连沿用、离线保留。
 */
const knownDevices = new Map();

/* 上限护栏（2026-10-13）：uid 由设备自报，而设备令牌是全班/全校共用的 ⇒ 不设上限时
 * 可被无限灌 uid 把这张表撑爆内存（一条 register 加一条，永不释放）。
 * 照 results 表的先例：超限淘汰最久未见的（重连会重新注册，丢的只是展示元数据）。 */
const KNOWN_KEEP = Number(process.env.CLOUD_KNOWN_KEEP || 20000);
function pruneKnown() {
  if (knownDevices.size <= KNOWN_KEEP) return;
  // ⚠️ 不能只删"插入序最前"的：Map 对已有 key 再 set **不改插入位置**，重连不刷新顺序，
  //    于是插入序 ≠ 活跃序，按插入序淘汰会误删天天在线的设备。必须显式按 lastSeen 排。
  //    一次多淘汰 10% 留余量，免得每来一台新设备就全表扫一遍。
  const overflow = knownDevices.size - KNOWN_KEEP;
  const drop = Math.max(overflow, Math.ceil(KNOWN_KEEP * 0.1));
  const byOldest = [...knownDevices.entries()]
    .sort((a, b) => (a[1].lastSeen || 0) - (b[1].lastSeen || 0));
  let n = 0;
  for (const [k] of byOldest) {
    if (n >= drop) break;
    // 🔴 在线设备一律不淘汰：knownDevices.lastSeen **只在连接时写**（心跳只更新 devices 表），
    //    于是连续在线十天的机器 lastSeen 反而是全表最旧 —— 不跳过就会把天天在用的设备删掉，
    //    还会让 offlineDevices(= knownDevices.size - online) 变成负数。
    if (devices.has(k)) continue;
    knownDevices.delete(k);
    n++;
  }
  pushEvent('warn', '已知设备表超上限，淘汰最久未见记录', { keep: KNOWN_KEEP, dropped: n });
}

/**
 * 分班制：classCode -> { code, name, devices:[uid] }。
 * 设备上线 / 绑定 / 解绑时维护；列表接口与「按班定向下发」都读它。
 * ⚠️ 这里只存「设备→班」的归属，不存设备自身的在线/帧数据（那些在一处真源 devices 里）。
 */
const classes = new Map();

/** 上次记进日志的"观看结论"（uid + 被看人数），用来决定要不要再记一行 —— 见 pushWatchToAgent 末尾。 */
let lastWatchLogUid = null;
let lastWatchLogN = -1;

/* 说明：指令队列已改为**落盘**（见 store.js）。
 * 原先这里有个内存态 `pending` Map，但云端一重启就没了、设备重连也不回放——那正是本功能要修的洞。
 * 现在"已下发未回执"的指令一律进 SQLite，重启不丢、设备重连按原顺序补发。 */

/**
 * viewer ws -> { subs:Set<uid>, live:Set<uid>, auto:boolean }
 *   live 里的 uid = 这个管理端要实时流（远控中）；只有它们才会让设备端 rtc-start。
 *   subs = 这个管理端在看哪几台
 *   auto = true 表示「还没让管理端显式挑过」，设备上线时自动补订阅
 * 为什么要有 auto：管理端常常比教室机先开机（老师先打开管理软件，教室机后启动），
 * 只在「连上时就订阅第一台」会漏掉这种情况 → 管理端永远黑屏。
 * 只推订阅的，不群发给所有人。
 */
const viewers = new Map();

const events = [];
let seq = 0;

/* 已落盘字节数 —— **必须自己记，不能每次 statSync 现读**。
 * appendFile 是异步的：广播那种一瞬间连着来几十条事件的场景里，同步循环跑完时
 * 磁盘上的文件还几乎是空的，statSync 每次都读到旧大小，轮转判断永远不成立
 * （2026-10-06 实测：钉 4096 字节上限、连推 301 条，文件长到 38KB 一次都没转）。
 * 自己累加不受异步滞后影响。 */
let eventsBytes = 0;

/** id -> { id, uid, action, result:'done'|'failed', error, at }  设备真实执行回执 */
const results = new Map();
/* 回执只保留一个滚动窗口（2026-10-06 占用优化）。
 * 这张表以前只 set 不删：每条指令回执永久留在内存里，教室机一天下发几千条就是
 * 几千个永不释放的对象，跑一学期内存只涨不落。
 * 现在超限先删最老的（Map 保持插入序，第一个 key 就是最老的那条）。
 * 不会有"刚到的回执被挤掉"的问题：窗口 5000 条，比一个教学日下发量还宽，
 * 真要用早先从这台机器上查过（真查走 SQLite 的 /api/instructions/notice）。 */
const RESULT_KEEP = Number(process.env.CLOUD_RESULT_KEEP || 5000);
function pruneResults() {
  if (results.size <= RESULT_KEEP) return;
  const drop = results.size - RESULT_KEEP;
  let i = 0;
  for (const k of results.keys()) {
    if (i++ >= drop) break;
    results.delete(k);
  }
}

/**
 * 事件日志体积到顶就归档：当前 events.log → events.log.<ISO时间戳>，然后重建一个空的。
 * 归档按文件名倒序只留最近 EVENTS_KEEP_FILES 个，更老的删掉 —— 留痕还是留，
 * 但不能让一个日志文件陪着教室机一起把盘吃干。
 *
 * 只在体积确实到顶时才会动 rename，平时花的是一次 statSync（十来字节的 syscall）。
 * 失败必须报出来（本文件头第 1 条纪律），但**不许中断这一次 pushEvent**：
 * 归档出问题不该让设备状态更新跟着失败。
 */
function rotateEventsLogIfNeeded(sizeBytes) {
  if (!(sizeBytes >= EVENTS_MAX_BYTES) || EVENTS_MAX_BYTES <= 0) return;
  try {
    const stamp = new Date().toISOString().replace(/[-:.]/g, '-');
    const archived = `${EVENTS_FILE}.${stamp}`;
    fs.renameSync(EVENTS_FILE, archived);
    fs.writeFileSync(EVENTS_FILE, '');
    eventsBytes = 0;

    const dir = path.dirname(EVENTS_FILE);
    const base = path.basename(EVENTS_FILE);
    let olds = [];
    try {
      olds = fs.readdirSync(dir)
        .filter((f) => f.startsWith(base + '.') && f !== archived)
        .sort()
        .reverse()
        // 归档名是 ISO 时间戳，字典序 == 时间序，直接截断就行
        .slice(EVENTS_KEEP_FILES);
    } catch { olds = []; }
    for (const f of olds) {
      try { fs.unlinkSync(path.join(dir, f)); } catch { /* 删不掉也要把轮转本身报完 */ }
    }
    console.log(`[cloud] 事件日志已轮转：${sizeBytes} 字节 → ${archived}，删除旧归档 ${olds.length} 个`);
  } catch (e) {
    console.error(`[cloud] FAIL 事件日志轮转失败（本次落盘照常进行）：${e.message}`);
  }
}

/**
 * 事件落盘走一条 Promise 链，一次只写一条。
 *
 * 为什么串行：appendFile 本来是并行的，但轮转要 rename 当前文件 ——
 * 2026-10-06 实测，轮转那一瞬间还有几次异步写在飞，rename 之后它们写进了**归档文件**，
 * 结果当前 events.log 只剩 31 行，最近一大段事全跑到归档里去了，主日志看着像断过。
 * 串成一条链，检查与写之间不会插进别的写，轮转永远发生在两次写之间。
 * 顺带还压掉了"广播风暴时几百个 appendFile 一起飞"这件事。
 *
 * 前一条写失败不影响后一条（then 的第二参数就是 run），失败照样在链上往下走。
 */
let eventsChain = Promise.resolve();
function writeEventLine(line) {
  const run = async () => {
    rotateEventsLogIfNeeded(eventsBytes);
    eventsBytes += Buffer.byteLength(line);
    await appendFile(EVENTS_FILE, line);
  };
  eventsChain = eventsChain.then(run, run);
  eventsChain.catch(() => {});   // 失败由下面的 catch 报，别在链上堆 unhandled rejection
  return eventsChain;
}

export function pushEvent(level, msg, extra = {}) {
  const ev = { id: ++seq, at: new Date().toISOString(), level, msg, ...extra };
  events.push(ev);
  if (events.length > EV_LIMIT) events.splice(0, events.length - EV_LIMIT);
  // 事件流水落盘：被控端没日志等于瞎子，云端也一样。落盘失败要报出来。
  const logLine = JSON.stringify(ev) + '\n';
  writeEventLine(logLine).catch((e) => console.error(`[cloud] FAIL 事件落盘失败: ${e.message}`));
  const line = `[cloud] ${ev.level.toUpperCase()} ${ev.msg}${ev.uid ? ' uid=' + ev.uid : ''}`;
  if (ev.level === 'error') console.error(line);
  else console.log(line);
  return ev;
}

/**
 * @param caps 被控端 register 里的能力对象（含 actions 指令全集），可空（老版本被控端不带）。
 * @param meta 设备展示元数据 { name?, classCode?, className? }，来自 register 的辅助字段；
 *             不传则沿用已知设备表里存的旧值（重连不改名）。
 */
export function markConnected(uid, version, ws, caps = null, meta = null) {
  const old = devices.get(uid);
  if (old && old.ws && old.ws !== ws) {
    pushEvent('warn', '同 uid 重连，踢掉旧连接', { uid, reason: 'duplicate' });
    try { old.ws.close(4001, 'superseded'); } catch { /* 已断就算了，别静默吞掉别的 */ }
  }
  const now = Date.now();
  const isNewUid = !knownDevices.has(uid);   // 只有新 uid 才需要过上限检查（重连不必）
  const known = knownDevices.get(uid) || {};
  // 上限护栏：新设备才查（超限先淘汰最久未见的，避免被灌爆内存）。
  // 🔴 必须在 knownDevices.set(uid) **之前**：放在 set 之后有可能把刚写进去的这条
  //    自己淘汰掉（同毫秒批量注册时排序不稳），下面 `knownDevices.get(uid).name` 当场就崩
  //    —— 2026-10-13 验证脚本实测撞到过。
  if (isNewUid) pruneKnown();
  // 已知设备表：离线也留着，online 由 devices.has 派生；name/classCode 落这里，重连沿用。
  const resolvedClass = meta?.classCode ?? known.classCode ?? null;
  knownDevices.set(uid, {
    uid,
    name: meta?.name ?? known.name ?? null,
    classCode: resolvedClass,
    version: version || known.version || 'unknown',
    lastSeen: now,
  });
  // 上限护栏：新设备才查（超限先淘汰最久未见的，避免被灌爆内存）
  // 注册带班级码 → 同步进班级索引（保证「按班下发」能找到这台）。
  if (resolvedClass) addToClassIndex(uid, resolvedClass, meta?.className ?? '');
  devices.set(uid, {
    uid,
    version: version || 'unknown',
    ws,
    connectedAt: now,
    lastSeen: now,
    framesIn: 0,
    bytesIn: 0,
    heartbeatsIn: 0,
    lastError: null,
    name: knownDevices.get(uid)?.name ?? null,
    classCode: knownDevices.get(uid)?.classCode ?? null,
    recentFrame: old?.recentFrame ?? null,
    // 能力清单（2026-10-06 契合度）：被控端 register 带上来，云端**原样转给管端**，
    // 管端据此把不支持的按钮置灰，别让人点下去才吃一句"未知指令"。
    // 重连时新包没带就沿用旧的 —— 老版本被控端发不回来，总不能让管端一见旧包
    // 就以为这台机器"什么都不会"、把整排按钮全灰掉。
    // 能力清单（2026-10-06 契合度）：被控端 register 带上来，云端**原样转给管端**，
    // 管端据此把不支持的按钮置灰，别让人点下去才吃一句"未知指令"。
    // 重连时的合并口径 = **字段级沿用**，不是整包替换：
    //   · 老版本被控端压根不发 caps → 沿用旧的，不许让管端一见旧包就以为这台机器"什么都不会"
    //   · 部分上报（例如重连只带 screen）→ 没带的字段从旧值留着，不许把 input/power 抹掉
    //     抹掉的实际后果：管端读 caps.input === undefined 就算"不支持"，按钮整排变灰。
    //   · 两边都没有（老版本首台上线）→ null，管端照旧全放开（见 deviceSupports 的兜底）
    // ⚠️ 这里**不能再剥一层**：被控端 register 的 payload.caps 本身就是扁平的能力对象
    // （{screen,input,power,actions}，见 control-qt sendRegister）。之前写成 caps?.caps
    // 是照着"caps 里再包一个 caps"的臆想写的 —— 实测首台上线的设备 caps/actions 全是
    // null。2026-10-06 端到端探针抓到并修正。
    caps: (caps || old?.caps) ? { ...(old?.caps ?? {}), ...(caps ?? {}) } : null,
    actions: caps?.actions ?? old?.actions ?? null,
  });
  pushEvent('info', '设备已注册并连上', { uid, version, name: knownDevices.get(uid)?.name ?? null });
  // 刚上线：之前那些"订阅/退订"它一条都没收到过（那时它不在线，全记成 warn 了）。
  // 补发一次当前的"你在被看着"结论，否则新上线会停在"没人看"上一直不抓屏。
  // 没人看时这次推送只是白跑一回 send（uids 空数组），比让它永久空转强。
  pushWatchToAgent(String(uid || '').trim());
  // 上线 → 把最新设备表推给所有管理端（含它自己），离线状态立刻刷新（根因修复）。
  broadcastDevices();
}

export function markDisconnected(ws) {
  for (const [uid, d] of devices) {
    if (d.ws === ws) {
      // 离线：从在线表移除，但「已知设备表」留着（online 由 devices.has 派生会自然变 false）。
      const known = knownDevices.get(uid);
      if (known) known.lastSeen = Date.now();
      devices.delete(uid);
      pushEvent('info', '设备掉线', { uid, frames: d.framesIn });
      // 立刻把"它没了"推给管理端，离线状态马上刷新（根因修复）。
      broadcastDevices();
      return uid;
    }
  }
  return null;
}

export function addFrame(uid, bytes) {
  const d = devices.get(uid);
  if (!d) return null;
  d.framesIn++;
  d.bytesIn += bytes;
  d.lastSeen = Date.now();
  if (!KEEP_LAST_FRAME) d.recentFrame = null;
  return d;
}

export function setRecentFrame(uid, buf) {
  const d = devices.get(uid);
  if (d) d.recentFrame = buf;
}

/**
 * 被控端心跳：刷新 lastSeen 并计数。
 * 心跳必须与帧解耦——将来上「按需拉流」省带宽后，没人看的机器根本不推帧，
 * 那时帧就不能当存活依据了，必须有一条独立的存活信号。
 * @returns {boolean} false = 这个 uid 不在在线表里（异常）
 */
export function touchHeartbeat(uid) {
  const d = devices.get(uid);
  if (!d) return false;
  d.lastSeen = Date.now();
  d.heartbeatsIn++;
  return true;
}

/**
 * 清扫僵死设备：超过 HEARTBEAT_TIMEOUT_MS 既没心跳也没帧 → 判定离线并踢掉连接。
 * 不扫的后果：教室机掉电/断网但 TCP 没干净关闭时，面板会一直显示"在线"，
 * 老师点了指令才发现没反应——那比显示离线更糟（假绿）。
 * @returns {string[]} 本次被判定离线的 uid 列表
 */
export function sweepStale() {
  const now = Date.now();
  const stale = [];
  for (const [uid, d] of devices) {
    if (now - d.lastSeen <= HEARTBEAT_TIMEOUT_MS) continue;
    stale.push(uid);
    pushEvent('warn', '心跳超时，判定离线并踢掉', {
      uid,
      lastSeenAgoSec: Math.round((now - d.lastSeen) / 1000),
      frames: d.framesIn,
      heartbeats: d.heartbeatsIn,
    });
    try { if (d.ws) d.ws.close(4000, 'heartbeat timeout'); } catch { /* 已经断了，不必再报 */ }
    // 离线表保留（根因修复）：只从在线表移除，knownDevices 的 lastSeen 已是最新。
    devices.delete(uid);
  }
  return stale;
}

export function listDevices() {
  const now = Date.now();
  const out = [];
  // 合并「已知设备表」与「在线表」：在线设备一定有 known 条目；离线设备只剩 known 条目。
  for (const [uid, kd] of knownDevices) {
    const d = devices.get(uid);
    const online = !!d && d.ws && d.ws.readyState === 1;
    const lastSeen = d ? d.lastSeen : kd.lastSeen;
    out.push({
      uid,
      // 设备名：没上报过就是 null，管端照旧回退显示 uid（不是 undefined）。
      name: kd.name ?? null,
      classCode: kd.classCode ?? null,
      version: d ? d.version : (kd.version || 'unknown'),
      // online 由「在线表里有没有且 socket 还活着」派生 —— 不写死 true（根因修复）。
      online,
      connectedAt: d ? new Date(d.connectedAt).toISOString() : null,
      lastSeenAt: new Date(lastSeen).toISOString(),
      lastSeenAgoSec: Math.round((now - lastSeen) / 1000),
      framesIn: d ? d.framesIn : 0,
      bytesIn: d ? d.bytesIn : 0,
      heartbeatsIn: d ? d.heartbeatsIn : 0,
      lastError: d ? d.lastError : null,
      // 管端门控要用：没上报过就是 null（老版本被控端），管端照旧全放开
      caps: d ? (d.caps ?? null) : null,
      actions: d ? (d.actions ?? null) : null,
    });
  }
  return out;
}

export function getDevice(uid) {
  return devices.get(uid) || null;
}

/**
 * 按 uid 查班级码（**含离线设备**）。getDevice() 只看得见在线表，离线后 devices 表里
 * 没有该 uid ⇒ 权限判断（2026-10-15 perms.js）拿不到 classCode 会把离线设备误判为
 * 「无归属」而拒绝。knownDevices 是离线保底表，班级归属在这里沿用到重连。
 * @returns {string|null}
 */
export function getDeviceClassCode(uid) {
  return knownDevices.get(uid)?.classCode ?? null;
}

/**
 * 设置设备的展示元数据（名字 / 班级）。被控端 register 时带上来、或管理端手动改。
 * 落「已知设备表」，重连沿用；不影响在线状态。
 */
export function setDeviceMeta(uid, meta = {}) {
  const known = knownDevices.get(uid);
  if (!known) {
    knownDevices.set(uid, {
      uid,
      name: meta.name ?? null,
      classCode: meta.classCode ?? null,
      version: 'unknown',
      lastSeen: Date.now(),
    });
    return;
  }
  if (meta.name !== undefined) known.name = meta.name ?? null;
  if (meta.classCode !== undefined) {
    known.classCode = meta.classCode ?? null;
    if (meta.classCode) addToClassIndex(uid, meta.classCode);
  }
}

/* ---------- 分班制（2026-10-08）----------
 * 班级：classCode -> { code, name, devices:[uid] }。设备归属存「已知设备表」的 classCode，
 * 这里只维护「班 → 设备列表」索引 + 班名，便于按班下发与管理端列班级树。
 */

/** 校验班级码格式：6-16 位字母数字或中划线/下划线（站点 /api/device/bind 的 classCode 同源）。 */
export function isClassCodeValid(code) {
  return typeof code === 'string' && /^[A-Za-z0-9_-]{6,16}$/.test(code);
}

/** 内部：保证 classCode 索引里有这台设备（注册带 classCode / 手动绑定 / 改名都走它）。
 *  name（2026-10-09）：被控端 register 带 className 时用作班级显示名（默认 = code）。
 *  只有非空才覆盖 —— 否则后继一条不带名的 register 会把已显示的真名打回 class_01。 */
function addToClassIndex(uid, code, name = '') {
  if (!code) return;
  let c = classes.get(code);
  if (!c) { c = { code, name: name || code, devices: [] }; classes.set(code, c); }
  if (name) c.name = name;
  if (!c.devices.includes(uid)) c.devices.push(uid);
  let known = knownDevices.get(uid);
  if (!known) { known = { uid, name: null, classCode: code, version: 'unknown', lastSeen: 0 }; knownDevices.set(uid, known); }
  else known.classCode = code;
}

export function bindDeviceToClass(code, uid, className = '') {
  if (!isClassCodeValid(code)) return { ok: false, error: '班级码格式不合法（需 6-16 位字母数字或 -_）' };
  const key = code;
  let c = classes.get(key);
  if (!c) { c = { code: key, name: className || key, devices: [] }; classes.set(key, c); }
  if (className) c.name = className;
  addToClassIndex(uid, key);
  pushEvent('info', '设备已绑定班级', { uid, classCode: key, className: c.name });
  broadcastDevices();
  return { ok: true, class: c };
}

export function unbindDeviceFromClass(code, uid) {
  const c = classes.get(code);
  if (c) c.devices = c.devices.filter((x) => x !== uid);
  const known = knownDevices.get(uid);
  if (known && known.classCode === code) known.classCode = null;
  broadcastDevices();
  return { ok: true };
}

export function listClasses() {
  return [...classes.values()].map((c) => ({
    code: c.code,
    name: c.name,
    devices: c.devices.slice(),
    onlineCount: c.devices.filter((u) => {
      const d = devices.get(u);
      return !!d && d.ws && d.ws.readyState === 1;
    }).length,
  }));
}

export function getDevicesOfClass(code) {
  const c = classes.get(code);
  return c ? c.devices.slice() : [];
}

/**
 * 云端存储/运行信息：管理端「班级电脑 / 云端存储信息」面板用。
 * 只聚合内存态与事件日志体积，**不读 assets/pkg 大目录**（那是发布物，不是运行态）；
 * 在线/离线/待回执指令都现算，不落表（落表会长出"不自刷新"的老毛病）。
 */
export function getStorageInfo() {
  let online = 0;
  for (const d of devices.values()) if (d.ws && d.ws.readyState === 1) online++;
  let pendingCnt = 0;
  try { pendingCnt = Object.keys(listPendingAll()).length; } catch { pendingCnt = 0; }
  return {
    knownDevices: knownDevices.size,
    onlineDevices: online,
    offlineDevices: knownDevices.size - online,
    classes: classes.size,
    pendingInstructions: pendingCnt,
    eventsBytes,
    eventsFiles: EVENTS_KEEP_FILES,
    at: new Date().toISOString(),
  };
}

/* ---------- 管理端（viewer）：订阅帧 + 收设备表 ---------- */

export function addViewer(ws) {
  // live：按 uid 记"这个管理端要的是实时流还是只看图"。
  // 默认（不记）就是只看图 —— 别一接上就让人家被控端烧起 Chromium 推流。
  viewers.set(ws, { subs: new Set(), live: new Set(), auto: true });
  return viewers.size;
}

export function removeViewer(ws) {
  // 这个管理端 diss 掉之后，它订阅过的那几台"到底还有没有人看"全变了 ——
  // 必须逐台重算并推给设备（否则设备还停在那台管理端已走的旧结论上，继续白抓屏）。
  const affected = [];
  for (const [vws, v] of viewers) {
    if (vws !== ws) continue;
    for (const k of v.subs) if (!affected.includes(k)) affected.push(k);
  }
  viewers.delete(ws);
  for (const k of affected) pushWatchToAgent(k);
}

export function viewerCount() {
  return viewers.size;
}

/**
 * 订阅某设备的帧。返回 false = 这个.ws 不是已注册的 viewer（连接早断了）。
 * live=true 记进 v.live：只有它需要实时流（管理端点过"远控"）时才让设备端起推流。
 */
export function subscribeViewer(ws, uid, live) {
  const v = viewers.get(ws);
  if (!v) return false;
  const k = String(uid || '').trim();
  v.subs.add(k);
  if (live === true) v.live.add(k); else v.live.delete(k);
  // 订阅变了 → 把"现在谁在看这台"的最新结论推给设备（见 pushWatchToAgent）。
  // 设备端靠它决定要不要继续抓屏发帧：没人看的机器一个字节帧都不该发。
  pushWatchToAgent(k);
  return true;
}

/** 退订某设备的帧（按需拉流预留：没人看了就别推）。返回 false = 这个 ws 不是已注册的 viewer。 */
export function unsubscribeViewer(ws, uid) {
  const v = viewers.get(ws);
  if (!v) return false;
  const k = String(uid || '').trim();
  v.live.delete(k);
  const gone = v.subs.delete(k);
  if (gone) pushWatchToAgent(k);
  return gone;
}

/**
 * 把"这台设备当前被谁看着"的最新结论推给设备（type=watch）。
 * 为什么不让设备自己猜：设备端根本不知道云端有几个管理端、谁订了谁没订，
 * 只能"按时抓屏发帧"——机房几十台常年这么空转，CPU/GPU 就是这么吃满的（2026-10-07）。
 * 现在订阅关系一变，云端就显式告诉设备：你在不在被看。没人看就不抓、不编码、不发包。
 *
 * ⚠️ 失败必须如实记（不让设备停在一个过期的"没人看"结论上，那就等于永久黑屏）：
 * 设备此刻不在线很常见（管端先订阅、机器后开机），这种只记 warn；
 * 真写出去了才算送到。
 * @param {string} uid 设备码
 * @returns {{ok:boolean, watchers:number}}
 */
export function pushWatchToAgent(uid) {
  const key = String(uid || '').trim();
  const d = devices.get(key);
  if (!d || !d.ws || d.ws.readyState !== 1) {
    // 设备不在线：这次推不成，等它上线自会再收到一次（markConnected 末尾会补一次），
    // 不至于永久停在旧结论上。仍然记一笔，不许假装送到了。
    pushEvent('warn', '没人看这台的通知没送到设备（这台此刻不在线）', { uid: key, type: 'watch' });
    return { ok: false, watchers: -1 };
  }
  const uids = [];
  for (const [ws, v] of viewers) {
    if (ws.readyState !== 1) continue;
    if (v.subs.has(key)) uids.push(key);
  }
  try {
    sendTo(d.ws, 'watch', { uids });
  } catch (e) {
    pushEvent('error', '没人看这台的通知写给设备失败', { uid: key, error: e.message });
    return { ok: false, watchers: -1 };
  }
  // 只在"被看着的人数"发生变化时记一行：订阅一变就推，每次都记会把日志刷满。
  // 这行同时是排障凭据 —— 不然云端说"发了"、设备说"没收到"，两边只能互相猜。
  if (lastWatchLogUid !== key || lastWatchLogN !== uids.length) {
    lastWatchLogUid = key;
    lastWatchLogN = uids.length;
    pushEvent('info', '已把观看结论推给设备', { uid: key, watchers: uids.length });
  }
  return { ok: true, watchers: uids.length };
}

/** 管理端显式挑了某台之后，就不再自动给它补订阅别的设备了。 */
export function pinViewer(ws) {
  const v = viewers.get(ws);
  if (v) v.auto = false;
}

/** 设备上线时：所有还没挑设备的管理端，自动补上这个 uid 并收到 subscribed 通知。 */
export function autoSubscribeViewers(uid) {
  const key = String(uid || '');
  let n = 0;
  for (const [ws, v] of viewers) {
    if (!v.auto) continue;
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    v.subs.add(key);
    n++;
    try { sendTo(ws, 'subscribed', { uid: key, auto: true }); }
    catch (e) { pushEvent('error', '给管理端补订阅通知失败', { uid: key, error: e.message }); viewers.delete(ws); }
    // 设备上线后第一次有人看：只有这个管理端选的是"实时流"（之前点过远控）才推流。
    // 只看图的话不推 —— 被控端每推一次都要抓屏 + 拉起 Chromium 采集，机房几十台一起推，
    // 机器直接卡。默认一律走 JPEG 轮询，点远控才升级成实时。
    if (v.live && v.live.has(key)) rtcSignalToAgent(key, 'rtc-start', { uid: key, auto: true });
    // 补完订阅就把"你在被看着"的最新结论补发一次 —— 设备刚上线，之前所有订阅变更它都没收到过，
    // 停在一个"没人看"的旧结论上就会一直不抓屏（画面永远空着）。
    pushWatchToAgent(key);
  }
  return n;
}

/* 背压护栏（2026-10-13）：管理端消费不过来时（TCP 发送缓冲里堆着还没发出去的字节），
 * 继续塞只会把帧全积在**云端**内存里 —— 一条慢连接就能把整个云端吃爆。
 * 帧是**实时**画面/流式输出：丢掉新的、保住连接，比"攒到 OOM 再崩"强得多。
 * 这是"尽力而为"的既定语义（不是丢包 bug），所以只按 10s 节流报一次。 */
const RELAY_BUFFER_MAX = Number(process.env.CLOUD_RELAY_BUFFER_MAX || 8 * 1024 * 1024);
let relayBpWarnAt = 0;
function relayBlocked(ws) {
  if (ws.bufferedAmount <= RELAY_BUFFER_MAX) return false;
  const now = Date.now();
  if (now - relayBpWarnAt > 10000) {
    relayBpWarnAt = now;
    pushEvent('warn', '管理端消费不过来，暂停推流（背压）',
      { buffered: ws.bufferedAmount, limit: RELAY_BUFFER_MAX });
  }
  return true;
}

/**
 * 把一帧推给所有订阅了该 uid 的 viewer。推不出去就报出来，绝不假装推成功。
 * v1 对端收**二进制帧**（带 seq/ts/mime，省掉 base64 的 33% 开销），旧对端仍收 JSON base64。
 * @returns {{hits:number, bytes:number}}
 */
export function broadcastFrame(uid, buf, frameSeq = 0) {
  let hits = 0;
  for (const [ws, v] of viewers) {
    if (!v.subs.has(String(uid))) continue;
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    if (relayBlocked(ws)) continue;
    try {
      if (ws.__v1 === true) ws.send(makeFrame(uid, frameSeq, 'image/jpeg', buf));
      else ws.send(JSON.stringify({
        type: 'frame', uid: String(uid), ts: new Date().toISOString(),
        bytes: buf.length, data: buf.toString('base64'),
      }));
      hits++;
    } catch (e) {
      pushEvent('error', '推帧给管理端失败：写 socket 出错', { uid, error: e.message });
      viewers.delete(ws);
    }
  }
  return { hits, bytes: buf.length };
}

/**
 * 把被控端主动吐出来的 **terminal_* 文本帧** 转给订阅了这台的管理端。
 * 不能复用 broadcastFrame：那条发的是 image/jpeg 二进制帧（makeFrame 合成），
 * 终端流式输出要的是普通 JSON 文本帧，走不着那条路。
 * @returns 命中几个管理端
 */
/**
 * 按 uid 推给**订阅了这台设备**的管理端（谁订阅推谁，不群发）。返回命中数。
 * 终端流式帧、噪音样本/告警等流式消息都走它 —— 共同点是"只在有人盯着这台时才推"。
 */
export function relayToSubscribers(uid, type, payload) {
  let hits = 0;
  for (const [ws, v] of viewers) {
    if (!v.subs.has(String(uid))) continue;
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    if (relayBlocked(ws)) continue;
    try { sendTo(ws, type, payload); hits++; }
    catch { viewers.delete(ws); }
  }
  return hits;
}

/** 被控端远程终端的流式帧（保留旧名；内部就是"按 uid 推给订阅者"）。 */
export function relayTerminalFrame(uid, type, payload) {
  return relayToSubscribers(uid, type, payload);
}

/** 设备表广播（管理端每几秒刷一次列表用）。 */
export function broadcastDevices() {
  const devices = listDevices();
  let hits = 0;
  for (const [ws] of viewers) {
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    try { sendTo(ws, 'devices', { devices, at: new Date().toISOString() }); hits++; }
    catch { viewers.delete(ws); }
  }
  return hits;
}

/** 把任意消息广播给所有在线管理端（指令回执、执行结果等都走它）。返回命中数。 */
export function broadcastToViewers(type, payload) {
  let hits = 0;
  for (const [ws] of viewers) {
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    try { sendTo(ws, type, payload); hits++; }
    catch { viewers.delete(ws); }
  }
  return hits;
}

/**
 * 记录设备真实执行回执（done / failed / started）。失败必须如实记下，不许静默。
 * 三态说明：
 *   done     —— 做完了（老协议只有这一态，绝大多数动作都回这个）
 *   failed   —— 没做成（带 error）
 *   started  —— 起来了但活儿还没完（2026-10-06 远程终端的 terminal_open 回这个：
 *               会话已经开好并会持续吐帧，不是失败，只是"别把它当成最终结果"）。
 *               早期这版是 `(result === 'done') ? 'done' : 'failed'`，
 *               started 会被非黑即白地记成 failed —— 审计上看着像"终端没开起来"，实为误报。
 * @param data 动作的附加数据（进程列表 / 日志行 / 音量实际值 / 截图路径…），可空。
 *             协议规范第十节：可选字段，客户端不认就忽略，不算错。
 * @returns {{id, uid, action, result, error, data, at}}
 */
export function recordResult(id, uid, action, result, error, data) {
  const rec = {
    id,
    uid,
    action,
    // 只放行三种已知态 + confirmed（2026-10-07 通知确认）：设备回了个没见过的串，
    // 一律按 failed 记（保底：宁可显眼，别蒙过去）。
    result: (result === 'done' || result === 'started' || result === 'confirmed') ? result : 'failed',
    error: error || null,
    data: (data && typeof data === 'object') ? data : null,
    at: new Date().toISOString(),
  };
  // 2026-10-04：把下发时携带的 notice_id 带进回执，站点广播才能把"已执行"对账回通知表。
  // paramsOf 查这条指令的原始 params（云端表完整保留），无则留空。
  try {
    const src = paramsOf(id);
    if (src && src.params && typeof src.params === 'object') {
      const nid = src.params.notice_id ?? src.params.params?.notice_id;
      if (nid !== undefined && nid !== null) rec.notice_id = nid;
    }
  } catch { /* 查不到就不带，不影响回执本身 */ }
  results.set(id, rec);
  pruneResults();
  // 回执到达 → 队列里这条"已下发未回执"标完成，之后重连不再补发。
  // 不在队列（已过期、或落盘降级）也不报错：回执本身仍如实转给管理端。
  settleQueue(id, rec.result, rec.error);
  // started 不是失败，别往红色事件流里塞
  pushEvent(rec.result === 'failed' ? 'error' : 'info',
    `设备真实执行回执 action=${action} result=${rec.result}`,
    { uid, id, error: rec.error || undefined, ...(rec.notice_id !== undefined ? { notice_id: rec.notice_id } : {}) });
  return rec;
}

export function getResult(id) {
  return results.get(id) || null;
}

/**
 * 下发指令。在线 → 先落盘拿到稳定 id，再真的写进 socket，返回 sent；
 * 不在线 → 返回 offline（绝不谎报 sent，**也不入补发队列**——离线不能当收下）。
 * @returns {{state:'sent'|'offline', id?:number, detail?:string}}
 */
export function sendInstruction(uid, action, params) {
  const d = devices.get(uid);
  if (!d || d.ws?.readyState !== 1) {
    // 关键：离线只报离线。若把离线指令也塞进队列，重连时就会补发出去 = 变相把"离线"当成了"收下"。
    return { state: 'offline', detail: '设备不在线，指令未下发（也不入补发队列，避免假绿）' };
  }
  // 先落盘再发：要拿到重启后仍单调不重号的 id（回执按它配对、重连按它排序补发）。
  // 落盘失败（降级为 null）时退回进程内自增 id，照常下发，只是这条没有"重启不丢"的兜底。
  const id = enqueueDispatch(uid, action, params);
  const msgId = id ?? ++seq;
  // 出向走协议层：v1 对端收信封（ts 在信封上），旧对端保持原有扁平格式（含 at 字段，透传不改）
  const text = (d.ws.__v1 === true)
    ? wrapOutgoing(true, 'instruction', { action, params }, String(msgId))
    : JSON.stringify({ type: 'instruction', action, params, id: msgId, at: new Date().toISOString() });
  try {
    d.ws.send(text);
    pushEvent('info', '已下发指令', { uid, action, id: msgId });
    return { state: 'sent', id: msgId };
  } catch (e) {
    // 写 socket 失败 = 没发出去：把刚落的那条记录撤掉（别留成"等回执"），并报 error，不能当 sent
    dropDispatch(id);
    pushEvent('error', '下发失败：写 socket 出错', { uid, action, error: e.message });
    return { state: 'offline', detail: `写 socket 失败：${e.message}` };
  }
}

/* ---------- WebRTC 信令中继（P1-A 实时画面）----------
 * 三进程链路：被控端(agent) -> 云端按 uid 转发 -> 管理端(viewer)，反之亦然。
 * 云端**只转不发**：不解析 sdp / candidate 内容，媒体流（RTP）不经过云端 —— 云端一旦挂，画面立刻断，
 *   这是刻意的：云端只当信令中间人，媒体走点对点，教室机出公网也不必让云端扛带宽。
 * 三条纪律（沿用本文件开头那三条）：
 *   ① 转不出去必须报出来（不许静默）；② 转了 0 个对端必须报出来（"发出去了" != "有人收"）；
 *   ③ 只投给订阅了这个 uid 的那一端，不群发。
 * 谁建 offer：被控端建 —— 它一直抓着屏，教室机无人值守，不能等有人去点按钮。
 */

/** 云端 -> 设备的 WebRTC 开关信号（订阅开始/停止），与管理端 subscribe/unsubscribe 一一对应。
 *  有人在看了设备才起推流，没人看就停：免得在没人看的机器上空耗 CPU。
 *  @returns {{ok:boolean, to:number, detail?:string}} */
export function rtcSignalToAgent(uid, type, payload = {}) {
  const key = String(uid || '').trim();
  const d = devices.get(key);
  if (!d || !d.ws || d.ws.readyState !== 1) {
    // 这条很常见（管理端先订阅、设备后上线）：不判成错误，但必须记一笔，不许假装发过了
    pushEvent('warn', 'WebRTC 开关信号没送到设备（这台此刻不在线）', { uid: key, type });
    return { ok: false, to: 0, detail: '设备不在线' };
  }
  try {
    sendTo(d.ws, type, { ...payload, uid: key, from: 'cloud' });
    return { ok: true, to: 1 };
  } catch (e) {
    pushEvent('error', 'WebRTC 开关信号写给设备失败', { uid: key, type, error: e.message });
    return { ok: false, to: 0, detail: e.message };
  }
}

/** 设备 -> 管理端的 WebRTC 信令（offer/answer/ice）转发，只投给订阅了这个 uid 的管理端。
 *  @returns {{ok:boolean, to:number, detail?:string}} */
export function rtcRelayToViewers(uid, type, payload = {}) {
  const key = String(uid || '').trim();
  // 防回声：发信人就是本端（管理端）自己的消息，不转 —— 否则 offer/answer 会在两端之间无限对传
  if (payload.from === 'viewer') {
    pushEvent('warn', '丢弃同源 WebRTC 信令（防回声）', { uid: key, type, from: payload.from });
    return { ok: false, to: 0, detail: '同源信令，不回传' };
  }
  let hits = 0;
  for (const [ws, v] of viewers) {
    if (!v.subs.has(key)) continue;
    if (ws.readyState !== 1) { viewers.delete(ws); continue; }
    try {
      // 转发时**补上 uid**：对端有时只带 sdp/candidate 不带 uid，但它必须知道这是哪台机器
      sendTo(ws, type, { ...payload, uid: key, from: 'agent' });
      hits++;
    } catch (e) {
      pushEvent('error', 'WebRTC 信令写给管理端失败：写 socket 出错', { uid: key, type, error: e.message });
      viewers.delete(ws);
    }
  }
  if (hits === 0) pushEvent('warn', 'WebRTC 信令发出去了但没人订阅这台设备', { uid: key, type });
  return { ok: hits > 0, to: hits };
}

/** 管理端 -> 设备的 WebRTC 信令（offer/answer/ice）转发。 */
export function rtcRelayToAgent(uid, type, payload = {}) {
  const key = String(uid || '').trim();
  // 防回声：发信人就是本端（设备）自己的消息，不转
  if (payload.from === 'agent') {
    pushEvent('warn', '丢弃同源 WebRTC 信令（防回声）', { uid: key, type, from: payload.from });
    return { ok: false, to: 0, detail: '同源信令，不回传' };
  }
  const d = devices.get(key);
  if (!d || !d.ws || d.ws.readyState !== 1) {
    pushEvent('warn', 'WebRTC 信令发不出去：设备不在线', { uid: key, type });
    return { ok: false, to: 0, detail: '设备不在线' };
  }
  try {
    sendTo(d.ws, type, { ...payload, uid: key, from: 'viewer' });
    return { ok: true, to: 1 };
  } catch (e) {
    pushEvent('error', 'WebRTC 信令写给设备失败', { uid: key, type, error: e.message });
    return { ok: false, to: 0, detail: e.message };
  }
}

export function listEvents(limit = 50) {
  return events.slice(-limit).reverse();
}

/** 某设备"已下发未回执"的指令（落盘队列），按原下发顺序。无 uid → 全部按 uid 分组。 */
export function listPending(uid) {
  return uid ? pendingFor(uid) : listPendingAll();
}

export function ensureEventsFile() {
  try {
    eventsBytes = fs.existsSync(EVENTS_FILE) ? fs.statSync(EVENTS_FILE).size : 0;
    if (!fs.existsSync(EVENTS_FILE)) fs.writeFileSync(EVENTS_FILE, '');
  } catch (e) {
    console.error(`[cloud] FAIL 无法初始化 ${EVENTS_FILE}: ${e.message}`);
  }
}

/* ---------- 语音对讲（设计文档 3.3 第一版，2026-10-07）----------
 * 老师→全班单向广播：管理端采集 PCM → 二进制帧发云端 → 云端 fan-out 给所有在线
 * agent → agent 播放。云端只转发不解码不转码（设计文档 3.3 关键）。
 * 第一版目标 = 全部在线设备（班级映射在站点侧，云端第一版不做按班路由）。
 */

/** 当前是否有老师正在讲话（音频会话状态，管理端 start/stop 控制）。 */
let g_audioBroadcasting = false;
/** 语音通话（duplex）的发起管理端 ws —— 学生上行音频只单播回它，不外泄（2026-10-09）。 */
let g_audioTeacherWs = null;
export function isAudioBroadcasting() { return g_audioBroadcasting; }
export function setAudioBroadcasting(on, teacherWs = null) {
  g_audioBroadcasting = on;
  g_audioTeacherWs = on ? teacherWs : null;
  pushEvent('info', `语音对讲${on ? '开始' : '结束'}`);
  // 通知所有在线被控端：老师开始/结束讲话（学生端"正在讲话"提示用）
  for (const d of devices.values()) {
    if (!d.ws || d.ws.readyState !== 1) continue;
    try {
      const text = (d.ws.__v1 === true)
        ? wrapOutgoing(true, on ? 'audio.start' : 'audio.stop', {}, '')
        : JSON.stringify({ type: on ? 'audio.start' : 'audio.stop' });
      d.ws.send(text);
    } catch (e) {
      pushEvent('error', '语音状态通知写给设备失败', { uid: d.uid, error: e.message });
    }
  }
  return on;
}

/**
 * 把一帧音频（PCM 二进制）转发给所有在线被控端。
 * @param {Buffer} buf 音频帧原始字节
 * @param {string} mime 内容类型（audio/pcm 等）
 * @param {number} seq 帧序号（日志用）
 * @returns {{hits:number, bytes:number}}
 */
export function broadcastAudioToAgents(buf, mime, seq) {
  let hits = 0;
  for (const d of devices.values()) {
    if (!d.ws || d.ws.readyState !== 1) continue;
    // 复用 v1 二进制帧格式（makeFrame 用 uid 标记来源）：audio 帧 uid 用虚拟
    // "audio:<seq>" 来源（agent 端按 mime=audio/pcm 区分语音帧与画面帧，不读 uid）。
    try {
      d.ws.send(makeFrame(`audio:${seq}`, seq, mime, buf));
      hits++;
    } catch (e) {
      pushEvent('error', '语音帧写给设备失败', { uid: d.uid, error: e.message });
    }
  }
  if (hits > 0) pushEvent('info', '语音帧已推给被控端', { hits, bytes: buf.length, seq });
  return { hits, bytes: buf.length };
}

/* ---------- 语音通话上行（2026-10-09 · 语音通话功能设计 §4.2）----------
 * 学生端 mic 采集的 audio/pcm 上行帧：单播回「发起 duplex 对讲的管理端」，
 * 不 fan-out 全教室（学生只对老师说话，隐私边界）。无发起者或非 duplex
 * 会话 → 丢弃并留事件流水。
 */
export function duplexActive() { return g_audioBroadcasting && g_audioTeacherWs != null; }
export function sendAudioToTeacher(buf, seq, fromUid) {
  if (!duplexActive() || !g_audioTeacherWs || g_audioTeacherWs.readyState !== 1) {
    pushEvent('warn', '学生上行音频被丢弃（无 duplex 会话或发起者已离线）', { from: fromUid });
    return { hits: 0 };
  }
  try {
    // 复用 v1 二进制帧格式；来源标记 student:<uid>:<seq>，管理端按 mime=audio/pcm 播放
    g_audioTeacherWs.send(makeFrame(`student:${fromUid}:${seq}`, seq, 'audio/pcm', buf));
    pushEvent('info', '学生上行语音已单播回老师', { from: fromUid, bytes: buf.length, seq });
    return { hits: 1, bytes: buf.length };
  } catch (e) {
    pushEvent('error', '学生上行语音写给管理端失败', { from: fromUid, error: e.message });
    return { hits: 0 };
  }
}

/* ---------- 屏幕广播（设计文档《屏幕广播-第一版设计》2026-10-07）----------
 * 管理端抓屏 → 云端 → 被控端全屏显示。与语音同一 fan-out 机制，
 * 区别只在 mime：image/jpeg（广播）vs audio/pcm（语音）。
 */

/** 当前是否有屏幕广播在播（管理端 broadcast.start/stop 控制）。 */
let g_screenBroadcasting = false;
export function isScreenBroadcasting() { return g_screenBroadcasting; }
export function setScreenBroadcasting(on) {
  g_screenBroadcasting = on;
  pushEvent('info', `屏幕广播${on ? '开始' : '结束'}`);
  // 通知所有在线被控端（学生端显示"正在接收老师屏幕"提示）
  for (const d of devices.values()) {
    if (!d.ws || d.ws.readyState !== 1) continue;
    try {
      const text = (d.ws.__v1 === true)
        ? wrapOutgoing(true, on ? 'broadcast.start' : 'broadcast.stop', {}, '')
        : JSON.stringify({ type: on ? 'broadcast.start' : 'broadcast.stop' });
      d.ws.send(text);
    } catch (e) {
      pushEvent('error', '广播状态通知写给设备失败', { uid: d.uid, error: e.message });
    }
  }
  return on;
}

/**
 * 把一帧屏幕广播（JPEG 二进制）转发给所有在线被控端。
 * @param {Buffer} buf JPEG 帧
 * @param {number} seq 帧序号
 * @returns {{hits:number, bytes:number}}
 */
export function broadcastScreenToAgents(buf, seq) {
  let hits = 0;
  for (const d of devices.values()) {
    if (!d.ws || d.ws.readyState !== 1) continue;
    try {
      d.ws.send(makeFrame(`bcast:${seq}`, seq, 'image/jpeg', buf));
      hits++;
    } catch (e) {
      pushEvent('error', '广播帧写给设备失败', { uid: d.uid, error: e.message });
    }
  }
  if (hits > 0) pushEvent('info', '广播帧已推给被控端', { hits, bytes: buf.length, seq });
  return { hits, bytes: buf.length };
}

