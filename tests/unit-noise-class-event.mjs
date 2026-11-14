// 噪音按班上报的云端协议单测（2026-11-15）。
// 不套外部规范：只固化本项目 class-events.js 既有四道闸与「班级归属只认云端表」的信任模型，
// 让被控端新发的 noise_alert / noise_incident 两种 kind 有可回归的契约。
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const oldCwd = process.cwd();
const d = mkdtempSync(join(tmpdir(), 'noiseclass-'));
process.chdir(d);                       // 模块级默认文件（events/queue/db）一律落临时目录
const { initClassDir, closeClassDir } = await import('../src/class-dir.js');
const { initClassRoom, closeClassRoom, recentEvents } = await import('../src/class-room.js');
const { bindDeviceToClass, getDeviceClassCode, relayToSubscribers, relayTerminalFrame } = await import('../src/registry.js');
const { TENANT_ID_RE } = await import('../src/tenant.js');
const { handleAgentClassMsg } = await import('../src/class-events.js');
initClassDir(join(d, 'class-dir.db'));
initClassRoom(join(d, 'class-room.db'));

let pass = 0, fail = 0;
function ok(name, fn) {
  try { const r = fn(); if (r === false) throw new Error('断言为 false'); pass++; console.log('OK   ' + name); }
  catch (e) { fail++; console.log('FAIL ' + name + ' :: ' + (e && e.message)); }
}

function once(payload) {
  const acks = [];
  const ws = { readyState: 1, send() {} };
  handleAgentClassMsg(ws, { type: 'class-event', payload }, 'dev-noise-1',
    (w, type, p) => acks.push({ type, p }));
  return acks;
}
const asData = (e) => (typeof e.data === 'string' ? JSON.parse(e.data) : e.data);

const CODE = 'class_noise_01';
const bind = bindDeviceToClass(CODE, 'dev-noise-1', '噪音测试班', 'cloud');
ok('绑班成功', () => bind.ok === true);
ok('云端表能查回班级码（归属只认表）', () => getDeviceClassCode('dev-noise-1') === bind.classCode);

const good = { kind: 'noise_alert', data: { message: '本节课噪音连续超阈 3 次，已触发提醒', hits: 3, db: -30, at: '2026-11-15T01:00:00.000Z' } };
const a1 = once(good);
ok('正常载荷回执 ok:true', () => a1.length === 1 && a1[0].type === 'class-event-ack' && a1[0].p.ok === true);
ok('回执带非空事件 id', () => a1[0].p.id != null);
const evs = recentEvents(bind.classCode, 20);
ok('事件落进设备所属班的历史', () => evs.length === 1);
ok('kind 与中文 message 原样保留（viewer Timeline 直接显示）', () => evs[0].kind === 'noise_alert' && asData(evs[0]).message === good.data.message);
ok('历史里没有 classCode 字段（端侧不许自报班级）', () => !('classCode' in asData(evs[0])));

// ★ 信任模型：载荷里塞 classCode 也一律忽略，仍投 knownDevices 里的真班
const spoof = { kind: 'noise_incident', data: { message: '本节课噪音存证已生成', classCode: 'class_evil_xx' } };
const a2 = once(spoof);
ok('伪造 classCode 仍被接收', () => a2[0].p.ok === true);
ok('伪造班级码没有新建出第二条历史', () => recentEvents('class_evil_xx', 20).length === 0);
ok('事件仍落真班（累计 2 条）', () => recentEvents(bind.classCode, 20).length === 2);

// 没绑班的设备：拒收并如实回错，不静默
const orphan = [];
handleAgentClassMsg({ readyState: 1, send() {} }, { type: 'class-event', payload: good }, 'dev-orphan',
  (w, type, p) => orphan.push(p));
ok('未绑班回 NO_CLASS', () => orphan.length === 1 && orphan[0].ok === false && orphan[0].code === 'NO_CLASS');

// 四道闸：kind 格式 / data 形状 / 体量 / 1s 限流
const badKind = once({ kind: 'Noise_Alert!', data: { message: 'x' } });
ok('kind 格式非法被拒（BAD_PAYLOAD）', () => badKind[0].p.ok === false && badKind[0].p.code === 'BAD_PAYLOAD');
const badData = once({ kind: 'noise_alert', data: [1, 2, 3] });
ok('data 是数组被拒', () => badData[0].p.ok === false && badData[0].p.code === 'BAD_PAYLOAD');
const huge = once({ kind: 'noise_alert', data: { message: 'x'.repeat(70000) } });
ok('超大 data（>64KB）被拒', () => huge[0].p.ok === false && huge[0].p.code === 'BAD_PAYLOAD');
const again = once({ kind: 'noise_alert', data: { message: '再来一条' } });
ok('1s 内同 kind 第二条回 throttled:true（不许静默丢）', () => again[0].p.ok === true && again[0].p.throttled === true);

// 重复合并的两处零行为变化断言（2026-11-15）
ok('relayTerminalFrame 与 relayToSubscribers 是同一函数', () => relayTerminalFrame === relayToSubscribers);
ok('TENANT_ID_RE 由 tenant.js 单一导出且格式不变', () => TENANT_ID_RE instanceof RegExp && TENANT_ID_RE.test('tn_default') && !TENANT_ID_RE.test('BAD_ID'));

closeClassRoom();
closeClassDir();
process.chdir(oldCwd);
rmSync(d, { recursive: true, force: true });
console.log('PASS=' + pass + ' FAIL=' + fail);
process.exit(fail ? 1 : 0);
