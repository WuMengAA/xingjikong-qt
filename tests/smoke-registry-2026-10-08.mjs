// 2026-10-08 冒烟测试：已知设备表（离线保留）+ name + 分班制 + storage
// 不依赖外部服务，直接 import registry.js 的函数驱动。
import assert from 'node:assert';
import {
  markConnected, markDisconnected, listDevices, getDevice,
  setDeviceMeta, bindDeviceToClass, unbindDeviceFromClass, listClasses,
  getStorageInfo, isClassCodeValid,
} from '../src/registry.js';

let pass = 0, fail = 0;
const ok = (c, m) => { if (c) { pass++; } else { fail++; console.error('  ✗ ' + m); } };

// 1) 设备上线带 name + classCode
const fakeWs = { readyState: 1, send() {}, close() {} };
markConnected('DEV-001', '0.6.23-rc.2', fakeWs, null, { name: '三年二班-01', classCode: 'C90901' });
let ds = listDevices();
ok(ds.length === 1, '上线后列表应有 1 台');
ok(ds[0].name === '三年二班-01', 'name 应回显');
ok(ds[0].classCode === 'C90901', 'classCode 应回显');
ok(ds[0].online === true, '在线应为 true');

// 2) 离线后设备仍在列表，online=false（根因修复核心）
markDisconnected(fakeWs);
ds = listDevices();
ok(ds.length === 1, '离线后设备不应从列表消失');
ok(ds[0].online === false, '离线应为 false');
ok(ds[0].name === '三年二班-01', '离线后 name 仍保留');

// 3) 重连沿用旧 name（register 没带 name）
markConnected('DEV-001', '0.6.23-rc.2', { readyState: 1, send() {}, close() {} });
ds = listDevices();
ok(ds[0].name === '三年二班-01', '重连沿用旧 name');
ok(ds[0].online === true, '重连后在线');

// 4) setDeviceMeta 改名
setDeviceMeta('DEV-001', { name: '机房A-07' });
ok(listDevices()[0].name === '机房A-07', 'setDeviceMeta 改名生效');

// 5) 分班制
const r1 = bindDeviceToClass('C90901', 'DEV-002', '三年二班');
ok(r1.ok === true, 'bindDeviceToClass 成功');
const cls = listClasses();
ok(cls.length === 1 && cls[0].code === 'C90901', '班级列表应含 C90901');
ok(cls[0].devices.includes('DEV-001') && cls[0].devices.includes('DEV-002'), '班级应包含两台设备');
// 在线数现算：DEV-001 在线、DEV-002 不在
ok(cls[0].onlineCount === 1, 'onlineCount 应现算为 1（DEV-002 未上线）');

// 6) 班级码格式校验
ok(isClassCodeValid('C90901') === true, '合法班级码应通过');
ok(isClassCodeValid('ab') === false, '过短应拒绝');
ok(isClassCodeValid('has space') === false, '含空格应拒绝');

// 7) 解绑
unbindDeviceFromClass('C90901', 'DEV-001');
ok(!listClasses()[0].devices.includes('DEV-001'), '解绑后 DEV-001 不在班级');
ok(listDevices().find(d => d.uid === 'DEV-001').classCode === null, '解绑后设备 classCode 清空');

// 8) storage 信息
const st = getStorageInfo();
ok(typeof st.knownDevices === 'number' && st.knownDevices >= 2, 'storage 应含 knownDevices');
ok(typeof st.onlineDevices === 'number', 'storage 应含 onlineDevices');
ok(typeof st.pendingInstructions === 'number', 'storage 应含 pendingInstructions');
ok(typeof st.eventsBytes === 'number', 'storage 应含 eventsBytes');

console.log(`\n云端数据面冒烟测试：通过 ${pass} / 失败 ${fail}`);
process.exit(fail ? 1 : 0);
