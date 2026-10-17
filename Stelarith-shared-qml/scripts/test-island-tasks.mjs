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

// ── 五、反向：不许出现绕过任务表的旁路 ───────────────────────────────────────
console.log('── 五、反向检查 ──');
check(/QVector<IslandOverlay::Task>|\bQVector<Task>/.test(H), 'island.h 声明任务表容器');
check(/const QVector<Task> &tasks\(\) const/.test(H), 'tasks() 只读访问器存在（桥与快照共用）');
// 快照里带任务表，供沙箱验证"每条状态源都进表"
check(/o\.insert\(QStringLiteral\("tasks"\), tarr\)/.test(C), 'islandSnapshot 外露任务表（验证用）');

console.log(`\n════ ${pass} 通过 / ${fail} 失败 ════`);
process.exit(fail ? 1 : 0);
