// 噪音存证（noise-evidence）单测 —— 2026-11-14
//
// 这份测试守的是一个**判据失效**类的事故，不是显示问题：
// 旧代码 meta 里写 db: Number(payload.db) || 0。被控端用 NaN 表示「这一拍没测到」，
// NaN 进不了 JSON（序列化成 null），而 Number(null) === 0 ⇒ 库里变成「测得 0 dB」。
// 0 dB 是理论上的绝对静音，与真实读数混在一起之后，任何按 dB 做的阈值判断、
// 排行、前后对照都不可信，而且**事后无法从库里分辨哪些是假的**。
//
// 现在约定：能解析成有限数才写字段，否则整个字段不出现。
// 上层用 Object.hasOwn(meta, 'db') 区分「有读数」与「没有读数」。
//
// 跑法：node tests/unit-noise-evidence.mjs
//
// ⚠️ CLOUD_RECORDINGS_DIR 是在模块**加载时**读的（noise-evidence.js 顶层 const），
//    所以必须先设环境变量再 import，不能用静态 import。

import assert from 'node:assert/strict';
import { mkdtempSync, rmSync, readFileSync, existsSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const DIR = mkdtempSync(join(tmpdir(), 'ste-noise-'));
process.env.CLOUD_RECORDINGS_DIR = DIR;
const mod = await import('../src/noise-evidence.js');
const { storeNoiseIncident, listNoiseIncidents, readNoiseEvidence } = mod;

let pass = 0;
let fail = 0;
function ok(name, fn) {
  try { fn(); pass++; console.log('  ok   ' + name); }
  catch (e) { fail++; console.error('  FAIL ' + name + '\n       ' + e.message); }
}

// 1x1 真 JPEG 的 base64（不是随便一串字符 —— b64decode 会照单全收，写出来却不是图）
const JPEG_B64 = '/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0a'
  + 'HBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/wAALCAABAAEBAREA/8QAFAABAAAAAAAA'
  + 'AAAAAAAAAAAACf/EABQQAQAAAAAAAAAAAAAAAAAAAAD/2gAIAQEAAD8AKp//2Q==';

console.log('[1] 缺失的 dB 读数不能变成 0');
const r1 = storeNoiseIncident('cls_a', { at: new Date().toISOString(), periodKey: 'p#1', hits: 3 });
const m1 = r1.meta;
ok('db 字段不存在（不是 0）', () => assert.equal(Object.hasOwn(m1, 'db'), false));
ok('baselineDb 字段不存在', () => assert.equal(Object.hasOwn(m1, 'baselineDb'), false));
ok('thresholdDb 字段不存在', () => assert.equal(Object.hasOwn(m1, 'thresholdDb'), false));
ok('hits 仍是 3（自然数，0 有意义所以保留兜底）', () => assert.equal(m1.hits, 3));

console.log('[2] 真实读数照写，且 0 dB 是**真读数**不是缺失');
const r2 = storeNoiseIncident('cls_a', { at: new Date().toISOString(), db: -32.5, baselineDb: -55, thresholdDb: -20 });
ok('db 写入 -32.5', () => assert.equal(r2.meta.db, -32.5));
ok('baselineDb 写入 -55', () => assert.equal(r2.meta.baselineDb, -55));
const r2b = storeNoiseIncident('cls_a', { at: new Date().toISOString(), db: 0 });
ok('显式 0 会被写下（0 是合法读数）', () => assert.equal(Object.hasOwn(r2b.meta, 'db'), true));
ok('显式 0 的值就是 0', () => assert.equal(r2b.meta.db, 0));

console.log('[3] null / 空串 / 非数一律不写');
const r3 = storeNoiseIncident('cls_a', { at: new Date().toISOString(), db: null, baselineDb: '', thresholdDb: 'abc' });
ok('null 不写', () => assert.equal(Object.hasOwn(r3.meta, 'db'), false));
ok('空串不写', () => assert.equal(Object.hasOwn(r3.meta, 'baselineDb'), false));
ok('非数不写', () => assert.equal(Object.hasOwn(r3.meta, 'thresholdDb'), false));

console.log('[4] 声学指纹：有就原样透传，没有就不出现');
const sp = { rmsDb: -31.2, aDb: -42.7, cDb: -38.1, lcMinusLa: 4.6, dominantBandHz: 1000,
  bandsDb: [-70, -65, -60, -55, -40, -58, -66], speechRatio: 0.71, l10: -35, l50: -44, l90: -52, statSamples: 120 };
const r4 = storeNoiseIncident('cls_a', { at: new Date().toISOString(), spectrum: sp });
ok('spectrum 透传', () => assert.equal(r4.meta.spectrum.aDb, -42.7));
ok('spectrum 的倍频程数组保留', () => assert.equal(r4.meta.spectrum.bandsDb.length, 7));
ok('LC-LA 保留（判低频有多重的那个数）', () => assert.equal(r4.meta.spectrum.lcMinusLa, 4.6));
ok('没有 spectrum 时字段不出现', () => assert.equal(Object.hasOwn(r1.meta, 'spectrum'), false));
const r4b = storeNoiseIncident('cls_a', { at: new Date().toISOString(), spectrum: 'not-an-object' });
ok('spectrum 不是对象时不写', () => assert.equal(Object.hasOwn(r4b.meta, 'spectrum'), false));

console.log('[5] 图片落盘 + 元数据长期保留 + 5 分钟过期');
const r5 = storeNoiseIncident('cls_a', {
  at: new Date().toISOString(), desktop: JPEG_B64, desktopError: '',
  cameraError: '摄像头不可用',
});
ok('桌面图落盘', () => assert.equal(r5.meta.desktop, true));
ok('摄像头失败记原因', () => assert.equal(r5.meta.cameraError, '摄像头不可用'));
ok('返回 savedImages 只含桌面图', () => assert.equal(r5.savedImages.length, 1));
const deskFile = r5.savedImages[0];
ok('图真的写进 noise_evidence/<uid>/', () => assert.ok(existsSync(join(DIR, 'noise_evidence', 'cls_a', deskFile))));
ok('过期前读得到', () => assert.ok(readNoiseEvidence('cls_a', deskFile)));

console.log('[6] 两张图都没有也要留元数据（频次/排行靠它）');
const r6 = storeNoiseIncident('cls_b', { at: new Date().toISOString(), desktopError: '锁屏', cameraError: '未配置' });
ok('事件入库', () => assert.equal(r6.ok, true));
ok('元数据文件存在', () => assert.ok(existsSync(join(DIR, 'noise_events', 'cls_b', r6.id + '.json'))));
ok('desktop 标 false', () => assert.equal(r6.meta.desktop, false));
ok('两个原因都留了', () => assert.equal(r6.meta.desktopError, '锁屏'));
// 2026-11-14：三路全失败 = **三条**原因。加了声谱图这一路之后，
// 原来是 2 条（桌面/摄像头），现在是 3 条 —— 这条断言本身没变语义
// （"每一路失败都要留一条原因"），只是把路数如实跟上。
// 若哪天变成 2 条，说明声谱图那一路的失败原因被静默吞掉了。
ok('errors 三路全失败列出三条', () => assert.equal(r6.errors.length, 3));
ok('errors 里点名了声谱图这一路', () => {
  assert.ok(r6.errors.some(e => e.startsWith('spectrogram:')));
  assert.deepEqual(r6.errors.map(e => e.split(':')[0]).sort(),
                   ['camera', 'desktop', 'spectrogram']);
});

console.log('[7] 列表按时间倒序 + 跨设备隔离');
const r7 = storeNoiseIncident('cls_b', { at: new Date(Date.now() + 1000).toISOString() });
const listB = listNoiseIncidents('cls_b');
ok('cls_b 有两件事', () => assert.equal(listB.length, 2));
ok('最新的在前', () => assert.equal(listB[0].id, r7.id));
ok('cls_a 看不到 cls_b 的', () => {
  const a = listNoiseIncidents('cls_a').map(x => x.uid);
  assert.ok(a.every(u => u === 'cls_a'));
});
ok('不存在的 uid 返回空数组（不是 undefined）', () => assert.deepEqual(listNoiseIncidents('nobody'), []));

console.log('[8] 过期图读不到，但元数据还在');
const metaPath = join(DIR, 'noise_events', 'cls_a', r5.id + '.json');
const meta = JSON.parse(readFileSync(metaPath, 'utf8'));
meta.expiry = Date.now() - 1000;   // 手工改成已过期
writeFileSync(metaPath, JSON.stringify(meta));
ok('过期后 readNoiseEvidence 返回 null', () => assert.equal(readNoiseEvidence('cls_a', deskFile), null));
ok('过期后元数据仍在（频次统计不受影响）', () => assert.ok(existsSync(metaPath)));

console.log('\nPASS ' + pass + ' / FAIL ' + fail);
rmSync(DIR, { recursive: true, force: true });
process.exit(fail ? 1 : 0);
