/**
 * 回归测试 · ClassIsland 档案归一化（2026-10-25）
 *
 * 为什么必须有：课表导入是"猜错就挂错班"的数据操作，靠肉眼核对 JSON 不可靠。
 * 这里用**老师真实档案**（D:\Classlsland\data\Profiles\Default.json，85KB，
 * 2 个课表群 × 6 天）跑一遍，断言群枚举、群选择、节次与作息对齐、科目翻译。
 *
 * 用法：node tests/regression-class-timetable.mjs [profile路径]
 * 退出码：0 全绿 / 1 有失败 / 2 档案缺失（跳过）
 */
import fs from 'node:fs';
import {
  isClassIslandProfile, listClassIslandGroups, normalizeClassIslandProfile, TIMETABLE_SCHEMA,
} from '../src/class-timetable.js';

const PROFILE = process.argv[2] || process.env.CI_PROFILE
  || 'D:\\Classlsland\\data\\Profiles\\Default.json';

let pass = 0;
const fails = [];
function check(ok, what, extra = '') {
  if (ok) { pass++; console.log(`  [PASS] ${what}`); }
  else { fails.push(what + (extra ? ` — ${extra}` : '')); console.log(`  [FAIL] ${what}${extra ? ' — ' + extra : ''}`); }
}

if (!fs.existsSync(PROFILE)) {
  console.log(`SKIP：找不到档案 ${PROFILE}（可用参数或 CI_PROFILE 指定）`);
  process.exit(2);
}
const raw = JSON.parse(fs.readFileSync(PROFILE, 'utf8').replace(/^\uFEFF/, ''));

console.log('── ① 档案识别 ──');
check(isClassIslandProfile(raw), '真档案被识别为 ClassIsland profile');
check(!isClassIslandProfile({}), '空对象被拒');
check(!isClassIslandProfile({ ClassPlans: {} }), '缺 TimeLayouts 被拒');

console.log('── ② 课表群枚举 ──');
const groups = listClassIslandGroups(raw);
check(groups.length === 2, `群数=2（实际 ${groups.length}）`, JSON.stringify(groups.map((g) => g.name)));
check(groups.every((g) => g.days === 6), '每个群都覆盖 6 天（周日+周一~周五）');
const g1 = groups.find((g) => g.name.includes('1班'));
const g8 = groups.find((g) => g.name.includes('8班'));
check(!!g1 && !!g8, '群名含「1班课表群」「8班课表群」');

console.log('── ③ 多群未指定必须拒绝（不许猜）──');
const amb = normalizeClassIslandProfile(raw, '');
check(amb.ok === false && Array.isArray(amb.groups) && amb.groups.length === 2,
  '未指定群 → 拒绝并回群清单', amb.error);

console.log('── ④ 归一化 1 班 ──');
const n1 = normalizeClassIslandProfile(raw, g1.name);
check(n1.ok === true, '按群名可导入', n1.ok ? '' : n1.error);
if (n1.ok) {
  const n = n1.normalized;
  check(n.schema === TIMETABLE_SCHEMA, `信封 schema=${TIMETABLE_SCHEMA}`);
  check(n.days.length === 6, `6 天（实际 ${n.days.length}）`);
  const mon = n.days.find((d) => d.dow === 1);
  check(!!mon && mon.lessons.length === 12, `周一 12 节（实际 ${mon ? mon.lessons.length : 'n/a'}）`);
  check(!!mon && mon.lessons.every((l) => l.start && l.end && l.index >= 1),
    '每节课都有 index/start/end');
  check(!!mon && mon.lessons.every((l) => l.subjectId && l.subject !== ''),
    '每节课都翻译出科目名（SubjectId → Subjects[].Name）');
  const sun = n.days.find((d) => d.dow === 0);
  check(!!sun && sun.lessons.length === 3, `周日 3 节（实际 ${sun ? sun.lessons.length : 'n/a'}）`);
  const fri = n.days.find((d) => d.dow === 5);
  check(!!fri && fri.lessons.length === 8, `周五 8 节（实际 ${fri ? fri.lessons.length : 'n/a'}）`);
  // 时间对齐（锁 TimeType 语义：0=上课）：第一版把 1 当上课，数学只剩 20 分钟。
  // 该校 周一 作息 type=0 时段首条是 07:25-08:05，第二条 08:10-08:50。
  check(!!mon && mon.lessons[0].start === '07:25' && mon.lessons[0].end === '08:05',
    `周一第1节 07:25-08:05（实际 ${mon ? mon.lessons[0].start + '-' + mon.lessons[0].end : 'n/a'}）`);
  check(!!mon && mon.lessons[1].start === '08:10' && mon.lessons[1].end === '08:50',
    `周一第2节 08:10-08:50（实际 ${mon ? mon.lessons[1].start + '-' + mon.lessons[1].end : 'n/a'}）`);
  check(!!mon && mon.lessons.every((l) => {
    const [sh, sm] = l.start.split(':').map(Number);
    const [eh, em] = l.end.split(':').map(Number);
    return (eh * 60 + em) - (sh * 60 + sm) >= 30;   // 每节课 ≥30 分钟（课间不会这么长）
  }), '每节课时长 ≥30 分钟（没把课间当上课）');
  check(n.subjects.length > 5, `科目表非空（${n.subjects.length} 项）`);
  const layoutNames = n.layouts.map((l) => l.name).join(' / ');
  check(n.layouts.length >= 2, `带上用到的作息（${n.layouts.length} 份：${layoutNames}）`);
  const anyLayout = n.layouts[0];
  check(anyLayout.items.some((i) => i.kind === 'lesson' && i.label.startsWith('第')),
    '作息条目翻出「第N节」人话');
  check(anyLayout.items.some((i) => i.kind === 'break' || i.kind === 'action'),
    '作息保留课间 / 行动条（不只是上课）');
}

console.log('── ⑤ 两个群必须是不同的课表（防张冠李戴）──');
const n8 = normalizeClassIslandProfile(raw, g8.name);
if (n1.ok && n8.ok) {
  const s1 = n1.normalized.days.find((d) => d.dow === 1).lessons.map((l) => l.subject).join(',');
  const s8 = n8.normalized.days.find((d) => d.dow === 1).lessons.map((l) => l.subject).join(',');
  check(s1 !== s8, '1 班 / 8 班周一课表内容不同', `1班=${s1.slice(0, 30)}…`);
  check(n1.normalized.group.id !== n8.normalized.group.id, '群 id 不同');
}

console.log('── ⑥ 脏输入 ──');
check(normalizeClassIslandProfile({ ClassPlans: {}, TimeLayouts: {} }, '').ok === false,
  '空课表档案 → 报错（不静默存空）');
check(normalizeClassIslandProfile(raw, '不存在的群').ok === false, '不存在的群 → 报错');

console.log(`\n==== ${pass}/${pass + fails.length} ====`);
if (fails.length) { fails.forEach((f) => console.log('  ✗ ' + f)); process.exit(1); }
process.exit(0);
