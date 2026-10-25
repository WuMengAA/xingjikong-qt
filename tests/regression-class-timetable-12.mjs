/**
 * 回归测试 · 完整档案 12 个班（2026-10-25）
 *
 * 为什么单独一份：`Default.json`（交付到教室机的那份）只剩 2 个群，容易让人误以为
 * "全校只有两个班"。完整档案是 **72 个 ClassPlan = 12 个群 × 6 天**，而 12 个群
 * **全叫「新课表群」**——群名分不出谁是谁，只能靠顺序。顺序错了就是把 A 班课表挂到 B 班。
 *
 * 权威口径：CIMS `docs/phase4-班级课表导入验证报告.md` 里逐班列出了 12 个班周一的课表，
 * 本文件把那 12 行**逐字**断言。同时锁住排序规则本身：
 *   · pre-stelarith（CIMS 真正导入的那份）：ClassPlanGroups 声明序 = 首次出现序（当年都能过）
 *   · pre-groupfix（改名之后）：声明序 **全错**，只有首次出现序对
 *   ⇒ 所以实现里排序只认「ClassPlans 里 AssociatedGroup 的首次出现顺序」。
 *
 * 用法：node tests/regression-class-timetable-12.mjs [完整档案路径] [现役档案路径]
 * 退出码：0 全绿 / 1 有失败 / 2 档案缺失（跳过）
 */
import fs from 'node:fs';
import {
  classIslandGroupMeta, normalizeClassIslandProfile, TIMETABLE_SCHEMA,
} from '../src/class-timetable.js';

const FULL = process.argv[2] || process.env.CI_FULL_PROFILE
  || 'D:\\Classlsland\\data\\Profiles\\Default.json.pre-stelarith-20260915-001850';
const LIVE = process.argv[3] || 'D:\\Classlsland\\data\\Profiles\\Default.json';

let pass = 0;
const fails = [];
function check(ok, what, extra = '') {
  if (ok) { pass++; console.log(`  [PASS] ${what}`); }
  else { fails.push(what + (extra ? ` — ${extra}` : '')); console.log(`  [FAIL] ${what}${extra ? ' — ' + extra : ''}`); }
}
const read = (p) => JSON.parse(fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, ''));

if (!fs.existsSync(FULL)) {
  console.log(`SKIP：找不到完整档案 ${FULL}`);
  process.exit(2);
}

/** CIMS Phase4 报告「12 班周一课表」原文（逐字断言，改一个字就红）。 */
const MON_EXPECT = {
  1: '早读 数学 语文 化学 生物 数学 英语 物理 物理 班会 数学 化学',
  2: '早读 语文 数学 生物 化学 数学 物理 英语 物理 班会 化学 数学',
  3: '早读 数学 语文 化学 地理 数学 英语 物理 物理 班会 数学 化学',
  4: '早读 语文 数学 生物 化学 数学 物理 英语 物理 班会 化学 数学',
  5: '早读 数学 语文 化学 生物 数学 英语 物理 物理 班会 数学 生物',
  6: '早读 语文 数学 生物 化学 数学 物理 英语 物理 班会 化学 数学',
  7: '早读 数学 化学 语文 地理 数学 英语 物理 物理 班会 数学 化学',
  8: '早读 语文 数学 地理 化学 数学 物理 英语 物理 自习 化学 数学',
  9: '早读 数学 语文 地理 政治 数学 英语 历史 历史 班会 地理 数学',
  10: '早读 语文 数学 化学 物理 数学 政治 英语 物理 班会 数学 化学',
  11: '早读 数学 语文 化学 生物 数学 英语 物理 物理 班会 数学 化学',
  12: '早读 语文 数学 美术 化学 数学 物理 英语 物理 班会 化学 数学',
};

const full = read(FULL);
const meta = classIslandGroupMeta(full);

console.log('── ① 完整档案：12 个班 ──');
check(meta.groups.length === 12, `群数=12（实际 ${meta.groups.length}）`);
check(meta.nameSource === 'order', '12 个群同名 ⇒ 班名按出现顺序推断（nameSource=order）');
check(meta.groups.every((g) => g.inferred === true), '每个群都标了 inferred=true（界面要提示核对）');
check(meta.groups.every((g) => g.days === 6), '每班 6 天（周日 + 周一~周五）');
check(meta.groups.map((g) => g.label).join(',') === '1班,2班,3班,4班,5班,6班,7班,8班,9班,10班,11班,12班',
  '标签按序 1班…12班', meta.groups.map((g) => g.label).join(','));
check(new Set(meta.groups.map((g) => g.id)).size === 12, '12 个群 id 互不相同');

console.log('\n── ② 逐班周一课表 = CIMS Phase4 报告原文 ──');
let mismatch = 0;
for (const g of meta.groups) {
  const n = normalizeClassIslandProfile(full, g.label);   // 用「X班」标签选群
  if (!n.ok) { check(false, `${g.label} 归一化`, n.error); mismatch++; continue; }
  const mon = n.normalized.days.find((d) => d.dow === 1);
  const seq = mon ? mon.lessons.map((l) => l.subject).join(' ') : '';
  const ok = seq === MON_EXPECT[g.index];
  if (!ok) { mismatch++; console.log(`      got: ${seq}\n      exp: ${MON_EXPECT[g.index]}`); }
  check(ok, `${g.label} 周一 12 节与报告一致`);
}
check(mismatch === 0, '12 个班无一错位', `${mismatch} 个不符`);

console.log('\n── ③ 归一化产物质量（抽 9 班：地理/政治/历史 那套）──');
const n9 = normalizeClassIslandProfile(full, '9班');
if (n9.ok) {
  const p = n9.normalized;
  check(p.schema === TIMETABLE_SCHEMA, `schema=${TIMETABLE_SCHEMA}`);
  check(p.group.label === '9班' && p.group.inferred === true, '信封里带班名标签 + inferred');
  check(p.days.length === 6, `6 天（实际 ${p.days.length}）`);
  const total = p.days.reduce((n, d) => n + d.lessons.length, 0);
  check(total === 59, `全周 59 节（周日3 + 周一~四12×4 + 周五8；实际 ${total}）`);
  const mon = p.days.find((d) => d.dow === 1);
  check(mon.lessons[0].start === '07:25' && mon.lessons[0].end === '08:05',
    `周一第1节 07:25-08:05（实际 ${mon.lessons[0].start}-${mon.lessons[0].end}）`);
  check(p.layouts.length === 3, `3 份作息（实际 ${p.layouts.length}）`);
  check(p.subjects.length >= 8, `科目表非空（${p.subjects.length} 项）`);
}

console.log('\n── ④ 按序号 / 按群 id 选群，结果必须一致（三种写法同一群）──');
const byLabel = normalizeClassIslandProfile(full, '3班');
const byIndex = normalizeClassIslandProfile(full, '#3');
const byId = normalizeClassIslandProfile(full, meta.groups[2].id);
check(byLabel.ok && byIndex.ok && byId.ok, '三种写法都能选中');
check(byLabel.ok && byIndex.ok && byLabel.normalized.group.id === byIndex.normalized.group.id,
  '「3班」与「#3」指向同一群');
check(byLabel.ok && byId.ok && byLabel.normalized.group.id === byId.normalized.group.id,
  '「3班」与群 GUID 指向同一群');

console.log('\n── ⑤ 现役档案（2 群）仍按群名走，不被顺序推断污染 ──');
if (fs.existsSync(LIVE)) {
  const live = read(LIVE);
  const lm = classIslandGroupMeta(live);
  check(lm.groups.length === 2, `现役档案 2 个群（实际 ${lm.groups.length}）`);
  check(lm.nameSource === 'declared', '群名可信 ⇒ nameSource=declared');
  check(lm.groups.map((g) => g.label).join(',') === '1班,8班',
    '标签是 1班、8班（不是 1班、2班）', lm.groups.map((g) => g.label).join(','));
  check(lm.groups.every((g) => g.inferred === false), '不需要人工核对（inferred=false）');
} else {
  console.log('  (现役档案不在，跳过)');
}

console.log(`\n==== ${pass}/${pass + fails.length} ====`);
if (fails.length) { fails.forEach((f) => console.log('  ✗ ' + f)); process.exit(1); }
process.exit(0);
