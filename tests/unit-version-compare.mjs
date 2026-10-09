// 版本比较单元回归测试（2026-10-08 固化）
//
// 作用：锁住 ota.js 的 parseVersion / cmpVersion —— 它是**全项目唯一一份版本比较实现**。
// 背景：管理端与被控端都要回答"要不要升级"。此前两端各写一遍 C++ 比较，出了个静默 bug：
//   core 打平后哨兵写错（无后缀当成 0，而 0 正好是 RANK_ALPHA）⇒「正式版 < 候选版」：
//     compareVersion("0.6.23", "0.6.23-rc.1") → -1   // 本该 +1
//   后果是云端发了正式版、客户端判成"已是最新"、教室机永远不升级。
//   现在客户端带 ?current= 来问云端，比较只有这一处 ⇒ 这里断言就是全网升级判定的护栏。
// 命名规范：docs/版本号命名规范-2026-10-07.md（X.Y.Z / -rc.N / -alpha.N，alpha<rc<正式）
//
// 用法：node tests/unit-version-compare.mjs   （纯单元测试，不起云端）
// 返回码：0 全过；非 0 有失败。

const { cmpVersion, parseVersion } = await import('../src/ota.js');

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  ✅ ${name}${detail ? ' — ' + detail : ''}`); }
  else { fail++; console.log(`  ❌ ${name}${detail ? ' — ' + detail : ''}`); }
}

console.log('\nA. 核心三段比较');
check('相同版本 = 0', cmpVersion('0.6.23', '0.6.23') === 0);
check('补丁号递增 > 0', cmpVersion('0.6.22', '0.6.23') === -1);
check('次版本递增 > 0', cmpVersion('0.6.23', '0.7.0') === -1);
check('主版本递增 > 0', cmpVersion('0.6.23', '1.0.0') === -1);
check('逆向返回 1', cmpVersion('0.7.0', '0.6.23') === 1);
// 字符串比较会错的用例：'0.6.9' > '0.6.10' 在字典序下成立，数值序下不成立
check('数值序而非字典序（0.6.9 < 0.6.10）', cmpVersion('0.6.9', '0.6.10') === -1, '字典序会判反');

console.log('\nB. 阶段序：alpha(0) < rc(1) < 正式(2)');
check('alpha < rc', cmpVersion('0.6.23-alpha.1', '0.6.23-rc.1') === -1);
check('rc < 正式', cmpVersion('0.6.23-rc.1', '0.6.23') === -1);
check('alpha < 正式', cmpVersion('0.6.23-alpha.1', '0.6.23') === -1);
// ⚠️ 这就是当年那个静默 bug 的断言（哨兵错时这里会返回 -1）
check('正式 > 候选（回归哨兵 bug）', cmpVersion('0.6.23', '0.6.23-rc.1') === 1);
check('正式 > 内测', cmpVersion('0.6.23', '0.6.23-alpha.1') === 1);
check('rc 同阶段号递增', cmpVersion('0.6.23-rc.1', '0.6.23-rc.2') === -1);
check('alpha 同阶段号递增', cmpVersion('0.6.23-alpha.2', '0.6.23-alpha.10') === -1, '数值序');
check('同阶段同号相等', cmpVersion('0.6.23-rc.2', '0.6.23-rc.2') === 0);
// 阶段不同时，阶段秩优先于阶段号（alpha.9 仍低于 rc.1）
check('阶段秩优先于阶段号', cmpVersion('0.6.23-alpha.9', '0.6.23-rc.1') === -1);
// 核心不同时，核心优先于阶段
check('核心优先于阶段', cmpVersion('0.6.23-rc.9', '0.6.24-alpha.1') === -1);

console.log('\nC. 不可解析 → 一律 0（"不比"，绝不误判有新版）');
for (const bad of ['', null, undefined, '0.6', '0.6.23.1', '0.6.23-rc', '0.6.23-rc.0', '0.6.23-beta.1', 'v0.6.23', 'rc.1']) {
  check(`非法输入 "${String(bad)}" 结果 0`, cmpVersion(bad, '0.6.23') === 0);
  check(`非法输入作右侧 "${String(bad)}" 结果 0`, cmpVersion('0.6.23', bad) === 0);
}

console.log('\nD. parseVersion 结构');
const p = parseVersion('0.6.23-rc.2');
check('core 拆成三段数字', JSON.stringify(p.core) === '[0,6,23]', JSON.stringify(p.core));
check('stage = rc', p.stage === 'rc', p.stage);
check('n = 2', p.n === 2, String(p.n));
const p2 = parseVersion('1.2.3');
check('正式版 stage 为空串', p2.stage === '', `"${p2.stage}"`);
check('正式版 n = 0', p2.n === 0, String(p2.n));

console.log(`\n结果：${pass} 通过 / ${fail} 失败`);
process.exit(fail === 0 ? 0 : 1);
