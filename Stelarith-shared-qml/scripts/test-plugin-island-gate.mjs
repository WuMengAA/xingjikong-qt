#!/usr/bin/env node
// 星集控 · 灵动岛第三方推岛治理（P0-0）· 源码契约门禁
// ─────────────────────────────────────────────────────────────────────────────
// 为什么需要：P0-0 的约束是**跨语言、跨文件**的——
//   常量在 pluginhost.h，执行在 pluginhost.cpp，接线在 main.cpp，
//   「键名/默认值」同时被 AgentSettings（QSettings）、agent_ui.h（Q_PROPERTY）
//   与 AgentSettingsPage.qml（开关）三处引用。任何一处漏改，故障都是**静默**的：
//     · 常量改了而执行没跟 ⇒ 文档说 5 条/秒、实际无限
//     · 接线丢了 senderPriorityLimit ⇒ 设置页开关形同虚设（跑的是 fail-open）
//     · 默认值三处不一致 ⇒ 开关打开后界面显示"关"，或反之
// 所以这里用 grep 源码的方式把这五处钉死。与既有 .mjs 门禁同一范式
// （test-island-forms / test-island-metrics / test-ui-contract），独立脚本、不进构建。
//
// 用法：cd Stelarith-xingjikong-qt && node Stelarith-shared-qml/scripts/test-plugin-island-gate.mjs
// 退出码：0 全过；1 有失败。
// ⚠️ 匹配前先剥注释（`//` 行、`/* */` 块）—— 本门禁的断言文案本身就写在注释里，
//    不剥就会"自己证明自己"（D 档 §七-5 的教训：假绿）。
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

const ROOT = process.cwd();
const F = {
  gateH:   'Stelarith-control-qt/src/plugin/pluginhost.h',
  gateCpp: 'Stelarith-control-qt/src/plugin/pluginhost.cpp',
  main:    'Stelarith-control-qt/src/main.cpp',
  agentUi: 'Stelarith-control-qt/src/agent_ui.h',
  page:    'Stelarith-control-qt/qml/shell/pages/AgentSettingsPage.qml',
  islandH: 'Stelarith-control-qt/src/island.h',
};
const read = (rel) => fs.readFileSync(path.join(ROOT, rel), 'utf8');

// 剥 C/C++ 注释（保留行结构与字符位置无关性；QML 同用）
function stripCComments(src) {
  let out = '';
  let i = 0;
  let mode = 0; // 0=code 1=line 2=block 3=string 4=char
  while (i < src.length) {
    const c = src[i], n = src[i + 1];
    if (mode === 0) {
      if (c === '/' && n === '/') { mode = 1; i += 2; continue; }
      if (c === '/' && n === '*') { mode = 2; i += 2; continue; }
      if (c === '"') { mode = 3; out += c; i++; continue; }
      if (c === "'") { mode = 4; out += c; i++; continue; }
      out += c; i++;
    } else if (mode === 1) {
      if (c === '\n') { mode = 0; out += c; }
      i++;
    } else if (mode === 2) {
      if (c === '*' && n === '/') { mode = 0; i += 2; continue; }
      if (c === '\n') out += c;
      i++;
    } else {
      out += c;
      if (mode === 3 && c === '\\') { out += src[i + 1] ?? ''; i += 2; continue; }
      if (mode === 4 && c === '\\') { out += src[i + 1] ?? ''; i += 2; continue; }
      if ((mode === 3 && c === '"') || (mode === 4 && c === "'")) mode = 0;
      i++;
    }
  }
  return out;
}
const strip = (src) => stripCComments(src);

let pass = 0, fail = 0;
const ok = (msg) => { pass++; console.log(`  PASS  ${msg}`); };
const bad = (msg, detail) => { fail++; console.log(`  FAIL  ${msg}`); if (detail) console.log(`        ↳ ${detail}`); };
function check(cond, msg, detail) { cond ? ok(msg) : bad(msg, detail); }

const gateH   = strip(read(F.gateH));
const gateCpp = strip(read(F.gateCpp));
const mainCpp = strip(read(F.main));
const agentUi = strip(read(F.agentUi));
const pageQml = strip(read(F.page));
const islandH = strip(read(F.islandH));

// ── 一、常量真源（pluginhost.h::PluginIslandGate）────────────────────────────
console.log('── 一、治理常量真源（pluginhost.h）──');
const consts = {};
for (const name of ['kMaxTitleChars', 'kMaxDescChars', 'kMaxMessageBytes',
                    'kRatePerSec', 'kRateBurst', 'kMaxPriority', 'kMinPriority']) {
  const m = gateH.match(new RegExp(`static\\s+constexpr\\s+int\\s+${name}\\s*=\\s*([^;]+);`));
  if (m) { consts[name] = m[1].trim(); ok(`PluginIslandGate::${name} = ${consts[name]}`); }
  else bad(`PluginIslandGate::${name} 未声明`, '常量丢了 ⇒ 执行侧要么编不过、要么退回硬编码');
}
check(consts.kRatePerSec === '5', '限流 5 条/秒（与方案 §4.5 同值）', `实际 ${consts.kRatePerSec}`);
check(consts.kRateBurst === '10', '突发桶 10（与方案 §4.5 同值）', `实际 ${consts.kRateBurst}`);
check(consts.kMaxTitleChars === '120', '标题上限 120 码点', `实际 ${consts.kMaxTitleChars}`);
check(consts.kMaxDescChars === '400', '正文上限 400 码点', `实际 ${consts.kMaxDescChars}`);
check(consts.kMaxMessageBytes === '64 * 1024', '单条报文上限 64KB', `实际 ${consts.kMaxMessageBytes}`);
check(consts.kMaxPriority === '8', '插件优先级上限 8（9-10 只给管理端）', `实际 ${consts.kMaxPriority}`);

// ── 二、执行侧：四道约束必须都真的在执行路径上 ──────────────────────────────
console.log('── 二、执行侧（pluginhost.cpp）──');
const hasFn = (name) => new RegExp(`PluginHost::${name}\\s*\\(`).test(gateCpp);
check(hasFn('handleIslandShow'), 'island_show 有独立治理入口 handleIslandShow()');
check(hasFn('handleNotifyAt'), 'notify 有独立治理入口 handleNotifyAt()');
check(hasFn('takeToken'), '令牌桶 takeToken() 存在');
check(hasFn('clipCodepoints'), '按码点截断 clipCodepoints() 存在（left(n) 会劈代理对）');
check(hasFn('clampPluginPriority'), '插件优先级钳制 clampPluginPriority() 存在');
check(hasFn('islandGateOpen'), '第三方推岛开关 islandGateOpen() 存在');
check(hasFn('islandGateStats'), '审计计数快照 islandGateStats() 存在');

// handleApi 必须**只走**治理入口，不许再直调 m_api.islandShow / m_api.notify
// ⚠️ 边界有两个实测坑（本轮都踩了，写下来）：
//   ① 用「→ handleResult」：治理函数实现就排在两者之间，会把治理函数里的
//      `m_api.islandShow(` 算进 handleApi 段 ⇒ 假红；
//   ② 用「indexOf('PluginHost::')」找下一个：`bool PluginHost::islandGateOpen()` 这类
//      **有返回类型**的定义不带 `void ` 前缀，锚点照样跑偏 ⇒ 假红。
//   所以用「[返回类型] PluginHost::<名字>（」找下一个**定义**（名字允许小写开头）。
const apiStart = gateCpp.indexOf('void PluginHost::handleApi');
let apiEnd = -1;
if (apiStart >= 0) {
  const re = /(?:^|\n)[A-Za-z_][\w:<>,\s*&]*?\sPluginHost::(\w+)\s*\(/g;
  re.lastIndex = apiStart + 10;
  let m;
  while ((m = re.exec(gateCpp)) !== null) {
    if (m[1] !== 'handleApi') { apiEnd = m.index; break; }
  }
}
const handleApi = apiStart >= 0 && apiEnd > apiStart ? gateCpp.slice(apiStart, apiEnd) : '';
check(handleApi.length > 0, 'handleApi 段可定位', `start=${apiStart} end=${apiEnd}`);
check(/handleIslandShow\s*\(\s*c\s*,\s*args\s*,\s*r\s*\)/.test(handleApi),
      'handleApi → island_show 走 handleIslandShow');
check(/handleNotifyAt\s*\(\s*c\s*,\s*args\s*,\s*r\s*\)/.test(handleApi),
      'handleApi → notify 走 handleNotifyAt');
check(!/m_api\.islandShow\s*\(/.test(handleApi),
      'handleApi 不再直调 m_api.islandShow（旁路即治理失效）');
check(!/m_api\.notify\s*\(/.test(handleApi),
      'handleApi 不再直调 m_api.notify');

// 报文超长必须**在解析之前**拒（先 fromJson 再检查 = 让超大 JSON 先吃满内存）
const onText = gateCpp.slice(gateCpp.indexOf('void PluginHost::onTextMessage'));
const sizeCheckAt = onText.indexOf('kMaxMessageBytes');
const parseAt = onText.indexOf('QJsonDocument::fromJson');
check(sizeCheckAt > 0 && parseAt > 0 && sizeCheckAt < parseAt,
      '超长报文在 fromJson **之前**被拒',
      `sizeCheck@${sizeCheckAt} parse@${parseAt}`);
// 负向对照补齐（本轮实测踩到）：只断言"先后顺序"会被 `if (false && …)` 蒙混过关——
// 顺序还在，但治理**永远不执行**。所以再钉死"这个 if 的条件就是那次比较本身"。
check(/if\s*\(\s*msg\.toUtf8\(\)\.size\(\)\s*>\s*PluginIslandGate::kMaxMessageBytes\s*\)\s*\{/
        .test(onText), '超长判定是**真会生效**的 if 条件（不是被短路/常假的分支）');

// 前缀归属 + fromAdmin 恒 false
const his = gateCpp.slice(gateCpp.indexOf('void PluginHost::handleIslandShow'),
                          gateCpp.indexOf('void PluginHost::handleNotifyAt'));
check(/\[%1\]/.test(his) || /QStringLiteral\("\[%1\]/.test(his),
      '插件推的标题带「[来源] 」前缀');
check(/false\s*,\s*durationMs/.test(his), 'fromAdmin 恒 false（不污染管理端置顶排序）');
check(!/emergency.*true/.test(his.replace(/toBool\(false\)/g, '')),
      'emergency 参数不被透传为 true（插件拿不到紧急档）');

// 计数：每条出口都要落账（放行 + 各类拒绝），否则"为什么插件推的没出现"无从回答
for (const ctr of ['allowed', 'deniedSwitch', 'deniedRate', 'deniedSize', 'trimmed', 'clampPriority']) {
  check(new RegExp(`\\+\\+m_gateStats\\.${ctr}`).test(gateCpp), `审计计数 ++m_gateStats.${ctr} 在写`);
}
// 四道约束在**执行路径**上各自的判据（顺序断言挡不住"条件被短路成常假"）：
const hisBody = gateCpp.slice(gateCpp.indexOf('void PluginHost::handleIslandShow'),
                              gateCpp.indexOf('void PluginHost::handleNotifyAt'));
check(/if\s*\(\s*!islandGateOpen\(\)\s*\)\s*\{/.test(hisBody), '开关判据是真 if 条件（!islandGateOpen()）');
check(/if\s*\(\s*!takeToken\(\s*c\s*\)\s*\)\s*\{/.test(hisBody), '限流判据是真 if 条件（!takeToken(c)）');
check(/clipCodepoints\(\s*rawTitle\s*,\s*PluginIslandGate::kMaxTitleChars\s*\)/.test(hisBody),
      '标题用 kMaxTitleChars 截断');
check(/clipCodepoints\(\s*rawDesc\s*,\s*PluginIslandGate::kMaxDescChars\s*\)/.test(hisBody),
      '正文用 kMaxDescChars 截断');
check(/tokens\s*<\s*1\.0\s*\)\s*return\s+false/.test(gateCpp), '令牌桶不足时返回 false（真的会拒）');
check(/tokens\s*\+\s*dt\s*\*\s*PluginIslandGate::kRatePerSec/.test(gateCpp), '令牌桶按 kRatePerSec 补充');

// ── 三、接线（main.cpp）：漏一处 = 设置页开关形同虚设 / 时间戳不变 ─────────────
console.log('── 三、接线（main.cpp / island.h）──');
check(/api\.islandShowAt\s*=/.test(mainCpp), 'PluginAgentApi::islandShowAt 已接线');
check(/islandShowAt\s*\(\s*title\s*,\s*desc\s*,\s*icon\s*,\s*durationMs\s*,\s*f\s*,\s*fromAdmin\s*,\s*noticeAtMs\s*\)/
        .test(mainCpp), 'islandShowAt 把通知发生时刻透传进岛');
check(/api\.senderPriorityLimit\s*=\s*\[\]\(\)\s*\{\s*return\s+AgentSettings::pluginIslandPush\(\)/.test(mainCpp),
      'senderPriorityLimit → AgentSettings::pluginIslandPush()（开关真生效）');
check(/static\s+void\s+islandShowAt\s*\(/.test(mainCpp), 'main.cpp 有 islandShowAt() 包装');
check(/setNoticeTimeForNext\s*\(\s*noticeAtMs\s*\)/.test(mainCpp), 'islandShowAt 走 setNoticeTimeForNext 注入');
check(/setNoticeTimeForNext\s*\(qint64\s+atMs\)/.test(islandH), 'IslandOverlay::setNoticeTimeForNext 公开存在');
check(/\?\s*\(m_noticeHintMs\s*\/\s*1000\)/.test(strip(read('Stelarith-control-qt/src/island.cpp'))),
      'showIsland 消费一次性提示位（island.cpp）');
check(/m_noticeHintMs\s*=\s*0;\s*\n/.test(strip(read('Stelarith-control-qt/src/island.cpp'))),
      '提示位消费后清零（否则下一条通知继承上一条的时间）');

// ── 四、开关三处一致：键名 / 默认值 / Q_PROPERTY / QML ───────────────────────
console.log('── 四、第三方推岛开关三处一致 ──');
check(/QSettings\(\)\.value\(QStringLiteral\("feature\/pluginIslandPush"\),\s*true\)\.toBool\(\)/
        .test(mainCpp), 'QSettings 键 feature/pluginIslandPush，默认 true');
check(/Q_PROPERTY\(bool\s+pluginIslandPush\s+READ\s+pluginIslandPush\s+WRITE\s+setPluginIslandPush/
        .test(agentUi), 'agent_ui.h 暴露 Q_PROPERTY pluginIslandPush');
check(/switchChanged\(QStringLiteral\("pluginIslandPush"\)/.test(agentUi),
      'agent_ui setter 发 switchChanged("pluginIslandPush")');
check(/key\s*==\s*QLatin1String\("pluginIslandPush"\)/.test(mainCpp),
      'main.cpp switchChanged 接住 pluginIslandPush 并落盘');
check(/m_pluginIslandPush\s*=\s*AgentSettings::pluginIslandPush\(\)/.test(mainCpp),
      'agentUiFill 回填快照（开关状态以真实存储为准）');
check(/bool\s+m_pluginIslandPush\s*=\s*true/.test(agentUi),
      'agent_ui 成员默认值与 AgentSettings 同值（true）');
check(/agentUi\.pluginIslandPush\s*=\s*v/.test(pageQml),
      '设置页开关绑定 agentUi.pluginIslandPush');

// ── 五、反向：不许出现第二条不受约束的推岛路径 ───────────────────────────────
console.log('── 五、反向检查 ──');
check(!/m_api\.islandShow\s*\(\s*args/.test(gateCpp), '不存在"直传 args"的推岛旁路');
check(/senderPriorityLimit.*= nullptr/.test(gateH), 'PluginAgentApi 声明 senderPriorityLimit（漏注入可静态发现）');

console.log(`\n════ ${pass} 通过 / ${fail} 失败 ════`);
process.exit(fail ? 1 : 0);
