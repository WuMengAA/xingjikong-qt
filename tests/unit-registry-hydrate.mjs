// 「重启后班级归属还在吗」单测 —— 2026-11-18 · 被控端主体地位。
//
// 守的是这一条：**设备知道自己在哪个班，云端也必须知道这个班里有它。**
//
// 改造前（真事故）：registry 的 classes 是纯内存索引，hydrateClassesFromDir() 只把
// 班级**名**灌回来，不灌「班 → 设备」。云端一重启，listClasses() 里每个班都变成
// devices: []、onlineCount: 0 —— 设备自己的 classCode 明明还在，按班下发却找不到人，
// 班级看板整片空。本测试用一个**真的子进程**写完退出、父进程重开，复现"重启"这件事，
// 比在同一个进程里清变量更接近生产。
//
// 跑法：node tests/unit-registry-hydrate.mjs

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const dir = mkdtempSync(join(tmpdir(), 'reg-hydrate-'));
const ledgerDb = join(dir, 'ledger.db');
const classDb = join(dir, 'class-dir.db');
process.env.CLOUD_DEVICE_LEDGER_DB_FILE = ledgerDb;

// 子进程的 -e 代码里只能用**相对** import：Windows 上把 /mnt/d 这类路径拼成 file:// 会变成 D:\D:\…
const SRC = './src/';
const CLASS_DB = classDb.replace(/\\/g, '/');
const LEDGER_DB = ledgerDb.replace(/\\/g, '/');

let pass = 0;
let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log(`  ok   ${name}`); }
  catch (e) { fail++; console.error(`  FAIL ${name}\n       ${e.message}`); }
}

/* ── 第一段：子进程里注册一台设备并绑班，然后进程退出（＝云端重启前） ── */
const childCode = `
  const { initClassDir, closeClassDir } = await import('${SRC}class-dir.js');
  const { markConnected } = await import('${SRC}registry.js');
  initClassDir('${CLASS_DB}');
  const ws = { readyState: 1, send() {}, close() {}, terminate() {} };
  markConnected('class_hydrate_2028_08', '0.6.74', ws,
    { screen: true, input: true, actions: ['notify'] },
    { name: '高一8班讲台', classCode: 'xlzx_2028_08', className: '8班', role: 'classroom' });
  closeClassDir();
`;
const child = spawnSync(process.execPath, ['--input-type=module', '-e', childCode], {
  cwd: fileURLToPath(new URL('..', import.meta.url)),
  env: process.env,
  encoding: 'utf8',
});
if (process.env.STE_HYDRATE_DEBUG) console.error('child:', child.status, child.error, child.stdout, child.stderr);
ok('子进程注册并绑班成功（＝重启前的云端）', () => {
  assert.equal(child.status, 0, '子进程退出码 ' + child.status + '\n' + child.stderr);
});

/* ── 第二段：父进程全新装载身份，再 hydrate（＝云端重启后） ── */
const { initClassDir, closeClassDir } = await import('../src/class-dir.js');
const {
  hydrateClassesFromDir, listClasses, getDevicesOfClass, listDevices, getDeviceClassCode,
} = await import('../src/registry.js');

closeClassDir();
initClassDir(classDb);
const n = hydrateClassesFromDir();

ok('身份账本把设备装回来了', () => {
  const dev = listDevices().find((d) => d.uid === 'class_hydrate_2028_08');
  assert.ok(dev, '重启后账本里查不到这台设备');
  assert.equal(dev.online, false, '重启后不该是在线态');
  assert.equal(dev.name, '高一8班讲台');
});

ok('hydrate 之后「班 → 设备」归属被重建（改造前这里是空的）', () => {
  const devs = getDevicesOfClass('xlzx_2028_08');
  assert.ok(devs.includes('class_hydrate_2028_08'),
    '「班 → 设备」没重建，实际 ' + JSON.stringify(devs));
});

ok('listClasses 里这个班带着设备（按班下发与班级看板都读它）', () => {
  const cls = listClasses().find((c) => c.code === 'xlzx_2028_08');
  assert.ok(cls, '重启后班级不见了');
  assert.ok(cls.devices.includes('class_hydrate_2028_08'),
    '班级 devices 为空，实际 ' + JSON.stringify(cls.devices));
  assert.equal(cls.onlineCount, 0, '重启后在线数应为 0');
  assert.equal(cls.name, '8班', '班名应来自班级目录');
});

ok('hydrate 是幂等的（重复启动不会重复叠加设备）', () => {
  hydrateClassesFromDir();
  hydrateClassesFromDir();
  const devs = getDevicesOfClass('xlzx_2028_08');
  assert.equal(devs.filter((u) => u === 'class_hydrate_2028_08').length, 1,
    '重复 hydrate 叠出了重复设备 ' + JSON.stringify(devs));
});

ok('没有班级归属的设备不会被塞进任何一个班', () => {
  const before = listClasses().reduce((s, c) => s + c.devices.length, 0);
  assert.equal(before, getDevicesOfClass('xlzx_2028_08').length, '出现了不该存在的归属');
});

ok('设备自己的 classCode 与班级索引口径一致', () => {
  assert.equal(getDeviceClassCode('class_hydrate_2028_08'), 'xlzx_2028_08');
});

closeClassDir();
try { rmSync(dir, { recursive: true, force: true }); } catch { /* 临时目录删不掉无所谓 */ }

if (fail) {
  console.error('\n[registry-hydrate] FAIL ' + fail);
  process.exit(1);
}
console.log('[registry-hydrate] PASS ' + pass + ' / FAIL 0');
