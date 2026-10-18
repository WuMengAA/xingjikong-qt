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

/**
 * 错误码体系（《全量设计文档》第七部分落地，2026-10-06）：
 * 格式 [模块2位][类型1位][编号3位]，模块 AU 认证 / DV 设备 / CM 指令 /
 * NT 网络 / ST 存储 / PV 权限 / SY 系统；类型 1 客户端 / 2 服务端 / 3 网络 / 4 业务。
 * 语义码 → 数字码 双向映射：对外主用数字码（ecode），语义码保留给旧客户端兼容。
 */
export const ECODES = {
  // 认证 AU
  AU1001: { sem: 'BAD_CREDENTIALS', msg: '账号或密码错误', http: 401 },
  AU1002: { sem: 'TOKEN_EXPIRED', msg: '登录已过期，请重新登录', http: 401 },
  AU1003: { sem: 'TOKEN_INVALID', msg: '认证失败', http: 401 },
  AU2001: { sem: 'ACCOUNT_DISABLED', msg: '账号已被禁用', http: 403 },
  AU2002: { sem: 'LOGIN_LIMITED', msg: '尝试次数过多，请稍后再试', http: 429 },
  // 设备 DV
  DV1001: { sem: 'DEVICE_NOT_FOUND', msg: '设备不存在', http: 404 },
  DV1002: { sem: 'DEVICE_DISABLED', msg: '该设备已被禁用', http: 403 },
  DV2001: { sem: 'REGISTER_FAILED', msg: '注册失败，请检查注册码', http: 400 },
  DV2002: { sem: 'FINGERPRINT_MISMATCH', msg: '设备验证失败', http: 403 },
  DV3001: { sem: 'DEVICE_OFFLINE', msg: '设备离线，无法操作', http: 409 },
  DV3002: { sem: 'HEARTBEAT_TIMEOUT', msg: '设备无响应', http: 408 },
  // 指令 CM
  CM1001: { sem: 'CMD_TYPE_UNSUPPORTED', msg: '不支持该操作', http: 400 },
  CM1002: { sem: 'CMD_PARAM_INVALID', msg: '参数错误', http: 400 },
  CM2001: { sem: 'CMD_EXEC_FAILED', msg: '操作失败', http: 500 },
  CM2002: { sem: 'CMD_TIMEOUT', msg: '操作超时', http: 504 },
  CM2003: { sem: 'CMD_REJECTED', msg: '设备拒绝执行', http: 409 },
  CM4001: { sem: 'CMD_NO_PERMISSION', msg: '您没有权限执行该操作', http: 403 },
  // 网络 NT
  NT3001: { sem: 'NET_CONNECT_FAILED', msg: '连接失败，正在重连', http: 502 },
  NT3002: { sem: 'NET_TIMEOUT', msg: '连接超时', http: 504 },
  NT3003: { sem: 'NET_DISCONNECTED', msg: '连接断开', http: 502 },
  NT3004: { sem: 'NET_SEND_FAILED', msg: '消息发送失败', http: 500 },
  // 权限 PV
  PV4001: { sem: 'NO_ACCESS', msg: '无权限访问', http: 403 },
  PV4002: { sem: 'NO_DEVICE_PERMISSION', msg: '无权限操作该设备', http: 403 },
  PV4003: { sem: 'ROLE_FORBIDDEN', msg: '角色不允许', http: 403 },
  // 系统 SY
  SY2001: { sem: 'INTERNAL_ERROR', msg: '系统内部错误', http: 500 },
};

/** 语义码 → 数字码（无映射则原样返回，兼容未编号的旧码）。 */
export function semToEcode(sem) {
  for (const [ec, def] of Object.entries(ECODES)) {
    if (def.sem === sem) return ec;
  }
  return sem; // 旧码未编号：保留语义名，两端按原样显示
}

/**
 * 统一错误负载（数字码主用 + 语义码兼容）：
 * { ecode:'CM4001', code:'CMD_NO_PERMISSION', message:'您没有权限执行该操作', http:403 }
 */
export function errorPayload(semOrEcode, detail) {
  let ecode = semOrEcode;
  let sem = semOrEcode;
  if (ECODES[semOrEcode]) { ecode = semOrEcode; sem = ECODES[semOrEcode].sem; }
  else { ecode = semToEcode(semOrEcode); }
  const def = ECODES[ecode] || null;
  const msg = def ? def.msg : (ERR[sem]?.msg || '未知错误');
  const p = { ecode, code: sem, message: detail ? `${msg}：${detail}` : msg };
  if (def?.http) p.http = def.http;
  return p;
}

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

/**
 * 宽松版帧解析（2026-10-18 · 远控统一 A）：v1 头 **type 不限**，非 v1 整块当 payload。
 * 画面帧请用 parseFrame（只认 type:'frame'）；vnc-data 等新类型走这里 —— 云端只
 * 按 header.uid 做字节中继，不解析、不重写信封。
 * @returns {{ok:true, header:object|null, payload:Buffer, legacy:boolean} | {ok:false, code:string, detail:string}}
 */
export function parseAnyFrame(buf) {
  if (buf.length >= 4 && buf.readUInt8(0) === PROTOCOL_VERSION) {
    const hLen = buf.readUInt16BE(1);
    if (hLen > 0 && hLen <= 4096 && buf.length >= 3 + hLen) {
      let header = null;
      try { header = JSON.parse(buf.subarray(3, 3 + hLen).toString('utf8')); }
      catch (e) { return { ok: false, code: 'BAD_JSON', detail: `帧头解析失败：${e.message}` }; }
      return { ok: true, header, payload: buf.subarray(3 + hLen), legacy: false };
    }
  }
  // 旧格式：整块当 payload（语音/广播的裸字节走这里）
  return { ok: true, header: null, payload: buf, legacy: true };
}
