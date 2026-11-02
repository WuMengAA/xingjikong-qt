// 媒体 fan-out：语音对讲 + 屏幕广播（2026-11-02 · 屎山整理 #5，从 registry.js 搬出）
//
// 为什么是这两个：它们是 registry.js 里仅有的两个**叶子域** —— 只用设备表遍历下发
// 与 pushEvent 记账，不碰 classes / viewers / results / seq。搬走它们不动任何状态所有权。
//
// ⚠️ 与 registry.js 构成 ESM 循环导入。安全的两个前提（搬动脚本做了断言）：
//   ① 两边都只有 export function（声明提升 + 活绑定），本文件顶层不调用 registry 任何东西；
//   ② registry.js 顶层也不调用本文件任何东西。任何人在本文件顶层加一句调用，
//      都会在冷启动时拿到 TDZ 错误 —— 那正是「整个云端起不来」的样子，务必别加。
import { wrapOutgoing, makeFrame } from './protocol.js';
import { pushEvent, registryDevices } from './registry.js';

/* ---------- 语音对讲（设计文档 3.3 第一版，2026-10-07）----------
 * 老师→全班单向广播：管理端采集 PCM → 二进制帧发云端 → 云端 fan-out 给所有在线
 * agent → agent 播放。云端只转发不解码不转码（设计文档 3.3 关键）。
 * 第一版目标 = 全部在线设备（班级映射在站点侧，云端第一版不做按班路由）。
 */

/** 当前是否有老师正在讲话（音频会话状态，管理端 start/stop 控制）。 */
let g_audioBroadcasting = false;
/** 语音通话（duplex）的发起管理端 ws —— 学生上行音频只单播回它，不外泄（2026-10-09）。 */
let g_audioTeacherWs = null;
/** 语音会话累计（2026-10-18 · 方案 S1/缺陷 D1「日志降噪」）：
 *  帧级**只累计、不写事件**，会话结束时一次性入账。
 *  原来 broadcastAudioToAgents 每帧 pushEvent('info', '语音帧已推给被控端')，
 *  20ms 一帧 = 50 条/秒 ≈ 3000 条/分钟，把事件表刷成噪音（用户点名的「语音广播60s」）。
 *  同一裁决也适用于屏幕广播（10 帧/秒）——见 g_screenStat。 */
const g_audioStat = { frames: 0, bytes: 0, startedAt: 0 };
/** 学生上行（duplex）累计：同一会话里由学生 mic 回传的帧，随「语音对讲结束」一次性入账。 */
const g_uplinkStat = { frames: 0, bytes: 0 };
export function isAudioBroadcasting() { return g_audioBroadcasting; }
export function setAudioBroadcasting(on, teacherWs = null) {
  const was = g_audioBroadcasting;
  g_audioBroadcasting = on;
  g_audioTeacherWs = on ? teacherWs : null;
  if (on) {
    g_audioStat.frames = 0;
    g_audioStat.bytes = 0;
    g_audioStat.startedAt = Date.now();
    g_uplinkStat.frames = 0;
    g_uplinkStat.bytes = 0;
    pushEvent('info', '语音对讲开始');
  } else if (was) {
    const seconds = g_audioStat.startedAt
      ? Math.round((Date.now() - g_audioStat.startedAt) / 1000) : 0;
    pushEvent('info', '语音对讲结束', {
      seconds,
      frames: g_audioStat.frames,
      kib: Math.round(g_audioStat.bytes / 1024),
      uplinkFrames: g_uplinkStat.frames,
      uplinkKib: Math.round(g_uplinkStat.bytes / 1024),
    });
  } else {
    pushEvent('info', '语音对讲结束');   // 没开就收到 stop：保持原语义，不编统计
  }
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
// 背压护栏节流（同 RELAY_BUFFER_MAX，2026-10-21）：被控端/管理端消费不过来时，
// 继续塞会把帧全积在云端内存、延迟只增不减。语音是实时流，丢这一帧比攒到 OOM 强。
let audioBpWarnAt = 0;
let audioUplinkBpWarnAt = 0;

export function broadcastAudioToAgents(buf, mime, seq) {
  let hits = 0;
  for (const d of devices.values()) {
    if (!d.ws || d.ws.readyState !== 1) continue;
    // 背压护栏：某台被控端消费不过来时跳过该设备，不阻塞其他设备、不报错刷屏（10s 节流告警）。
    if (d.ws.bufferedAmount > RELAY_BUFFER_MAX) {
      const now = Date.now();
      if (now - audioBpWarnAt > 10000) {
        audioBpWarnAt = now;
        pushEvent('warn', '被控端消费不过来，跳过该设备语音帧（背压）',
          { uid: d.uid, buffered: d.ws.bufferedAmount, limit: RELAY_BUFFER_MAX });
      }
      continue;
    }
    // 复用 v1 二进制帧格式（makeFrame 用 uid 标记来源）：audio 帧 uid 用虚拟
    // "audio:<seq>" 来源（agent 端按 mime=audio/pcm 区分语音帧与画面帧，不读 uid）。
    try {
      d.ws.send(makeFrame(`audio:${seq}`, seq, mime, buf));
      hits++;
    } catch (e) {
      pushEvent('error', '语音帧写给设备失败', { uid: d.uid, error: e.message });
    }
  }
  // 帧级**不写事件**（见 g_audioStat 的注释）：只累计，会话结束由 setAudioBroadcasting 入账。
  g_audioStat.frames += 1;
  g_audioStat.bytes += buf.length;
  return { hits, bytes: buf.length };
}

/* ---------- 语音通话上行（2026-10-09 · 语音通话功能设计 §4.2）----------
 * 学生端 mic 采集的 audio/pcm 上行帧：单播回「发起 duplex 对讲的管理端」，
 * 不 fan-out 全教室（学生只对老师说话，隐私边界）。无发起者或非 duplex
 * 会话 → 丢弃并留事件流水。
 */
export function duplexActive() { return g_audioBroadcasting && g_audioTeacherWs != null; }
export function sendAudioToTeacher(buf, seq, fromUid, mime = 'audio/pcm') {
  if (!duplexActive() || !g_audioTeacherWs || g_audioTeacherWs.readyState !== 1) {
    pushEvent('warn', '学生上行音频被丢弃（无 duplex 会话或发起者已离线）', { from: fromUid });
    return { hits: 0 };
  }
  // 背压护栏：管理端消费不过来时跳过这一帧，避免云端积压（10s 节流告警）。
  if (g_audioTeacherWs.bufferedAmount > RELAY_BUFFER_MAX) {
    const now = Date.now();
    if (now - audioUplinkBpWarnAt > 10000) {
      audioUplinkBpWarnAt = now;
      pushEvent('warn', '管理端消费不过来，跳过学生上行语音帧（背压）',
        { from: fromUid, buffered: g_audioTeacherWs.bufferedAmount, limit: RELAY_BUFFER_MAX });
    }
    return { hits: 0 };
  }
  try {
    // 复用 v1 二进制帧格式；来源标记 student:<uid>:<seq>，管理端按 mime 播放
    // 2026-10-21（P1）：mime 原样透传（audio/opus 或 audio/pcm），云端只转发不解码。
    g_audioTeacherWs.send(makeFrame(`student:${fromUid}:${seq}`, seq, mime, buf));
    // 帧级不写事件（同 D1）：学生上行也是 20ms 一帧，逐帧 pushEvent 同样会刷屏。
    g_uplinkStat.frames += 1;
    g_uplinkStat.bytes += buf.length;
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
/** 屏幕广播累计（同 D1：10 帧/秒逐帧写事件同样是噪音）。会话结束一次性入账。 */
const g_screenStat = { frames: 0, bytes: 0, startedAt: 0 };
export function isScreenBroadcasting() { return g_screenBroadcasting; }
export function setScreenBroadcasting(on) {
  const was = g_screenBroadcasting;
  g_screenBroadcasting = on;
  if (on) {
    g_screenStat.frames = 0;
    g_screenStat.bytes = 0;
    g_screenStat.startedAt = Date.now();
    pushEvent('info', '屏幕广播开始');
  } else if (was) {
    const seconds = g_screenStat.startedAt
      ? Math.round((Date.now() - g_screenStat.startedAt) / 1000) : 0;
    pushEvent('info', '屏幕广播结束', {
      seconds,
      frames: g_screenStat.frames,
      kib: Math.round(g_screenStat.bytes / 1024),
    });
  } else {
    pushEvent('info', '屏幕广播结束');   // 没开就收到 stop：保持原语义
  }
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
  // 帧级不写事件（见 g_screenStat 的注释）：只累计，会话结束由 setScreenBroadcasting 入账。
  g_screenStat.frames += 1;
  g_screenStat.bytes += buf.length;
  return { hits, bytes: buf.length };
}
