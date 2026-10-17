#!/usr/bin/env node
// 星集控 · 灵动岛 T 档（任务中心）源码契约门禁
// ─────────────────────────────────────────────────────────────────────────────
// 为什么需要：T 档把岛从"一条内容"改成"任务表"，改动跨 island.h / island.cpp /
//   island_bridge.cpp 三处，且**故障是静默的** ——
//     · Task 字段改了而某个状态源没跟 ⇒ 那类状态永远进不了表（界面少一行，没人报警）
//     · 桥仍输出单元素 ⇒ 界面看着还是老样子，等于白改
//     · 同 tag 合并逻辑没了 ⇒ 通知不再顶掉，变成无限堆积
// 所以这里用 grep 源码把三处钉死。与既有 .mjs 同一范式（独立脚本、不进构建）。
//
// 用法：cd Stelarith-xingjikong-qt && node Stelarith-shared-qml/scripts/test-island-tasks.mjs
// 退出码：0 全过；1 有失败。
// ⚠️ 匹配前先剥注释 —— 断言文案本身就写在注释里，不剥会"自己证明自己"（D 档 §七-5 的教训）。
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

const ROOT = process.cwd();
const F = {
  islandH: 'Stelarith-control-qt/src/island.h',
  islandCpp: 'Stelarith-control-qt/src/island.cpp',
  bridgeCpp: 'Stelarith-control-qt/src/island_bridge.cpp',
};
const read = (rel) => fs.readFileSync(path.join(ROOT, rel), 'utf8');

function stripCComments(src) {
  let out = '', i = 0, mode = 0;
  while (i < src.length) {
    const c = src[i], n = src[i + 1];
    if (mode === 0) {
      if (c === '/' && n === '/') { mode = 1; i += 2; continue; }
      if (c === '/' && n === '*') { mode = 2; i += 2; continue; }
      if (c === '"') { mode = 3; out += c; i++; continue; }
      if (c === "'") { mode = 4; out += c; i++; continue; }
      out += c; i++;
    } else if (mode === 1) { if (c === '\n') { mode = 0; out += c; } i++; }
    else if (mode === 2) {
      if (c === '*' && n === '/') { mode = 0; i += 2; continue; }
      if (c === '\n') out += c; i++;
    } else {
      out += c;
      if (c === '\\') { out += src[i + 1] ?? ''; i += 2; continue; }
      if ((mode === 3 && c === '"') || (mode === 4 && c === "'")) mode = 0;
      i++;
    }
  }
  return out;
}
const strip = (s) => stripCComments(s);

let pass = 0, fail = 0;
const ok = (m) => { pass++; console.log(`  PASS  ${m}`); };
const bad = (m, d) => { fail++; console.log(`  FAIL  ${m}`); if (d) console.log(`        ↳ ${d}`); };
const check = (c, m, d) => { c ? ok(m) : bad(m, d); };

const H = strip(read(F.islandH));
const C = strip(read(F.islandCpp));
const B = strip(read(F.bridgeCpp));

// ── 一、Task 结构（island.h 真源）──────────────────────────────────────────
console.log('── 一、Task 结构（island.h）──');
const structBody = H.slice(H.indexOf('struct Task'), H.indexOf('// 任务表（本步只承载'));
const fields = ['id', 'tag', 'kind', 'source', 'title', 'desc', 'icon',
                'priority', 'form', 'severity', 'sticky', 'expireAtMs',
                'progress', 'actions', 'state', 'fromAdmin', 'shownTs'];
for (const f of fields) {
  check(new RegExp(`\\bQString\\s+${f}\\s*;|\\bint\\s+${f}\\s*=|\\bbool\\s+${f}\\s*=|\\bqint64\\s+${f}\\s*=|\\bdouble\\s+${f}\\s*=|\\bForm\\s+${f}\\s*=|\\bQStringList\\s+${f}\\s*;`).test(structBody),
        `Task 字段 ${f} 存在`);
}
// C++17 约束：本工程未开 C++20，指派初始化器会编译失败（实测 C7555/C7560）。
// ⚠️ 说明写在注释里 ⇒ 查**未剥注释**的原文；上面 H 是剥过的，所以这里重新读一次。
const Hraw = read(F.islandH);
check(/不支持指派初始化器/.test(Hraw),
      'Task 有 C++17 说明（禁止 .field= 写法）');
// 只认真正的指派初始化器形式 `Task{ .xxx = ... }` 或 `Task nt{ .xxx = ... }`；
// 普通成员赋值 `nt.desc = desc;` 是 C++17 合法写法，不能误伤（上一版正则就误伤了）。
check(!/Task\s*\w*\s*\{\s*\.[a-zA-Z]+\s*=/.test(C),
      'island.cpp 未使用指派初始化器（C++17 会编不过）');

// ── 二、同 tag 合并与上限（upsertTask 的实现要点）──────────────────────────
console.log('── 二、upsertTask（island.cpp）──');
const upsert = C.slice(C.indexOf('void IslandOverlay::upsertTask'), C.indexOf('void IslandOverlay::archiveToHistory'));
check(upsert.length > 0, 'upsertTask 段可定位');
check(/if\s*\(\s*t\.tag\.isEmpty\(\)\s*\)/.test(upsert), '无 tag 的任务被拒绝登记（防脏数据）');
check(/e\.tag\s*==\s*t\.tag/.test(upsert), '同 tag 合并（更新而非追加）');
check(/while\s*\(\s*m_tasks\.size\(\)\s*>\s*8\s*\)/.test(upsert), '任务表上限 8（与方案 §4.2 同值）');
check(/c\.sticky\s*\|\|\s*c\.priority\s*>=\s*9/.test(upsert), 'sticky / 高优先任务不被溢出丢弃');

// ── 三、各状态源必须都登记进任务表 ─────────────────────────────────────────
console.log('── 三、状态源登记（island.cpp）──');
const srcs = [
  ['通知 showIsland', /upsertTask\(nt\)/],
  ['文件传输 setFileTransfer', /upsertTask\(ft\)/],
  ['媒体 setMediaSession', /upsertTask\(Task\(QStringLiteral\("media"\)/],
  ['对讲 setSpectrumActive', /upsertTask\(Task\(QStringLiteral\("voice"\)/],
];
// 对讲有两条进来的路：audio.start→setSpectrumActive、本地 PCM→feedSpectrum 首次来帧。
// 两条都必须登记 voice（实测只埋了一条 ⇒ dev_act spectrum 路径漏登记）。
const feedSeg = C.slice(C.indexOf('void IslandOverlay::feedSpectrum'),
                        C.indexOf('void IslandOverlay::setSpectrumActive'));
check(/QStringLiteral\("voice"\)/.test(feedSeg),
      'feedSpectrum 首次来帧也登记 voice（对讲第二条路径）');
for (const [name, re] of srcs) check(re.test(C), `${name} 登记进任务表`);
// 结束/清理也要标记，否则任务永远 running
check(/tag\s*==\s*QStringLiteral\("file"\)\s*\)\s*\{\s*t\.state\s*=\s*2/.test(C),
      '文件传输结束 ⇒ 任务 state=done');
check(/tag\s*==\s*QStringLiteral\("media"\)\s*\)\s*\{\s*t\.state\s*=\s*2/.test(C),
      '媒体结束 ⇒ 任务 state=done');
check(/tag\s*==\s*QStringLiteral\("voice"\)\s*\)\s*\{\s*t\.state\s*=\s*2/.test(C),
      '对讲结束 ⇒ 任务 state=done');

// ── 四、桥 states 输出全表（island_bridge.cpp）──────────────────────────────
console.log('── 四、桥 states 输出全表 ──');
const st = B.slice(B.indexOf('QVariantList IslandBridge::states'), B.indexOf('QString IslandBridge::whaleBubble'));
check(st.length > 0, 'states() 段可定位');
check(/m_host->tasks\(\)/.test(st), 'states() 读任务表（不再只读 m_title）');
check(/for\s*\(\s*const auto &t\s*:\s*tasks\s*\)/.test(st), 'states() 遍历全表（不是只取一条）');
// 旧契约字段必须保留，否则 QML 的 StateList 会解析不到
for (const k of ['k', 't', 'd', 'p']) {
  check(new RegExp(`insert\\(QStringLiteral\\("${k}"\\)`).test(st), `states 保留旧字段 ${k}`);
}
// 新字段（供后续排序/内联动作）
for (const k of ['id', 'tag', 'state', 'priority', 'sticky']) {
  check(new RegExp(`insert\\(QStringLiteral\\("${k}"\\)`).test(st), `states 新增字段 ${k}`);
}
check(/t\.state\s*==\s*2\s*\)\s*continue/.test(st), '已完成（done）的任务不进状态列表');
check(!/out\.append\(m\);\s*\n\s*return out;\s*\n\}/.test(st) || /for\s*\(\s*const auto &t/.test(st),
      'states 不再是"单元素硬编"');

// ── 五、显示仲裁：回落 + 1Hz 过期扫描 ──────────────────────────────────────
console.log('── 五、显示仲裁（回落 / 扫描）──');
check(/const IslandOverlay::Task \*IslandOverlay::nextDisplayTask/.test(C),
      'nextDisplayTask 存在（排序：priority 降序、同优先取最新）');
check(/void IslandOverlay::sweepExpiredTasks/.test(C), 'sweepExpiredTasks 存在');
// 排序口径
const nextSeg = C.slice(C.indexOf('IslandOverlay::nextDisplayTask'),
                        C.indexOf('void IslandOverlay::sweepExpiredTasks'));
check(/t\.priority\s*>\s*best->priority/.test(nextSeg), '排序：priority 高者优先');
check(/t\.priority\s*==\s*best->priority\s*&&\s*t\.shownTs\s*>\s*best->shownTs/.test(nextSeg),
      '排序：同 priority 取最新（shownTs）');
check(/t\.state\s*==\s*2\s*\)\s*continue/.test(nextSeg), '排序跳过已完成任务');
// 扫描：只标 done，不动显示（否则扫描器会和用户抢窗口）
const sweepSeg = C.slice(C.indexOf('void IslandOverlay::sweepExpiredTasks'),
                         C.indexOf('void IslandOverlay::archiveToHistory'));
check(/t\.sticky\s*\)\s*continue/.test(sweepSeg), '扫描跳过 sticky 任务');
check(/setInterval\(1000\)/.test(C), '扫描定时器 1Hz（单定时器扫表，不挂每任务定时器）');
check(!/dismiss\(\)|enterIdle\(\)/.test(sweepSeg),
      '扫描器不主动改显示（不调 dismiss/enterIdle）—— 避免与用户操作抢窗口');
// 回落：只在 enterIdle 的收回路径上，且排除 notice（否则通知会自己回落成自己，永远关不掉）
const idleSeg = C.slice(C.indexOf('void IslandOverlay::enterIdle'),
                        C.indexOf('void IslandOverlay::setExpanded'));
// "回落"标记写在注释里（会被剥掉）⇒ 查未剥注释的原文；
// 同时用代码特征兜底：enterIdle 里出现了对任务表的遍历。
const Craw = read(F.islandCpp);
const idleRaw = Craw.slice(Craw.indexOf('void IslandOverlay::enterIdle'),
                           Craw.indexOf('void IslandOverlay::setExpanded'));
check(/T 档：回落/.test(idleRaw), 'enterIdle 接了回落分支（注释标记）');
check(/for \(const auto &t : m_tasks\)/.test(idleSeg), 'enterIdle 回落分支确实遍历任务表');
check(/t\.tag\s*==\s*QStringLiteral\("notice"\)\s*\)\s*continue/.test(idleSeg),
      '回落排除 notice 任务（防"通知过期又把自己显示回来"）');

// ── 六、渲染层（StateList.qml）：进度条 / 状态 / 图标 / 不与 ftRow 重复 ──────
console.log('── 六、渲染层 StateList / 图标资源 ──');
const qmlDir = 'Stelarith-shared-qml/island';
const STATE_QML = fs.readFileSync(path.join(ROOT, qmlDir, 'StateList.qml'), 'utf8');
// 进度条：p ∈ [0,1] 才画，且行高必须把进度条算进去（否则被裁在行外＝看着没画）
check(/row\.hasBar/.test(STATE_QML), 'StateList 有 hasBar（p>=0 才画进度条）');
check(/barBox\.height/.test(STATE_QML), '行高把进度条算进去（否则进度条被裁掉）');
check(/parent\.width \* Math\.min\(1, row\.prog\)/.test(STATE_QML), '进度条宽度按 prog 钳制到 [0,1]');
// 状态标：失败/暂停要能看出来；running 不挂字（免得每行都挂）
check(/stateLabelFor/.test(STATE_QML), 'StateList 有状态文案映射');
check(/it\.state === 3/.test(STATE_QML), '失败态有独立配色');
// ⚠️ 失败态必须用主题真实存在的色（severityUrgent）。曾误写 theme.err —— 该属性不存在，
//    取 undefined 会静默画黑色（2026-10-18 查证并修）。这条断言防回归。
// ⚠️ 断言只看**代码**：注释里会提到 theme.err 这个"曾经的错"，剥注释后再查，
//    否则注释自己就把门禁顶红了（同 §七 的教训）。
const STATE_QML_CODE = stripCComments(STATE_QML);
check(!/theme\.err\b/.test(STATE_QML_CODE), 'StateList 不得引用不存在的 theme.err');
check(/severityUrgent/.test(STATE_QML), '失败态复用 severityUrgent（主题真实属性）');
// 图标：桥直透的 Task.icon 必须能被 iconNameFor 认识，否则一律画成空心圆
for (const ic of ['download', 'music', 'mic', 'bell']) {
  check(new RegExp(`case "${ic}"`).test(STATE_QML), `iconNameFor 认得桥直透的 ${ic}`);
}
// ⚠️ 图标名必须是 resources/icons 里真实存在的 svg（曾把 check-circle 写进去，
//    实际文件叫 circle-check ⇒ 一直画空白）
check(/case "check-circle": return "circle-check"/.test(STATE_QML),
      'check-circle 映射到真实资源名 circle-check');
const iconDir = 'Stelarith-control-qt/resources/icons';
for (const ic of ['download', 'music', 'mic', 'bell', 'circle-check', 'triangle-alert']) {
  check(fs.existsSync(path.join(ROOT, iconDir, `${ic}.svg`)), `图标资源 ${ic}.svg 存在`);
}
// ⚠️ ft 进度不得两处渲染：ftRow 已画进度卡，StateList 若再输出 file 项 ⇒
//    同一件事显示两遍，且 ftH 与 StateList 行高被重复计入几何预算（实测布局错位）
check(/t\.tag == QStringLiteral\("file"\)\s*\)\s*continue/.test(B),
      '桥 states() 排除 file 项（ft 进度只由 ftRow 画，避免重复计高）');
// ⚠️ 进度必须**持续更新**，不能只在登记时设一次 0（曾如此 ⇒ 播放中进度条永远空，
//    比不画更糟：看着像卡住）。2026-10-18 修，这条断言防回归。
const advSeg = C.slice(C.indexOf('void IslandOverlay::advanceMediaElapsed'),
                       C.indexOf('void IslandOverlay::armMediaPhaseTimer'));
check(/t\.tag == QStringLiteral\("media"\)\s*\)\s*\{\s*t\.progress = p/.test(advSeg),
      '播放推进时同步更新 media 任务进度（不只登记时设一次）');

// ── 八、VoiceHub 广播任务（2026-10-18）────────────────────────────────────
console.log('── 八、VoiceHub 广播链路 ──');
const MAIN_CPP = strip(read('Stelarith-control-qt/src/main.cpp'));
const BRIDGE_H = strip(read('Stelarith-control-qt/src/island.h'));
const BRIDGE_CPP_JS = fs.readFileSync(path.join(ROOT, 'Stelarith-cloud-ws/src/voicehub-bridge.js'), 'utf8');
// 被控端：消息分支 + 岛侧接口
check(/voicehub_nowplaying/.test(MAIN_CPP), 'main.cpp 处理 voicehub_nowplaying 消息');
check(/setBroadcastTask/.test(MAIN_CPP) && /clearBroadcastTask/.test(MAIN_CPP),
      '状态非 playing 时撤任务、playing 时设任务（两分支都在）');
check(/struct BroadcastInfo/.test(BRIDGE_H), 'island.h 有 BroadcastInfo');
check(/broadcastProgress\(\)/.test(BRIDGE_H), 'island.h 暴露 broadcastProgress（外推集中一处）');
// ⚠️ 广播进度必须**现算**：云端 30s 才给一次 position，照搬会让进度条在两次快照间静止
check(/t\.tag == QStringLiteral\("broadcast"\)/.test(B) && /broadcastProgress\(\)/.test(B),
      '桥对 broadcast 任务现算进度（不照搬 30s 一次的 position）');
// ⚠️ 岛侧外推必须用本地时钟锚点
const bcSeg = C.slice(C.indexOf('void IslandOverlay::setBroadcastTask'),
                      C.indexOf('void IslandOverlay::archiveToHistory'));
check(/m_bcAnchorAtMs\s*=\s*QDateTime::currentMSecsSinceEpoch\(\)/.test(bcSeg),
      '广播进度锚点用本地时钟');
check(/m_bcAnchorPos \+ double\(el\) \/ 1000\.0/.test(bcSeg),
      '进度 = 锚点位置 + 本地流逝秒数');
check(/tag\.isEmpty|QStringLiteral\("broadcast"\)/.test(bcSeg), '广播任务用 tag=broadcast');
// 云端侧：只读 + 关键口径
check(!/method:\s*'POST'|method:\s*"POST"/.test(BRIDGE_CPP_JS), '云端桥只发 GET（绝不写 VoiceHub）');
check(/POLL_MS = 30_000/.test(BRIDGE_CPP_JS), '云端轮询间隔 30s');
check(/STALE_MS = 95_000/.test(BRIDGE_CPP_JS), '过期阈值 95s（服务端 90s + 5s 余量）');
check(/失败\*\*不动 snapshot|保留上一次/.test(BRIDGE_CPP_JS), '拉取失败保留上一次状态（有说明与实现）');

// ── 九、反向：不许出现绕过任务表的旁路 ───────────────────────────────────────
console.log('── 九、反向检查 ──');
check(/QVector<IslandOverlay::Task>|\bQVector<Task>/.test(H), 'island.h 声明任务表容器');
check(/const QVector<Task> &tasks\(\) const/.test(H), 'tasks() 只读访问器存在（桥与快照共用）');
// 快照里带任务表，供沙箱验证"每条状态源都进表"
check(/o\.insert\(QStringLiteral\("tasks"\), tarr\)/.test(C), 'islandSnapshot 外露任务表（验证用）');

console.log(`\n════ ${pass} 通过 / ${fail} 失败 ════`);
process.exit(fail ? 1 : 0);
