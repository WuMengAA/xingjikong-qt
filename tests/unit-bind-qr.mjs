// 云端单测：二维码矩阵 + 绑定票据（node tests/unit-bind-qr.mjs）
//
// 为什么这份测试长这样：
//   QR 编码器是典型的"错了也能出图"的组件 —— 图看着正常，手机就是扫不出来，
//   而且线上没有反馈渠道（老师只会说"扫不了"）。所以这里不能只测"函数不抛异常"，
//   必须把**每一个模块**钉死：下面的 sha256 是对每个 (内容, 版本, 掩码) 组合的
//   完整矩阵做哈希，值是 2026-10-20 用业界最常用的 node-qrcode 逐像素比对
//   （248 个用例全一致）之后固化下来的黄金值。改了编码器就必须重新对拍，
//   否则这里会红。
//
// 另一条纪律：溢出必须报错。指定版本装不下内容时静默截断会产出扫不出的码，
// 比"抛异常"危险得多。

import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {
  makeQrMatrix, issueDeviceBind, issueClassBind, getTicket, markBound,
  pollDeviceBind, ticketStats, BIND_QR_LIMITS,
} from '../src/bind-qr.js';

let passed = 0;
const fails = [];
function test(name, fn) {
  try { fn(); passed++; console.log(`  ✓ ${name}`); }
  catch (e) { fails.push([name, e]); console.log(`  ✗ ${name}\n      ${e.message}`); }
}

const LONG = 'https://control.245959623.xyz/bind?c=Z9K4MP&t=1729400000&v=0.6.46';

console.log('\n[1] QR 矩阵黄金用例（与 node-qrcode 逐像素比对后固化）');
const GOLDEN = [
  { text: 'A7X9Q2', version: 1, mask: 0, size: 21, sha256: 'e514db7f3aa85061e9d5548dfbd4395e1d317b9e810cd699758670e1648555b7' },
  { text: 'A7X9Q2', version: 1, mask: 5, size: 21, sha256: 'e672a13f2a5a2441cacea1a4c729dd39637a72b42df037bfdac3dccc33c4b4c2' },
  { text: 'A7X9Q2', version: 2, mask: 3, size: 25, sha256: 'b7daf70c9676c0816707bf62478e70b661a99d8219f6f835ae04282c5b9e2f5a' },
  { text: 'https://control.245959623.xyz/bind?c=A7X9Q2', version: 4, mask: 2, size: 33, sha256: 'c3c868e96dace4d004c493a853253758e5a3c55e510f752f63738b9d719bb6f1' },
  { text: 'https://control.245959623.xyz/bind?c=A7X9Q2', version: 4, mask: 7, size: 33, sha256: '444c7db93f3bea79e5bdcce02bb8f875413f899a8e8a12df7997f016d5d691e1' },
  { text: LONG, version: 5, mask: 1, size: 37, sha256: '86619e8d2b3790451280ebdc419dcf38c02688f67a6dc612bb815ef08eb4aea3' },
  { text: LONG, version: 7, mask: 3, size: 45, sha256: 'a59b6ffd65999360bdc075af72f9ab6417b188aaf29dc74c6d2a0bf49aca058a' },
  { text: LONG, version: 10, mask: 6, size: 57, sha256: '836fd31278354b51a9be4df42e69493167ee9c574b595f125ad75079268b26eb' },
];
for (const g of GOLDEN) {
  test(`v${g.version} mask${g.mask} · ${g.text.slice(0, 24)}…`, () => {
    const m = makeQrMatrix(g.text, { version: g.version, mask: g.mask });
    assert.equal(m.size, g.size, '尺寸');
    assert.equal(m.rows.length, g.size, '行数');
    for (const row of m.rows) assert.match(row, /^[01]+$/, '每行只能是 0/1');
    const sha = crypto.createHash('sha256').update(m.rows.join('\n')).digest('hex');
    assert.equal(sha, g.sha256, '矩阵与黄金值不符（编码器被改动？必须重新与 node-qrcode 对拍）');
  });
}

console.log('\n[2] 自动选版 / 溢出保护');
test('自动选到能装下的最小版本（44 字节 → v4）', () => {
  assert.equal(makeQrMatrix('https://control.245959623.xyz/bind?c=A7X9Q2').version, 4);
});
test('短内容选 v1', () => {
  assert.equal(makeQrMatrix('A7X9Q2').version, 1);
});
test('指定版本装不下 → 抛错（不许静默溢出）', () => {
  assert.throws(() => makeQrMatrix(LONG, { version: 2 }), /装不下/);
});
test('超长内容 → 抛错', () => {
  assert.throws(() => makeQrMatrix('x'.repeat(400)), /内容过长/);
});
test('不支持的版本 → 抛错', () => {
  assert.throws(() => makeQrMatrix('A7X9Q2', { version: 40 }), /不支持/);
});
test('自动掩码稳定（同内容连跑 3 次同结果）', () => {
  const a = makeQrMatrix(LONG);
  const b = makeQrMatrix(LONG);
  const c = makeQrMatrix(LONG);
  assert.equal(a.mask, b.mask);
  assert.equal(b.mask, c.mask);
  assert.ok(a.mask >= 0 && a.mask <= 7, '掩码必须在 0..7');
  assert.equal(a.rows.join('\n'), c.rows.join('\n'));
});

console.log('\n[3] 绑定码格式');
test('6 位、只含无歧义字符（无 I/O/0/1）', () => {
  for (let i = 0; i < 40; i++) {
    const r = issueDeviceBind(`uid-${i}`);
    assert.equal(r.ok, true);
    assert.equal(r.ticket.code.length, BIND_QR_LIMITS.CODE_LEN);
    assert.match(r.ticket.code, /^[ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{6}$/);
  }
});
test('缺 uid → 报错不返回假成功', () => {
  const r = issueDeviceBind('');
  assert.equal(r.ok, false);
  assert.match(r.error, /设备标识/);
});

console.log('\n[4] 设备自助码完整生命周期');
test('开码 → 待确认 → 手机确认 → 设备轮询拿到班级', () => {
  const uid = 'dev-lifecycle';
  const { ticket } = issueDeviceBind(uid);
  assert.equal(getTicket(ticket.code).state, 'pending');

  const pending = pollDeviceBind(ticket.code, uid);
  assert.equal(pending.ok, true);
  assert.equal(pending.state, 'pending');
  assert.equal(pending.className, '');

  markBound(ticket.code, { classCode: 'class_0801', className: '高二8班' });

  const done = pollDeviceBind(ticket.code, uid);
  assert.equal(done.ok, true);
  assert.equal(done.state, 'bound');
  assert.equal(done.classCode, 'class_0801');
  assert.equal(done.className, '高二8班');
});
test('重复确认幂等（不覆盖第一次的班级）', () => {
  const { ticket } = issueDeviceBind('dev-idem');
  markBound(ticket.code, { classCode: 'A1', className: '一班' });
  const again = markBound(ticket.code, { classCode: 'B2', className: '二班' });
  assert.equal(again.ok, true);
  assert.equal(again.already, true);
  assert.equal(pollDeviceBind(ticket.code, 'dev-idem').className, '一班');
});
test('别的设备拿不到这张票', () => {
  const { ticket } = issueDeviceBind('dev-owner');
  const r = pollDeviceBind(ticket.code, 'dev-thief');
  assert.equal(r.ok, false);
  assert.equal(r.state, 'invalid');
  assert.match(r.error, /不属于这台设备/);
});
test('同一设备重新开码 → 旧码立即失效（换一张必须真的换）', () => {
  const uid = 'dev-rotate';
  const first = issueDeviceBind(uid).ticket;
  const second = issueDeviceBind(uid).ticket;
  assert.notEqual(first.code, second.code);
  assert.equal(getTicket(first.code), null, '旧票应被作废');
  assert.equal(pollDeviceBind(first.code, uid).ok, false);
  assert.equal(pollDeviceBind(second.code, uid).state, 'pending');
});
test('过期票 → 查不到，轮询报无效', () => {
  const { ticket } = issueDeviceBind('dev-expire', { ttlMs: 1 });
  const t0 = Date.now();
  while (Date.now() - t0 < 5) { /* 等 TTL 过期 */ }
  assert.equal(getTicket(ticket.code), null);
  assert.equal(pollDeviceBind(ticket.code, 'dev-expire').state, 'invalid');
});
test('小写/带空格输入也能认（老师手抄不会因为大小写失败）', () => {
  const { ticket } = issueDeviceBind('dev-lower');
  const messy = ` ${ticket.code.toLowerCase()} `;
  assert.equal(pollDeviceBind(messy, 'dev-lower').state, 'pending');
});
test('不存在的码 → invalid，不抛异常', () => {
  assert.equal(pollDeviceBind('ZZZZZZ', 'x').state, 'invalid');
  assert.equal(getTicket(''), null);
});

console.log('\n[5] 管理员班级码（码里带班级）');
test('开码即带班级，确认后设备拿到同一个班', () => {
  const r = issueClassBind('class_0802', '高二9班');
  assert.equal(r.ok, true);
  assert.equal(r.ticket.kind, 'class');
  assert.equal(r.ticket.classCode, 'class_0802');
  assert.equal(r.ticket.className, '高二9班');
  // 管理员码还没被任何设备认领
  markBound(r.ticket.code, { classCode: 'class_0802', className: '高二9班', uid: 'dev-claim' });
  const p = pollDeviceBind(r.ticket.code, 'dev-claim');
  assert.equal(p.state, 'bound');
  assert.equal(p.className, '高二9班');
});
test('缺班级码 → 报错', () => {
  const r = issueClassBind('', '');
  assert.equal(r.ok, false);
  assert.match(r.error, /班级码/);
});

console.log('\n[6] 运维统计');
test('ticketStats 计数与状态一致', () => {
  const before = ticketStats();
  const a = issueDeviceBind('stat-1').ticket;
  issueDeviceBind('stat-2');
  markBound(a.code, { classCode: 'X1', className: '统计班' });
  const after = ticketStats();
  assert.ok(after.total >= before.total + 2);
  assert.ok(after.bound >= 1);
  assert.ok(after.pending >= 1);
});

console.log(`\n单元测试：${passed} 通过 / ${fails.length} 失败`);
if (fails.length) {
  for (const [n, e] of fails) console.log(`  FAIL ${n}: ${e.stack || e.message}`);
  process.exit(1);
}
console.log('=== ALL PASS ===');
