// 班级码「扫码选设备直接绑」· 纯逻辑回归 + 反向验证（2026-11-05）
//
// 守的契约（用户 m01202 裁决「支持扫码选设备直接绑」）：
//   · /api/bind/info 只回**同租户、未分班**的已知设备，且字段脱敏（uid/name/online）；
//   · 手机挑的设备必须来自云端权威 listDevices()，不接受请求体拼出来的对象；
//   · 六道拒绝：无 uid / 设备不存在 / 已分班 / 跨租户 / 票已用 / 票过期 / 非班级票；
//   · 落盘仍走 bindDeviceCore 唯一核心，班级与租户都从票里取，绝不读请求体。
//
// 反向验证（探针断言铁律）：把 classScanDecide 的「已分班」「跨租户」两道判据抽掉，
// 用 data: URL 模块加载坏版本（零仓库写入），断言必须变红；干净版全绿。
// 坏版本**只跑纯判定层**，不跑会落盘的 classScanBind —— 反向验证不该有生产副作用。
//
// 跑法：node tests/regression-class-scan.mjs

import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const SRC = join(__dirname, '..', 'src');
const BIND_HTML = join(SRC, 'bind.html');
const BIND_API = join(SRC, 'bind-api.js');

/* ⚠️ 数据目录隔离必须在**任何 ../src/* 模块被加载之前**完成：config.js 在模块
 * 评估期就把 CLOUD_EVENTS_FILE / CLOUD_CLASS_DIR_DB_FILE 等常量读死了。
 * 静态 import 会被 hoist 到所有语句之前 ⇒ 这里一律用动态 import。
 * 否则 classScanBind 万一跑进 bindDeviceCore，会往生产 events.log / class-dir.db 写测试假数据。 */
const WAR = mkdtempSync(join(tmpdir(), 'class-scan-'));
process.env.CLOUD_EVENTS_FILE = join(WAR, 'events.log');
process.env.CLOUD_CLASS_DIR_DB_FILE = join(WAR, 'class-dir.db');
process.env.CLOUD_QUEUE_DB_FILE = join(WAR, 'instruction-queue.db');
process.env.CLOUD_DATA_DIR = WAR;

const { initTenants, registerTenant, closeTenants, DEFAULT_TENANT } = await import('../src/tenant.js');
const { classScanCandidates, classScanDecide, classScanBind, BIND_CHANNELS } = await import('../src/bind-api.js');

let PASS = 0; let FAIL = 0;
function ok(name, fn) {
  try { fn(); PASS++; console.log(`  ok   ${name}`); }
  catch (e) { FAIL++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

/* ---------- 隔离租户表：让「跨租户」判据真的能区分两个学校 ---------- */
initTenants(join(WAR, 'tenants.json'));
const reg = registerTenant({ id: 'tn_school_b', name: '乙学校' });
assert.ok(reg.ok, `登记测试租户失败：${reg.error}`);
const TENANT_B = reg.tenant.id;
const TENANT_A = DEFAULT_TENANT;
assert.notEqual(TENANT_B, TENANT_A);

const now = Date.now();
const TICKET = {
  code: 'ABC123', kind: 'class', uid: '', classCode: 'cls-scan01',
  className: '高二8班', tenantId: TENANT_B, state: 'pending',
  createdAt: now - 1000, expireAt: now + 60_000, boundAt: 0,
};
const DEVICES = [
  { uid: 'D_FREE_B', name: '乙校-空闲', classCode: null, tenantId: TENANT_B, online: true },
  { uid: 'D_BOUND', name: '乙校-已分班', classCode: 'cls-old', tenantId: TENANT_B, online: true },
  { uid: 'D_OTHER', name: '甲校-空闲', classCode: null, tenantId: TENANT_A, online: true },
  { uid: 'D_OFFLINE', name: '乙校-离线', classCode: null, tenantId: TENANT_B, online: false },
];

/**
 * 一组可重复跑的探针；返回每条是否通过。clean 必须全 true，bad 必须有红。
 * 每条都用 t() 包住：坏版本抽掉安全判据后若跑到落盘层会抛异常（class-dir 未初始化），
 * 异常**本身就是「坏版本放行了本该拒的设备」的信号**，计为红，绝不让逃逸中断整轮。
 * includeCore=false 时不跑会落盘的 classScanBind（反向验证专用，零副作用）。
 */
function probes(mod, includeCore = true) {
  const t = (fn) => { try { return fn(); } catch { return false; } };
  const cands = t(() => mod.classScanCandidates(DEVICES, TICKET));
  const g = (tk, uid, dev) => t(() => mod.classScanDecide(tk, uid, dev, now));
  const out = {
    '渠道已登记 class-scan': t(() => mod.BIND_CHANNELS.includes('class-scan')),
    '候选只含同租户未分班': cands && cands.length === 2 && cands.every((x) => ['D_FREE_B', 'D_OFFLINE'].includes(x.uid)),
    '候选字段脱敏': cands && cands.every((x) => Object.keys(x).sort().join(',') === 'name,online,uid'),
    '已绑票不给候选': t(() => mod.classScanCandidates(DEVICES, { ...TICKET, state: 'bound' }).length === 0),
    '设备票不给候选': t(() => mod.classScanCandidates(DEVICES, { ...TICKET, kind: 'device' }).length === 0),
    '空闲设备放行': g(TICKET, 'D_FREE_B', DEVICES[0])?.ok === true,
    '缺 uid 拒': g(TICKET, '', DEVICES[0])?.code === 'NO_UID',
    '未知设备拒': g(TICKET, 'D_GHOST', undefined)?.code === 'DEVICE_UNKNOWN',
    'uid 与权威记录不符拒': g(TICKET, 'D_FREE_B', DEVICES[2])?.code === 'DEVICE_UNKNOWN',
    '已分班设备拒': g(TICKET, 'D_BOUND', DEVICES[1])?.code === 'DEVICE_BOUND',
    '跨租户设备拒': g(TICKET, 'D_OTHER', DEVICES[2])?.code === 'CROSS_TENANT',
    '已用票拒': g({ ...TICKET, state: 'bound' }, 'D_FREE_B', DEVICES[0])?.code === 'TICKET_USED',
    '过期票拒': g({ ...TICKET, expireAt: now - 1 }, 'D_FREE_B', DEVICES[0])?.code === 'TICKET_EXPIRED',
    '非班级票拒': g({ ...TICKET, kind: 'device' }, 'D_FREE_B', DEVICES[0])?.code === 'BAD_TICKET',
  };
  if (includeCore) {
    out['classScanBind 拦已分班'] = t(() => mod.classScanBind(TICKET, 'D_BOUND', DEVICES).code === 'DEVICE_BOUND');
    out['classScanBind 拦跨租户'] = t(() => mod.classScanBind(TICKET, 'D_OTHER', DEVICES).code === 'CROSS_TENANT');
    out['classScanBind 拦未知设备'] = t(() => mod.classScanBind(TICKET, 'D_GHOST', DEVICES).code === 'DEVICE_UNKNOWN');
  }
  return out;
}

console.log('班级码扫码选设备 · 纯逻辑回归 + 反向验证');

const clean = probes({ classScanCandidates, classScanDecide, classScanBind, BIND_CHANNELS });
for (const [name, good] of Object.entries(clean)) ok(name, () => assert.equal(good, true, name));

/* ---------- 静态接线：bind.html 必须真的把 uid 发上去 ---------- */
const html = readFileSync(BIND_HTML, 'utf8');
ok('bind.html 有设备选择容器', () => assert.match(html, /id="devicePickWrap"/));
ok('bind.html 有设备列表容器', () => assert.match(html, /id="deviceList"/));
ok('bind.html 对 kind=class 分支存在', () => assert.match(html, /state\.kind === 'class'/));
ok('bind.html 提交时带 body.uid', () => assert.match(html, /body\.uid = state\.selectedDevice/));
ok('bind.html 不再用 needsDeviceInput 隐藏确认按钮', () => assert.doesNotMatch(html, /needsDeviceInput/));
ok('bind.html 保留 6 位码兜底入口', () => assert.match(html, /el\('bigCode'\)\.style\.display = 'block'/));

/* ---------- 反向验证：抽掉两道安全判据，断言必须变红 ---------- */
function badModule() {
  let src = readFileSync(BIND_API, 'utf8');
  // 把相对 import 改写成绝对 file: URL，才能塞进 data: 模块。
  src = src.replace(/from '\.\/([^']+)'/g, (_m, p) => `from '${pathToFileURL(join(SRC, p)).href}'`);
  const a = `  if (device.classCode) return fail('DEVICE_BOUND', '这台设备已经属于别的班了');\n`;
  const b = `  if (normalizeTenantId(device.tenantId) !== normalizeTenantId(ticket.tenantId)) {\n    return fail('CROSS_TENANT', '这台设备不在本班所属的学校里');\n  }\n`;
  assert.ok(src.includes(a), '反向验证锚点 A 没命中（源码已变，请更新探针）');
  assert.ok(src.includes(b), '反向验证锚点 B 没命中（源码已变，请更新探针）');
  src = src.replace(a, '').replace(b, '');
  return `data:text/javascript;base64,${Buffer.from(src, 'utf8').toString('base64')}`;
}

let reverseOk = false;
try {
  const bad = await import(badModule());
  const r = probes(bad, false);   // 不跑 classScanBind：坏版本不该触发真实落盘副作用
  const reds = Object.entries(r).filter(([, v]) => v !== true).map(([k]) => k);
  const cleanReds = Object.entries(clean).filter(([, v]) => v !== true).map(([k]) => k);
  ok('反向验证：坏版本至少 2 条断言变红', () =>
    assert.ok(reds.length >= 2, `只红了 ${reds.length} 条：${reds.join(' / ')}`));
  ok('反向验证：干净版零红', () => assert.equal(cleanReds.length, 0, cleanReds.join(' / ')));
  ok('反向验证：变红的正是已分班/跨租户判据', () =>
    assert.ok(reds.some((x) => x.includes('已分班')) && reds.some((x) => x.includes('跨租户')), reds.join(' / ')));
  reverseOk = reds.length >= 2 && cleanReds.length === 0;
  console.log(`       坏版本变红 ${reds.length} 条：${reds.join(' / ')}`);
} catch (e) {
  console.error(`  FAIL 反向验证加载失败：${e.message}`);
}

closeTenants();
rmSync(WAR, { recursive: true, force: true, maxRetries: 3 });

console.log(`\n结果：${PASS} 通过 / ${FAIL} 失败 · 反向验证 ${reverseOk ? 'PASS' : 'FAIL'}`);
process.exit(FAIL === 0 && reverseOk ? 0 : 1);
