// 设备用途（role）模型回归 —— 2026-10-25
//
// 为什么单独立测：用途是**设备身份的第二维**（第一维是班级归属），
// 它决定云端能不能按班级域下发、端侧该起哪套外壳。口径一旦漂了，
// 表现是"广播机收到黑板卡"或"教室机不推缩略图"这类看不见原因的怪事。
//
// 隔离纪律：registry → class-dir / events 都写磁盘，**必须先把路径指到临时目录**，
// 否则这个测试会往生产 assets 里写事件和班级库（同"别拿生产库做实验"）。
// 口径同 regression-class-*：只断言行为，不 mock 内部实现。
//
// 跑法：node tests/regression-device-role.mjs
import os from 'node:os';
import path from 'node:path';
import fs from 'node:fs';

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ste-role-'));
process.env.CLOUD_EVENTS_FILE = path.join(tmp, 'events.log');
process.env.CLOUD_CLASS_DIR_DB_FILE = path.join(tmp, 'class-dir.db');
process.env.CLOUD_QUEUE_DB_FILE = path.join(tmp, 'queue.db');
process.env.CLOUD_CLASS_ROOM_DB_FILE = path.join(tmp, 'room.db');

const {
  normalizeDeviceRole, markConnected, getDeviceRole, setDeviceMeta, listDevices,
} = await import('../src/registry.js');
const { initClassDir } = await import('../src/class-dir.js');
initClassDir();

let pass = 0, fail = 0;
function ok(name, cond, extra = '') {
  if (cond) { pass++; console.log(`  ✓ ${name}`); }
  else { fail++; console.log(`  ✗ ${name}${extra ? '  ← ' + extra : ''}`); }
}

/** 假 ws：markConnected 只用 readyState / close / send。 */
function fakeWs() { return { readyState: 1, close() {}, send() {} }; }

console.log('── 一、归一：白名单外一律 classroom ──');
ok('undefined → classroom', normalizeDeviceRole(undefined) === 'classroom');
ok('空串 → classroom', normalizeDeviceRole('') === 'classroom');
ok('未知值 gpu → classroom', normalizeDeviceRole('gpu') === 'classroom');
ok('大小写宽容 BROADCAST → broadcast', normalizeDeviceRole('BROADCAST') === 'broadcast');
ok('带空格 → broadcast', normalizeDeviceRole('  broadcast  ') === 'broadcast');

console.log('── 二、首次登记：端侧自报被采纳 ──');
markConnected('ROLE-A', '0.6.47', fakeWs(), null, { role: 'broadcast', classCode: 'station_xl_01' });
ok('A 用途 = broadcast', getDeviceRole('ROLE-A') === 'broadcast');
const a = listDevices().find((d) => d.uid === 'ROLE-A');
ok('设备表也带 role', a && a.role === 'broadcast');
ok('设备表仍带 classCode（站码）', a && a.classCode === 'station_xl_01');

console.log('── 三、云端权威：重连自报改不回 ──');
markConnected('ROLE-A', '0.6.47', fakeWs(), null, { role: 'classroom' });
ok('A 重连自报 classroom 无效（仍 broadcast）', getDeviceRole('ROLE-A') === 'broadcast',
  `实际 ${getDeviceRole('ROLE-A')}`);

console.log('── 四、管理端改用途 → 下次重连生效 ──');
setDeviceMeta('ROLE-A', { role: 'classroom' });
ok('A 改为 classroom', getDeviceRole('ROLE-A') === 'classroom');
markConnected('ROLE-A', '0.6.47', fakeWs(), null, { role: 'broadcast' });
ok('A 重连自报 broadcast 无效（仍 classroom）', getDeviceRole('ROLE-A') === 'classroom',
  `实际 ${getDeviceRole('ROLE-A')}`);

console.log('── 五、向后兼容：老被控端不带 role ──');
markConnected('ROLE-B', '0.6.20', fakeWs(), null, { classCode: 'class_x' });
ok('B 用途默认 classroom', getDeviceRole('ROLE-B') === 'classroom');
const b = listDevices().find((d) => d.uid === 'ROLE-B');
ok('B 设备表 role = classroom（不是 undefined）', b && b.role === 'classroom');

console.log('── 六、setDeviceMeta 新建条目时也认 role ──');
setDeviceMeta('ROLE-C', { role: 'broadcast', name: '广播机-1' });
ok('C 新建即为 broadcast', getDeviceRole('ROLE-C') === 'broadcast');
ok('C 名字落表', (listDevices().find((d) => d.uid === 'ROLE-C') || {}).name === '广播机-1');
setDeviceMeta('ROLE-C', { name: '只改名' });
ok('C 只改名不动用途', getDeviceRole('ROLE-C') === 'broadcast');

console.log(`\n[RESULT] ${pass} 通过 / ${fail} 失败`);
// 只删本测试自建的临时目录。sqlite 连接在进程内仍开着（node:sqlite 无 close 导出），
// Windows 上会 EBUSY —— 清理失败不算测试失败，临时目录由系统回收。
try { fs.rmSync(tmp, { recursive: true, force: true }); } catch { /* EBUSY：留系统临时目录 */ }
process.exit(fail === 0 ? 0 : 1);
