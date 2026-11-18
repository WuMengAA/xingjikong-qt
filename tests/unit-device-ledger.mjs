// 设备身份账本单测（2026-11-18 · 被控端主体地位）。
//
// 覆盖四件事，每件都是改造前**会失败**的：
//   ① 落盘：写入后关掉重开，身份还在（改造前 knownDevices 是纯内存 Map，重启即空）；
//   ② 就地写：known.classCode = null 这类「不是 set 的写」也要落盘（解绑后重启班不能回来）；
//   ③ 合并语义：寄存器自报的空值/主机名不能顶掉云端已知的班级与人工 pinned 名字；
//   ④ 客户端档案：initialized 四标志 + registerCount，且查无档案时**不凭空造**。

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const dir = mkdtempSync(join(tmpdir(), 'device-ledger-'));
process.env.CLOUD_DEVICE_LEDGER_DB_FILE = join(dir, 'ledger.db');

const {
  initDeviceLedger, closeDeviceLedger, hydrateKnown, LedgerMap,
  clientIdOf, clientProfile, ledgerStats, ledgerStatus, setNamePin, getNamePin,
} = await import('../src/device-ledger.js');

let pass = 0;
const fails = [];
function ok(name, fn) {
  try { fn(); pass++; } catch (e) { fails.push(name + ' → ' + (e && e.message ? e.message : e)); }
}

const fresh = () => {
  closeDeviceLedger();
  rmSync(join(dir, 'ledger.db'), { force: true });
  initDeviceLedger(join(dir, 'ledger.db'));
};

/* ---------- ① 落盘与重载 ---------- */
ok('账本能打开且不降级', () => {
  fresh();
  const st = ledgerStatus();
  assert.equal(st.ok, true);
  assert.equal(st.degraded, false);
});

ok('写入后重开仍在（改造前这里必然是 0 条）', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  m.set('class_xlzx_2028_08', {
    uid: 'class_xlzx_2028_08', name: '高一8班讲台', classCode: 'xlzx_2028_08',
    tenantId: 'xlzx', role: 'classroom', version: '0.6.74', env: 'prod', lastSeen: 1763000000000,
  });
  closeDeviceLedger();
  initDeviceLedger(join(dir, 'ledger.db'));
  const m2 = hydrateKnown(new LedgerMap());
  const rec = m2.get('class_xlzx_2028_08');
  assert.equal(rec.name, '高一8班讲台');
  assert.equal(rec.classCode, 'xlzx_2028_08');
  assert.equal(rec.tenantId, 'xlzx');
  assert.equal(rec.role, 'classroom');
});

ok('客户端编号由 uid 派生且稳定', () => {
  assert.equal(clientIdOf('abc'), clientIdOf('abc'));
  assert.notEqual(clientIdOf('abc'), clientIdOf('abd'));
  assert.match(clientIdOf('abc'), /^dev-[0-9a-f]{10}$/);
});

/* ---------- ② 就地写（Proxy） ---------- */
ok('get() 返回的记录就地改字段也会落盘', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  m.set('u-inplace', { uid: 'u-inplace', name: '机1', classCode: 'c_000001', tenantId: 't1', role: 'classroom', lastSeen: 1 });
  const rec = m.get('u-inplace');
  rec.classCode = 'c_000002';
  closeDeviceLedger();
  initDeviceLedger(join(dir, 'ledger.db'));
  assert.equal(hydrateKnown(new LedgerMap()).get('u-inplace').classCode, 'c_000002');
});

ok('就地置 null = 显式清空（解绑后重启班不许回来）', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  m.set('u-unbind', { uid: 'u-unbind', name: 'n', classCode: 'c_000003', tenantId: 't1', role: 'classroom', lastSeen: 1 });
  m.get('u-unbind').classCode = null;
  closeDeviceLedger();
  initDeviceLedger(join(dir, 'ledger.db'));
  assert.equal(hydrateKnown(new LedgerMap()).get('u-unbind').classCode, null);
});

/* ---------- ③ 合并语义 ---------- */
ok('重连自报主机名不能顶掉人工钉住的名字', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  m.set('u-pin', { uid: 'u-pin', name: '讲台', classCode: 'c_000004', tenantId: 't1', role: 'classroom', lastSeen: 1 });
  setNamePin('u-pin', '讲台（已改名）');
  m.set('u-pin', { uid: 'u-pin', name: 'DESKTOP-ABC', classCode: 'c_000004', tenantId: 't1', role: 'classroom', lastSeen: 2 });
  assert.equal(m.get('u-pin').name, '讲台（已改名）');
  assert.equal(getNamePin('u-pin'), '讲台（已改名）');
});

ok('重连自报空 classCode 不能把云端已知班级清空', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  m.set('u-keep', { uid: 'u-keep', name: 'n', classCode: 'c_000005', tenantId: 't1', role: 'classroom', lastSeen: 1 });
  m.set('u-keep', { uid: 'u-keep', name: 'n2', classCode: null, tenantId: 't1', role: 'classroom', lastSeen: 3 });
  assert.equal(m.get('u-keep').classCode, 'c_000005');
});

ok('同一对象重复 set 不算一次接入（registerCount 不虚高）', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  const obj = { uid: 'u-count', name: 'n', classCode: 'c_000006', tenantId: 't1', role: 'classroom', lastSeen: 1 };
  m.set('u-count', obj);
  m.set('u-count', obj);
  m.set('u-count', obj);
  assert.equal(clientProfile('u-count').registerCount, 1);
  m.set('u-count', { ...obj, lastSeen: 9 });
  assert.equal(clientProfile('u-count').registerCount, 2);
});

ok('删除设备会同时删掉账本行', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  m.set('u-del', { uid: 'u-del', name: 'n', classCode: '', tenantId: 't1', role: 'classroom', lastSeen: 1 });
  m.delete('u-del');
  closeDeviceLedger();
  initDeviceLedger(join(dir, 'ledger.db'));
  assert.equal(hydrateKnown(new LedgerMap()).size, 0);
});

/* ---------- ④ 客户端档案 ---------- */
ok('完整设备 initialized.complete 为真', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  m.set('u-full', {
    uid: 'u-full', name: '讲台', classCode: 'c_000007', tenantId: 't1',
    role: 'classroom', version: '0.6.74', env: 'prod', lastSeen: Date.now(),
  });
  const p = clientProfile('u-full');
  assert.equal(p.initialized.complete, true);
  assert.equal(p.initialized.class, true);
  assert.equal(p.online, false);
  assert.match(p.clientId, /^dev-/);
});

ok('缺班级 → partial，且 initialized 指出缺哪项', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  m.set('u-part', { uid: 'u-part', name: '讲台', classCode: '', tenantId: 't1', role: 'classroom', lastSeen: Date.now() });
  const p = clientProfile('u-part');
  assert.equal(p.initialized.complete, false);
  assert.equal(p.initialized.class, false);
  assert.equal(p.initialized.name, true);
});

ok('查无档案返回 null（不凭空造记录）', () => {
  fresh();
  assert.equal(clientProfile('never-seen'), null);
});

ok('ledgerStats 统计完成度与租户分布', () => {
  fresh();
  const m = hydrateKnown(new LedgerMap());
  m.set('s1', { uid: 's1', name: 'a', classCode: 'c1', tenantId: 't1', role: 'classroom', lastSeen: 1 });
  m.set('s2', { uid: 's2', name: 'b', classCode: '', tenantId: 't1', role: 'classroom', lastSeen: 1 });
  const st = ledgerStats();
  assert.equal(st.total, 2);
  assert.equal(st.complete, 1);
  assert.equal(st.partial, 1);
  assert.equal(st.tenants.t1, 2);
});

ok('未钉住时 getNamePin 返回空串', () => {
  fresh();
  assert.equal(getNamePin('nobody'), '');
});

/* ---------- 收尾 ---------- */
closeDeviceLedger();
try { rmSync(dir, { recursive: true, force: true }); } catch { /* 临时目录删不掉无所谓 */ }

if (fails.length) {
  console.error('\n[device-ledger] FAIL ' + fails.length);
  for (const f of fails) console.error('  ✗ ' + f);
  process.exit(1);
}
console.log('[device-ledger] PASS ' + pass + ' / FAIL 0');
