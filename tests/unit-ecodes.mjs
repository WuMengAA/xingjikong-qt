// 错误码体系单元回归测试（2026-10-07 固化，v2 按真实 API 语义）
//
// 作用：验证 protocol.js 的 ECODES 表与 errorPayload/semToEcode 归一化。
// 覆盖 9f78901 的核心承诺：数字码主用 + 语义码兼容。
//
// 用法：node tests/unit-ecodes.mjs   （纯单元测试，不起云端）
// 返回码：0 全过；非 0 有失败。

import { ECODES, semToEcode, errorPayload, errPayload } from '../src/protocol.js';

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  \u2705 ${name}${detail ? ' \u2014 ' + detail : ''}`); }
  else { fail++; console.log(`  \u274c ${name}${detail ? ' \u2014 ' + detail : ''}`); }
}

// 1) 码表键格式：{ecode: {sem, msg, http}}，ecode 形如 AU1001（模块2+数字4）
let formatOk = true, badKey = '';
for (const ec of Object.keys(ECODES)) {
  if (!/^[A-Z]{2}\d{4}$/.test(ec) || typeof ECODES[ec].sem !== 'string' || !ECODES[ec].msg) {
    formatOk = false; badKey = ec; break;
  }
}
check('ECODES 键格式 + 每条含 sem/msg', formatOk, badKey ? `坏: ${badKey}` : `${Object.keys(ECODES).length} 条`);

// 2) 关键语义码 → 数字码
check('DEVICE_OFFLINE → DV3001', semToEcode('DEVICE_OFFLINE') === 'DV3001');
check('HEARTBEAT_TIMEOUT → DV3002', semToEcode('HEARTBEAT_TIMEOUT') === 'DV3002');
check('CMD_NO_PERMISSION → CM4001', semToEcode('CMD_NO_PERMISSION') === 'CM4001');
check('TOKEN_INVALID → AU1003', semToEcode('TOKEN_INVALID') === 'AU1003');

// 3) errorPayload(语义码)：输出 ecode+code+message+http
const p = errorPayload('DEVICE_OFFLINE', '设备不在线');
check('errorPayload 语义码 → ecode DV3001', p.ecode === 'DV3001', p.ecode);
check('errorPayload 保留语义码 code', p.code === 'DEVICE_OFFLINE', p.code);
check('errorPayload 有 message', typeof p.message === 'string' && p.message.includes('设备离线'), p.message);
check('errorPayload 有 http 409', p.http === 409, String(p.http));

// 4) errorPayload(数字码)：ecode 保留，code 转成对应语义码
const p2 = errorPayload('DV3001', '直接码');
check('errorPayload 数字码 → ecode 保留', p2.ecode === 'DV3001', p2.ecode);
check('errorPayload 数字码 → code 为语义码', p2.code === 'DEVICE_OFFLINE', p2.code);

// 5) 未编号语义码兼容：原样返回
check('未编号码原样返回', semToEcode('LEGACY_OLD_CODE') === 'LEGACY_OLD_CODE', semToEcode('LEGACY_OLD_CODE'));

// 6) 旧 errPayload 接口仍可用（旧客户端兼容）
const e3 = errPayload('BAD_TOKEN', 'token 坏');
check('errPayload 旧接口可用', !!e3 && e3.code === 'BAD_TOKEN', e3.code);

// 7) 双向自洽：遍历 ECODES，semToEcode(每条.sem) == 该键
let allOk = true, badSem = '';
for (const [ec, def] of Object.entries(ECODES)) {
  if (semToEcode(def.sem) !== ec) { allOk = false; badSem = `${def.sem}→${semToEcode(def.sem)} 期望 ${ec}`; break; }
}
check('ECODES 双向自洽（sem 反查全部命中）', allOk, badSem || `${Object.keys(ECODES).length} 键`);

console.log(`\n\u2550\u2550 \u7ed3\u679c: ${pass} \u901a\u8fc7 / ${fail} \u5931\u8d25 \u2550\u2550`);
process.exit(fail === 0 ? 0 : 1);