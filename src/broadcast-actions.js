// 星集控云端 · 广播台指令白名单与参数校验（2026-11-09 · I1）
//
// 与 control-qt/src/broadcast/BroadcastActions.h **同源契约**（14 个 bc_* 动作）。
// 为什么云端要有一份：云端是 REST 入口，必须在下发前就知道哪些动作合法；
// 被控端执行层再校验一次（双闸门），两端各自解析但词表一一对应。
//
// ⚠️ 命名红线：execute_action.cpp:124-136 把 voicehub_broadcast / song_request /
//    lyrics / broadcast_audio 判成「未装插件」needs-plugin。所有新动作带 bc_ 前缀避开。
//
// 用户裁决「按推荐的来」：本端点挂在 /api/broadcast-club/* 下，**不放开管理面**。
// broadcast_club 票在管理面仍 401（index.js:942-947），既有安全裁决零改动。

import { getDeviceRole, getDeviceClassCode, sendInstruction, pushEvent } from './registry.js';
import { proxyReorder } from './broadcast-club.js';

/**
 * 排期重排上报（2026-11-09 · 去除端侧 apiKey 遗留）。
 *
 * 旧实现：被控端 VoiceHubModule::reorderSchedule 直连
 *   POST {voicehub}/api/open/schedules/reorder + x-api-key，等于把写密钥下发到每台教室机。
 * 新实现：端侧经已鉴权 WS 上报 schedule_reorder → 云端用自己持有的 Key 走 proxyReorder，
 *   并把真实结果以 schedule-reorder-ack 回给端上（不伪造成功）。
 *
 * 闸门：只有 devrole=broadcast 的广播站设备能改排期（教室机一律拒绝）。
 */
export function handleScheduleReorder({ p, uid, snd, ws, pushEvent: log }) {
  if (getDeviceRole(uid) !== 'broadcast') {
    log('warn', '非广播机尝试改排期被拒', { uid });
    snd(ws, 'schedule-reorder-ack', { ok: false, message: '只有广播站设备可以修改排期' });
    return true;
  }
  const pairs = Array.isArray(p.payload?.schedules) ? p.payload.schedules : [];
  proxyReorder(pairs).then((r) => {
    if (!r.ok) {
      log('warn', '广播站排期保存失败', { uid, error: r.error });
      snd(ws, 'schedule-reorder-ack', { ok: false, message: r.error || '保存失败' });
      return;
    }
    pushEvent('info', '广播站排期已保存（云端代发）', { uid, count: pairs.length });
    snd(ws, 'schedule-reorder-ack', { ok: true, message: '已保存' });
  }).catch((e) => {
    snd(ws, 'schedule-reorder-ack', { ok: false, message: `保存失败：${e.message}` });
  });
  return true;   // 已接管，调用方直接 return（异步结果经 ack 回）
}

/** 动作名 → { 必需参数, 需要间期闸门 }。顺序与 C++ kActions 一致。 */
export const BC_ACTIONS = Object.freeze({
  bc_start:            { required: [],                    gate: 'admin' },
  bc_pause:            { required: [],                    gate: 'admin' },
  bc_resume:           { required: [],                    gate: 'admin' },
  bc_next:             { required: [],                    gate: 'admin' },
  bc_seek:             { required: ['positionSec'],       gate: 'admin' },
  bc_stop:             { required: [],                    gate: 'admin' },
  bc_set_schedule:     { required: ['entries'],           gate: 'admin' },
  bc_reorder:          { required: ['pairs'],             gate: 'admin' },
  bc_set_interlude:    { required: ['index', 'gapAfterSec'], gate: 'admin' },
  bc_extend_interlude: { required: ['extraSec'],          gate: 'interlude' },
  bc_hold_music:       { required: ['hold'],              gate: 'admin' },
  bc_announce_start:   { required: ['text', 'durationSec'], gate: 'announce' },
  bc_announce_end:     { required: [],                    gate: 'interlude' },
  bc_state_query:      { required: [],                    gate: 'none' },
});

export function isBroadcastAction(action) {
  return Object.prototype.hasOwnProperty.call(BC_ACTIONS, String(action || ''));
}

/**
 * 参数校验（与 C++ validate() 同口径）。
 * 返回 { ok, error, ecode }；error 可直接回执给音乐管理员，要看得懂、给得出下一步。
 */
export function validateBroadcastCommand(action, params) {
  const spec = BC_ACTIONS[String(action)];
  if (!spec) {
    return { ok: false, ecode: 'CMD_UNKNOWN_ACTION', error: `未知的广播台动作：${action}` };
  }
  const p = (params && typeof params === 'object' && !Array.isArray(params)) ? params : {};
  for (const k of spec.required) {
    if (p[k] === undefined || p[k] === null) {
      return { ok: false, ecode: 'CMD_PARAM_INVALID', error: `动作 ${action} 缺参数 ${k}` };
    }
  }
  for (const [k, v] of Object.entries(p)) {
    if (typeof v === 'number' && !Number.isFinite(v)) {
      return { ok: false, ecode: 'CMD_PARAM_INVALID', error: `参数 ${k} 不是有限数值` };
    }
  }
  if (action === 'bc_seek' && p.positionSec < 0) {
    return { ok: false, ecode: 'CMD_PARAM_INVALID', error: '进度不能为负' };
  }
  if (action === 'bc_set_interlude'
      && (p.gapAfterSec < 0 || p.index < 0)) {
    return { ok: false, ecode: 'CMD_PARAM_INVALID', error: '间期长度/排期序号不能为负' };
  }
  if (action === 'bc_extend_interlude'
      && (!(p.extraSec > 0) || p.extraSec > 600)) {
    return { ok: false, ecode: 'CMD_PARAM_INVALID',
             error: '延长时间须在 1~600 秒（防止音乐永远不响）' };
  }
  if (action === 'bc_announce_start') {
    if (String(p.text ?? '').trim() === '') {
      return { ok: false, ecode: 'CMD_PARAM_INVALID', error: '播报稿不能为空' };
    }
    if (!(p.durationSec > 0) || p.durationSec > 3600) {
      return { ok: false, ecode: 'CMD_PARAM_INVALID', error: '播报时长须在 1~3600 秒' };
    }
  }
  return { ok: true, gate: spec.gate };
}

/** 是否在间期类动作之外（辅助：I1 不做编排状态判定，硬闸门留给 I2 执行层）。 */
export function needsInterludeGate(action) {
  const s = BC_ACTIONS[String(action)];
  return !!s && (s.gate === 'interlude' || s.gate === 'announce');
}

/**
 * I1 端点主体：POST /api/broadcast-club/command
 *
 * 用户裁决「按推荐的来」：挂在 broadcast-club 组下，用**成员票**鉴权，
 * 不放开管理面（broadcast_club 票在管理面仍 401，既有安全裁决零改动）。
 *
 * 三道校验后才下发：
 *   ① 动作词表 + 参数（validateBroadcastCommand）
 *   ② station 匹配：目标设备的站码必须等于票里的 station
 *   ③ 设备用途必须是 broadcast（教室机一律拒绝，哪怕站码碰巧相同）
 * 最后复用既有 sendInstruction 通道，不新造下发链路。
 *
 * 「广播只在音乐间期」的**运行时**硬闸门在 I2 被控端执行层（它有 G0 Timeline 与
 * 权威时钟）；云端这里只做 gate 标记透传，不自己算编排状态（避免第二份时钟）。
 */
export function dispatchBroadcastCommand({ req, res, send, v }) {
  let body = '';
  req.on('data', (c) => { body += c; if (body.length > 1e5) req.destroy(); });
  req.on('end', () => {
    let p;
    try { p = JSON.parse(body || '{}'); }
    catch (e) { return send(400, { ok: false, error: `请求体不是合法 JSON：${e.message}` }); }
    const action = String(p.action || '');
    const uid = String(p.uid || '').trim();
    const params = p.params ?? {};
    const chk = validateBroadcastCommand(action, params);
    if (!chk.ok) return send(400, { ok: false, ecode: chk.ecode, error: chk.error });
    if (!uid) return send(400, { ok: false, ecode: 'CMD_PARAM_INVALID', error: '缺 uid（目标广播端设备）' });

    // 站码与班码同一命名空间（registry.js:318-326），直接比对即可。
    const targetStation = getDeviceClassCode(uid) || '';
    if (!v.station || !targetStation || v.station !== targetStation) {
      pushEvent('warn', '广播台指令越站被拒', { uid, station: v.station, targetStation, action, who: v.uid });
      return send(403, { ok: false, ecode: 'PV4002',
        error: `只能操作本站（${v.station || '未绑定'}）的广播端设备` });
    }
    if (getDeviceRole(uid) !== 'broadcast') {
      pushEvent('warn', '广播台指令目标非广播机被拒', { uid, action, who: v.uid });
      return send(403, { ok: false, ecode: 'PV4002', error: '目标设备不是广播机，拒绝下发播放指令' });
    }

    const r = sendInstruction(uid, action, params);
    if (r.state !== 'sent') {
      pushEvent('warn', '广播台指令下发失败', { uid, action, state: r.state, who: v.uid });
      const ecode = r.state === 'offline' ? 'DV3001' : 'DV3002';
      return send(409, { ok: false, ecode, state: r.state, error: r.detail || '设备不在线或写入失败' });
    }
    pushEvent('info', '广播台指令已下发', { uid, action, gate: chk.gate, who: v.uid, station: v.station });
    return send(200, { ok: true, state: 'sent', uid, action, gate: chk.gate });
  });
  return undefined;
}
