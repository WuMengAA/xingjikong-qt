// 验证 repairs.js updateRepair 修复（2026-10-08）
// 运行：cd cloud-ws && node tests/test-repair-fix.mjs
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { _resetRepairs, useRepairFile, loadRepairs, createRepair, updateRepair } from '../src/repairs.js';

let pass = 0, fail = 0;
const ok = (name, cond, extra) => { if (cond) { pass++; console.log(`  ✅ ${name}`); } else { fail++; console.log(`  ❌ ${name}`, extra || ''); } };

const tmp = mkdtempSync(join(tmpdir(), 'repair-fix-'));
useRepairFile(join(tmp, 'repairs.json'));
_resetRepairs();
loadRepairs();

// === Bug 1: 无效 status 静默忽略 → 应返回 400 ===
const c1 = createRepair({ uid: 'test01', symptom: '屏幕不亮' });
ok('创建票', !!c1.ticket, c1);

let r = updateRepair(c1.ticket.id, { status: 'invalid' });
ok('无效 status 返回 400', r.http === 400, r);

// === Bug 2: 空操作当成功 → 应返回 400 ===
r = updateRepair(c1.ticket.id, {});
ok('空对象返回 400', r.http === 400, r);

r = updateRepair(c1.ticket.id, { status: null, note: null });
ok('全 null 返回 400', r.http === 400, r);

// === 正常路径（成功返回 {ticket}，无 http 字段）===
r = updateRepair(c1.ticket.id, { note: 'hello' });
ok('只改备注', !r.error && r.ticket.note === 'hello', r);

r = updateRepair(c1.ticket.id, { status: 'processing' });
ok('只改状态', !r.error && r.ticket.status === 'processing', r);

r = updateRepair(c1.ticket.id, { status: 'closed' });
ok('关闭设置 closedAt', !r.error && !!r.ticket.closedAt, r);

r = updateRepair(c1.ticket.id, { note: 'done', status: 'open' });
ok('重新打开清除 closedAt', !r.error && !r.ticket.closedAt, r);

// === 备注清空 ===
r = updateRepair(c1.ticket.id, { note: '' });
ok('备注清空', !r.error && r.ticket.note === '', r);

// === 不存在的票 ===
r = updateRepair('nonexistent', { status: 'processing' });
ok('不存在的票返回 404', r.http === 404, r);

// === 备注过长 ===
r = updateRepair(c1.ticket.id, { note: 'x'.repeat(501) });
ok('备注超 500 返回 400', r.http === 400, r);

rmSync(tmp, { recursive: true, force: true });
console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
