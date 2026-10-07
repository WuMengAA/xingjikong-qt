// 设备接入票（agent ticket）单元回归测试（2026-10-07 固化）
//
// 作用：验证 ticket.js 的 verifyAgentTicket / mintAgentTicket 往返闭环与全部拒绝分支。
// 背景：设备票由站点 /api/device/bind 签发（登录态下给名下设备签票），
//   云端 verifyAgentTicket 验票放行——被控端「用星璃账号绑定」零手填通道的验票端。
// 覆盖：7 段格式识别、签名往返、HMAC 篡改、过期、fp 对照与大小写、段数混淆（vs 5 段 viewer 票）。
//
// ⚠️ 必须在 import ticket.js **之前**设置 CLOUD_VIEWER_SECRET：
//   config.js 在模块加载时读 env 并把 VIEWER_SECRET 固化成常量，之后再设 env 无效。
// 用法：node tests/unit-agent-ticket.mjs   （纯单元测试，不起云端）
// 返回码：0 全过；非 0 有失败。

// 先设密钥再动态 import（config.js 加载时固化 VIEWER_SECRET）
process.env.CLOUD_VIEWER_SECRET = 'unit-test-secret-not-for-prod';
const { verifyAgentTicket, mintAgentTicket, mintViewerTicket, verifyViewerTicket } =
  await import('../src/ticket.js');

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  ✅ ${name}${detail ? ' — ' + detail : ''}`); }
  else { fail++; console.log(`  ❌ ${name}${detail ? ' — ' + detail : ''}`); }
}

const FP = 'dev-fp-abc123';      // 8-64 位小写字母数字_-
const UID = 'user1-dev-abc123';
const OWNER = 'zhangsan';

// 0) 签发往返闭环：mint → verify 必须通过，且 uid/fp/owner 原样解出
let m = mintAgentTicket(UID, FP, OWNER);
check('签发成功', m.ok === true, m.reason || `exp=${m.exp}`);
let v = verifyAgentTicket(m.ticket, FP);
check('往返验票通过', v.ok === true, v.reason || `mode=${v.mode}`);
check('验票 mode=agent-ticket', v.mode === 'agent-ticket', v.mode);
check('解出设备 uid', v.uid === UID, v.uid);
check('解出绑定者 owner', v.owner === OWNER, v.owner);
check('票含 7 段', String(m.ticket).split('.').length === 7, String(m.ticket).split('.').length);

// 1) fp 大小写不敏感（注册端可能上报大写）
check('fp 大小写不敏感', verifyAgentTicket(m.ticket, FP.toUpperCase()).ok === true);

// 2) fp 不匹配必须拒绝（票被拷到别的机器）
let bad = verifyAgentTicket(m.ticket, 'someone-else-fp');
check('fp 不匹配被拒', bad.ok === false, bad.reason);

// 3) 篡改签名位（伪造）→ 签名校验失败
const parts = m.ticket.split('.');
parts[6] = parts[6].replace(/^./, c => (c === 'A' ? 'B' : 'A'));
let forged = verifyAgentTicket(parts.join('.'), FP);
check('篡改签名被拒', forged.ok === false, forged.reason);

// 4) 篡改载荷（改 uid，不重签）→ 签名校验失败
const p4 = m.ticket.split('.');
p4[2] = Buffer.from('attacker-dev-0000', 'utf8').toString('base64url');
check('篡改载荷被拒', verifyAgentTicket(p4.join('.'), FP).ok === false);

// 5) 过期票 → 拒绝（ttl 设为负数）
const expired = mintAgentTicket(UID, FP, OWNER, -1000);
check('过期票被拒', verifyAgentTicket(expired.ticket, FP).ok === false);

// 6) 格式错：缺段
check('段数不足被拒', verifyAgentTicket('v1.a.b.c.d', FP).ok === false);
check('空票被拒', verifyAgentTicket('', FP).ok === false);
check('undefined 票被拒', verifyAgentTicket(undefined, FP).ok === false);

// 7) 段数混淆防线：viewer 票（5 段）不能被当成设备票验过
const vt = mintViewerTicket('viewer-u1');
check('viewer 票签发成功', vt.ok === true, vt.reason || '');
const cross = verifyAgentTicket(vt.ticket, FP);
check('viewer 票不能被当设备票验过', cross.ok === false, cross.reason);
check('设备票不能当 viewer 票验过', verifyViewerTicket(m.ticket).ok === false);

// 8) 票里签了 fp，被控端却不报 fp → 拒绝。
//    这是「拿到别人的票但不报指纹」这条绕路的堵口：不报不能当不知道。
let nofp = verifyAgentTicket(m.ticket, '');
check('票绑定了 fp 却不报 fp 被拒', nofp.ok === false, nofp.reason);

// 8b) 兼容：票里**本来就没有 fp**（早期/无指纹签发）→ 不做对照，签名与过期仍照验。
const legacy = mintAgentTicket(UID, '', OWNER);
check('无 fp 的票签发成功', legacy.ok === true);
check('无 fp 的票验票通过（不做指纹对照）', verifyAgentTicket(legacy.ticket, '').ok === true);

// 9) 抹掉 fp 段企图绕过指纹绑定 → 载荷已变、签名对不上，拒绝。
// 这是防篡改的另一面：攻击者改不了 fp 字段而不被签名校验抓住。
const p9 = m.ticket.split('.');
p9[3] = '';
check('抹掉 fp 段被签名校验拒绝', verifyAgentTicket(p9.join('.'), FP).ok === false);

console.log(`\n══ 结果: ${pass} 通过 / ${fail} 失败 ══`);
process.exit(fail === 0 ? 0 : 1);
