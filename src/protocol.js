// 协议层 v1（《星集控-协议规范v1-2026-10-03.md》的实现）
//
// 为什么要有这一层：以前消息是"想到一条加一条"的扁平 {type:...}，agent 与 viewer 两套、
// 没有版本号、帧没有序号。加任何新机制都要在两端各猜一遍。
// 这一层做三件事：
//   1. 入向：把 v1 信封与旧扁平格式**规范化**成同一种内部表示（过渡期双认，不破坏现有两端）；
//   2. 出向：按对端能力发 v1 信封或旧扁平格式（对端发什么格式，我就回什么格式）；
//   3. 错误：统一错误码 + 人话原因，任何拒绝都不许静默（fail-silent 红线）。

import { PROTOCOL_MODE } from './config.js';

export const PROTOCOL_VERSION = 1;

/** 错误码 → 是否关闭连接（true=关）。关闭码沿用既有约定，避免老客户端困惑。 */
export const ERR = {
  BAD_VERSION: { close: 4001, msg: '协议版本不支持' },
  BAD_JSON: { close: 4003, msg: '消息不是合法 JSON' },
  BAD_PAYLOAD: { close: 0, msg: '消息缺必填字段' },
  UNKNOWN_TYPE: { close: 0, msg: '消息类型不认识' },
  AUTH_REQUIRED: { close: 4003, msg: '握手（register/auth）之前不能发别的消息' },
  BAD_TOKEN: { close: 4004, msg: '令牌校验失败' },
  AUTH_FAILED: { close: 4006, msg: '管理端鉴权失败' },
  HANDSHAKE_TIMEOUT: { close: 4002, msg: '握手超时' },
  HEARTBEAT_TIMEOUT: { close: 4000, msg: '心跳超时，判定离线' },
  NOT_SUBSCRIBED: { close: 0, msg: '没有订阅这台设备' },
  DEVICE_OFFLINE: { close: 0, msg: '设备不在线' },
};

export function errPayload(code, detail) {
  const e = ERR[code] || { msg: '未知错误' };
  return { code, message: detail ? `${e.msg}：${detail}` : e.msg };
}

/**
 * 规范化一条入向文本消息。
 * @returns {{ok:true, v:number, type:string, id:string, ts:number, payload:object, legacy:boolean}}
 *          | {{ok:false, code:string, detail:string}}
 */
export function parseIncoming(text) {
  let obj;
  try { obj = JSON.parse(text); }
  catch (e) { return { ok: false, code: 'BAD_JSON', detail: e.message }; }
  if (!obj || typeof obj !== 'object') return { ok: false, code: 'BAD_PAYLOAD', detail: '不是对象' };

  // v1 信封：有 v + type + payload
  if (Object.prototype.hasOwnProperty.call(obj, 'v') && obj.payload !== undefined) {
    const v = Number(obj.v);
    if (!Number.isFinite(v)) return { ok: false, code: 'BAD_VERSION', detail: 'v 不是数字' };
    if (v !== PROTOCOL_VERSION) return { ok: false, code: 'BAD_VERSION', detail: `收到 v${v}，本服务端只支持 v${PROTOCOL_VERSION}` };
    if (PROTOCOL_MODE === 'legacy') return { ok: false, code: 'BAD_VERSION', detail: '服务端当前只接受旧格式' };
    if (typeof obj.type !== 'string' || !obj.type) return { ok: false, code: 'BAD_PAYLOAD', detail: '缺 type' };
    return {
      ok: true, v, type: obj.type,
      id: typeof obj.id === 'string' ? obj.id : '',
      ts: Number(obj.ts) || Date.now(),
      payload: (obj.payload && typeof obj.payload === 'object') ? obj.payload : {},
      legacy: false,
    };
  }

  // 旧扁平格式：{type, ...其余字段}
  if (PROTOCOL_MODE === 'v1-strict') {
    return { ok: false, code: 'BAD_VERSION', detail: '服务端已切换到 v1-strict，旧格式不再接受' };
  }
  if (typeof obj.type !== 'string' || !obj.type) return { ok: false, code: 'BAD_PAYLOAD', detail: '缺 type' };
  const { type, id, ts, ...rest } = obj;
  return {
    ok: true, v: 0, type,
    id: id === undefined ? '' : String(id),
    ts: Number(ts) || Date.now(),
    payload: rest,
    legacy: true,
  };
}

/**
 * 出向封装：对端是 v1 就发信封，是旧客户端就发旧扁平格式（保证过渡期不断链）。
 * @param {boolean} peerV1 这个连接是否发过 v1 消息
 */
export function wrapOutgoing(peerV1, type, payload = {}, id = '') {
  if (peerV1) return JSON.stringify({ v: PROTOCOL_VERSION, type, id, ts: Date.now(), payload });
  return JSON.stringify({ type, id, ts: Date.now(), ...payload });
}

/* ---------- 二进制帧 ----------
 * v1 帧：[1B 版本=1][2B 大端 headerLen][header JSON][payload]
 *   header: {v,type:'frame',uid,seq,ts,mime,bytes}
 * 旧帧：裸 JPEG（没有头部）——过渡期靠"首字节是否为 1 且 headerLen 合理"来区分。
 */
export function makeFrame(uid, seq, mime, buf) {
  const header = Buffer.from(JSON.stringify({
    v: PROTOCOL_VERSION, type: 'frame', uid: String(uid), seq: Number(seq) || 0,
    ts: Date.now(), mime: String(mime || 'image/jpeg'), bytes: buf.length,
  }), 'utf8');
  const head = Buffer.alloc(3);
  head.writeUInt8(PROTOCOL_VERSION, 0);
  head.writeUInt16BE(header.length, 1);
  return Buffer.concat([head, header, buf]);
}

/**
 * 解析入向二进制帧（v1 带头的 / 旧裸帧都认）。
 * @returns {{ok:true, header:object|null, payload:Buffer, legacy:boolean} | {ok:false, code:string, detail:string}}
 */
export function parseFrame(buf) {
  if (buf.length >= 4 && buf.readUInt8(0) === PROTOCOL_VERSION) {
    const hLen = buf.readUInt16BE(1);
    if (hLen > 0 && hLen <= 4096 && buf.length >= 3 + hLen) {
      let header = null;
      try { header = JSON.parse(buf.subarray(3, 3 + hLen).toString('utf8')); }
      catch (e) { return { ok: false, code: 'BAD_JSON', detail: `帧头解析失败：${e.message}` }; }
      if (header?.type !== 'frame') return { ok: false, code: 'BAD_PAYLOAD', detail: '帧头 type 不是 frame' };
      return { ok: true, header, payload: buf.subarray(3 + hLen), legacy: false };
    }
  }
  // 旧格式：整块就是图片
  return { ok: true, header: null, payload: buf, legacy: true };
}
