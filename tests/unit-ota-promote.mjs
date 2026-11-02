// 渠道自动晋升单元测试（2026-11-02 · 用户点单语义固化）
//
// 语义：构建不变，只是渠道状态变 ——
//   alpha(灰度) 发布即可见 → 满 5 小时进 beta(测试) → 满 24 小时进 stable(正式)。
//   客户端显示的是"渠道已晋升"，版本号/包/sha 一个字都不变。
//
// 用法：node tests/unit-ota-promote.mjs   （纯单元测试，不写盘、不起云端）
// 返回码：0 全过；非 0 有失败。

import {
  normalizeChannelName, visibleInChannel, thresholdFor, pickBuildFor,
  PROMOTE_BETA_MS, PROMOTE_STABLE_MS, buildKey,
} from '../src/ota-promote.js';

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  OK   ${name}${detail ? ' — ' + detail : ''}`); }
  else { fail++; console.log(`  FAIL ${name}${detail ? ' — ' + detail : ''}`); }
}

const T0 = Date.parse('2026-11-02T10:00:00.000Z');
const H = 3600 * 1000;
const build = {
  product: 'agent', version: '0.7.0-rc.3', url: 'https://x/a.exe',
  sha256: 'a'.repeat(64), size: 100, notes: 'n', mandatory: false,
  publishedAt: new Date(T0).toISOString(),
};
const reg = { builds: { [buildKey('agent', build)]: build } };

console.log('\nA. 渠道名归一（旧叫法兼容）');
check('alpha -> alpha', normalizeChannelName('alpha') === 'alpha');
check('gray  -> alpha（旧叫法）', normalizeChannelName('gray') === 'alpha');
check('beta -> beta', normalizeChannelName('beta') === 'beta');
check('rc   -> beta（旧叫法）', normalizeChannelName('rc') === 'beta');
check('stable -> stable', normalizeChannelName('stable') === 'stable');
check('空 -> stable', normalizeChannelName('') === 'stable');
check('未知渠道 -> stable（保守，不当 alpha）', normalizeChannelName('weird') === 'stable');

console.log('\nB. 门槛常量（用户点单 5 小时 / 24 小时）');
check('beta 门槛 = 5 小时', PROMOTE_BETA_MS === 5 * H, `${PROMOTE_BETA_MS}ms`);
check('stable 门槛 = 24 小时', PROMOTE_STABLE_MS === 24 * H, `${PROMOTE_STABLE_MS}ms`);
check('alpha 门槛 = 0', thresholdFor('alpha') === 0);
check('gray 门槛 = 0', thresholdFor('gray') === 0);
check('beta 门槛函数一致', thresholdFor('beta') === PROMOTE_BETA_MS);
check('stable 门槛函数一致', thresholdFor('stable') === PROMOTE_STABLE_MS);

console.log('\nC. 可见性纯函数');
check('alpha 立刻可见', visibleInChannel('alpha', 0) === true);
check('beta 4h59m 不可见', visibleInChannel('beta', 5 * H - 60000) === false);
check('beta 满 5h 可见', visibleInChannel('beta', 5 * H) === true);
check('stable 5h 不可见', visibleInChannel('stable', 5 * H) === false);
check('stable 23h59m 不可见', visibleInChannel('stable', 24 * H - 60000) === false);
check('stable 满 24h 可见', visibleInChannel('stable', 24 * H) === true);

console.log('\nD. 各时刻各渠道拿到什么（构建不变）');
check('t=0 alpha 拿到', pickBuildFor(reg, 'agent', 'alpha', T0)?.version === '0.7.0-rc.3');
check('t=0 gray 拿到（旧叫法）', pickBuildFor(reg, 'agent', 'gray', T0)?.version === '0.7.0-rc.3');
check('t=0 beta 拿不到', pickBuildFor(reg, 'agent', 'beta', T0) === null);
check('t=0 stable 拿不到', pickBuildFor(reg, 'agent', 'stable', T0) === null);
check('t=5h beta 拿到', pickBuildFor(reg, 'agent', 'beta', T0 + 5 * H)?.version === '0.7.0-rc.3');
check('t=5h stable 仍拿不到', pickBuildFor(reg, 'agent', 'stable', T0 + 5 * H) === null);
check('t=24h stable 拿到', pickBuildFor(reg, 'agent', 'stable', T0 + 24 * H)?.version === '0.7.0-rc.3');

console.log('\nE. 构建不变（用户点单的核心约束）');
const a = pickBuildFor(reg, 'agent', 'alpha', T0);
const b = pickBuildFor(reg, 'agent', 'beta', T0 + 5 * H);
const c = pickBuildFor(reg, 'agent', 'stable', T0 + 24 * H);
check('三档拿到同一 version', a.version === b.version && b.version === c.version);
check('三档拿到同一 url', a.url === b.url && b.url === c.url);
check('三档拿到同一 sha256', a.sha256 === b.sha256 && b.sha256 === c.sha256);
check('三档拿到同一 publishedAt（晋升不改时间）', a.publishedAt === b.publishedAt && b.publishedAt === c.publishedAt);

console.log('\nF. 多构建并存：各渠道拿"自己可见范围内最新"');
const b2 = { ...build, version: '0.7.1-rc.1', sha256: 'b'.repeat(64), publishedAt: new Date(T0 + 2 * H).toISOString() };
const reg2 = { builds: { [buildKey('agent', build)]: build, [buildKey('agent', b2)]: b2 } };
check('alpha 拿到更新的 rc.1', pickBuildFor(reg2, 'agent', 'alpha', T0 + 2 * H)?.version === '0.7.1-rc.1');
check('beta 在 7h 拿到 rc.1（它已满 5h）', pickBuildFor(reg2, 'agent', 'beta', T0 + 7 * H)?.version === '0.7.1-rc.1');
check('beta 在 6h 只能拿 0.7.0-rc.3（rc.1 刚 4h）', pickBuildFor(reg2, 'agent', 'beta', T0 + 6 * H)?.version === '0.7.0-rc.3');
// rc.1 发布于 T0+2h，stable 门槛 24h ⇒ T0+26h 整点刚好可见（>= 语义）。
check('stable 在 25h59m 只能拿 0.7.0-rc.3', pickBuildFor(reg2, 'agent', 'stable', T0 + 26 * H - 60000)?.version === '0.7.0-rc.3');
check('stable 在 26h 整点拿到 rc.1', pickBuildFor(reg2, 'agent', 'stable', T0 + 26 * H)?.version === '0.7.1-rc.1');

console.log('\nG. 产品隔离与边界');
check('viewer 问 agent 的构建 -> null', pickBuildFor(reg, 'viewer', 'alpha', T0) === null);
check('空注册表 -> null', pickBuildFor({ builds: {} }, 'agent', 'alpha', T0) === null);
check('坏 publishedAt 的记录被跳过', pickBuildFor({ builds: { k: { ...build, publishedAt: 'garbage' } } }, 'agent', 'alpha', T0) === null);

console.log('\nG2. 存量豁免（上线前已直发 stable 的构建不能凭空消失）');
const gfOld = { ...build, version: '0.6.61', publishedAt: new Date(T0).toISOString(), grandfathered: true };
const reg3 = { builds: { [buildKey('agent', gfOld)]: gfOld } };
check('豁免构建 t=0 就可见 stable', pickBuildFor(reg3, 'agent', 'stable', T0)?.version === '0.6.61');
check('豁免构建 beta 也可见', pickBuildFor(reg3, 'agent', 'beta', T0)?.version === '0.6.61');
check('豁免构建 alpha 也可见', pickBuildFor(reg3, 'agent', 'alpha', T0)?.version === '0.6.61');
check('新构建存在时 stable 取更新的那个', (() => {
  const newer = { ...build, version: '0.7.0', publishedAt: new Date(T0 + 30 * H).toISOString() };
  const r = { builds: { [buildKey('agent', gfOld)]: gfOld, [buildKey('agent', newer)]: newer } };
  return pickBuildFor(r, 'agent', 'stable', T0 + 60 * H)?.version === '0.7.0';
})());

console.log('\nH. 构建指纹');
check('同版本同 sha -> 同一指纹', buildKey('agent', build) === buildKey('agent', { ...build, notes: 'changed' }));
check('同版本不同 sha -> 不同指纹', buildKey('agent', build) !== buildKey('agent', { ...build, sha256: 'c'.repeat(64) }));
check('不同产品 -> 不同指纹', buildKey('agent', build) !== buildKey('viewer', build));

console.log(`\n==== ${pass} 通过 / ${fail} 失败 ====`);
process.exit(fail === 0 ? 0 : 1);
