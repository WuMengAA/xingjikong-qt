/**
 * 班级课表 · ClassIsland 档案规范化（2026-10-25）
 *
 * 背景：CIMS 时期课表是「ClassIsland 官方档案 + 资源集」那一套（cp_files / tl_files /
 * sub_files）。CIMS 退役后，课表要落回我们自己的 class_timetable（class-data.js），
 * 但**不能自成一套格式** —— 老师手里的档案、教室里的 ClassIsland 客户端、
 * 以后的平板端都要能互通，所以：
 *   · 入：ClassIsland 官方 Profile（ClassPlans / TimeLayouts / Subjects / ClassPlanGroups）
 *   · 出：本项目归一化信封（schema=classisland/v1），保留 SubjectId 等原始标识
 *
 * 归一化只做三件事：挑一个课表群、把「第 N 节课」和作息里的「第 N 个上课时段」对齐、
 * 把科目/时间翻译成人话。**不做语义增删** —— 课表内容是老师的数据，服务端不解释。
 *
 * 数据事实（2026-10-25 用 D:\Classlsland\data\Profiles\Default.json 核对）：
 *   · 12 个 ClassPlan = 2 个课表群 × 6 天（周日 + 周一~周五），群名「1班课表群」「8班课表群」
 *   · 4 份 TimeLayout（周日 / 周1234 / 周5 / 周1 各一套「高二通用」）
 *   · 23 个科目
 *
 * TimeType 语义（**踩过坑，别再猜**）：`0 = 上课`、`1 = 课间`、`3 = 行动`。
 *   第一版按「1=上课」写，导入后数学只剩 20 分钟、语文 5 分钟 —— 因为 1 都是
 *   早操/早点/预备这类短时段。权威依据：CIMS 导入器
 *   `app/services/schedule_importer.py::_layout_class_count` 就是 `TimeType == 0` 计数，
 *   且该校真实档案（1班课表群）12 节课正好对上 12 个 type=0 时段。
 *   只有 type=0 占「第 N 节」，其余条目仍保留在作息里给人看。
 */

export const TIMETABLE_SCHEMA = 'classisland/v1';
/** 单份课表上限与 class-data.js 的 TIMETABLE_MAX_BYTES 同源，这里只做归一化前的粗挡。 */
const MAX_DAYS = 7;
const MAX_LESSONS = 30;

const DOW = { 周日: 0, 周天: 0, 周一: 1, 周二: 2, 周三: 3, 周四: 4, 周五: 5, 周六: 6 };
const DOW_NAME = ['周日', '周一', '周二', '周三', '周四', '周五', '周六'];

/** "18:00:00" → "18:00"（ClassIsland 用 HH:mm:ss，界面只需要分钟）。 */
function hhmm(t) {
  const s = String(t ?? '').trim();
  if (!s) return '';
  const m = /^(\d{1,2}):(\d{2})/.exec(s);
  return m ? `${m[1].padStart(2, '0')}:${m[2]}` : '';
}

/** 字符串收敛：去空白 + 截断（防脏档案把 UI 撑爆）。 */
function clip(v, n = 40) {
  return String(v ?? '').trim().slice(0, n);
}

/** 判断这份 JSON 是不是 ClassIsland 档案（供 UI 早失败，不必等云端报错）。 */
export function isClassIslandProfile(obj) {
  return !!obj && typeof obj === 'object'
    && !!obj.ClassPlans && typeof obj.ClassPlans === 'object'
    && !!obj.TimeLayouts && typeof obj.TimeLayouts === 'object';
}

/**
 * 列出档案里「有课表」的课表群（供 UI 让老师选：一个档案常含多班）。
 * @returns {Array<{id:string,name:string,days:number,isGlobal:boolean}>}
 */
export function listClassIslandGroups(profile) {
  if (!isClassIslandProfile(profile)) return [];
  const meta = profile.ClassPlanGroups && typeof profile.ClassPlanGroups === 'object'
    ? profile.ClassPlanGroups : {};
  const tally = new Map();       // gid -> Set(dow)
  for (const plan of Object.values(profile.ClassPlans)) {
    if (!plan || typeof plan !== 'object') continue;
    const gid = String(plan.AssociatedGroup ?? '');
    if (!tally.has(gid)) tally.set(gid, new Set());
    const dow = DOW[clip(plan.Name, 8)];
    if (dow !== undefined) tally.get(gid).add(dow);
  }
  const out = [];
  for (const [gid, days] of tally) {
    if (days.size === 0) continue;                 // 全局群（无日计划）不算候选
    const m = meta[gid] || {};
    out.push({
      id: gid,
      name: clip(m.Name, 30) || (gid ? `课表群 ${gid.slice(0, 8)}` : '默认课表群'),
      days: days.size,
      isGlobal: m.IsGlobal === true,
    });
  }
  return out.sort((a, b) => b.days - a.days || a.name.localeCompare(b.name, 'zh'));
}

/** 作息条目类型 → 人话（TimeType 见文件头：0=上课 1=课间 3=行动）。 */
function layoutKind(type) {
  if (type === 0) return 'lesson';
  if (type === 1) return 'break';
  if (type === 3) return 'action';
  return 'other';
}

/**
 * 归一化一份 ClassIsland 档案的某个课表群。
 * @param {object} profile 原始档案
 * @param {string} groupKey 群 id（可只给前 8 位）或群名；空 = 只有一个候选群时自动选
 * @returns {{ok:true, normalized:object}|{ok:false, error:string, groups?:Array}}
 */
export function normalizeClassIslandProfile(profile, groupKey = '') {
  if (!isClassIslandProfile(profile)) {
    return { ok: false, error: '不是 ClassIsland 档案（缺 ClassPlans / TimeLayouts）' };
  }
  const groups = listClassIslandGroups(profile);
  if (groups.length === 0) return { ok: false, error: '档案里没有任何日课表（ClassPlan 为空）' };

  const key = clip(groupKey, 64).toLowerCase();
  let picked = null;
  if (key) {
    picked = groups.find((g) => g.id.toLowerCase() === key)
      || groups.find((g) => g.id.toLowerCase().startsWith(key))
      || groups.find((g) => g.name.toLowerCase() === key)
      || null;
    if (!picked) return { ok: false, error: `档案里没有这个课表群：${groupKey}`, groups };
  } else if (groups.length === 1) {
    picked = groups[0];
  } else {
    // 多群且没指定：拒绝猜（猜错=把 8 班的课表挂到 1 班，属于数据事故）
    return { ok: false, error: '档案含多个课表群，请指定要导入哪一个', groups };
  }

  const subsMeta = profile.Subjects && typeof profile.Subjects === 'object' ? profile.Subjects : {};
  const tlsMeta = profile.TimeLayouts && typeof profile.TimeLayouts === 'object' ? profile.TimeLayouts : {};

  const days = [];
  const usedLayouts = new Map();
  for (const plan of Object.values(profile.ClassPlans)) {
    if (!plan || typeof plan !== 'object') continue;
    if (String(plan.AssociatedGroup ?? '') !== picked.id) continue;
    const dow = DOW[clip(plan.Name, 8)];
    if (dow === undefined) continue;
    const tlId = String(plan.TimeLayoutId ?? '');
    const tl = tlsMeta[tlId] || {};
    const items = Array.isArray(tl.Layouts) ? tl.Layouts : [];
    // 只有 TimeType=0 占「第 N 节」；课间/行动不进 lessons（但要保留在作息里给人看）
    const slots = items.filter((x) => x && x.TimeType === 0)
      .map((x) => ({ start: hhmm(x.StartTime), end: hhmm(x.EndTime) }))
      .slice(0, MAX_LESSONS);
    usedLayouts.set(tlId, { id: tlId, name: clip(tl.Name, 30), items });

    const raw = Array.isArray(plan.Classes) ? plan.Classes : [];
    const lessons = [];
    for (let i = 0; i < Math.min(raw.length, slots.length); ++i) {
      const cell = raw[i] || {};
      const sid = String(cell.SubjectId ?? '');
      const sm = subsMeta[sid] || {};
      lessons.push({
        index: i + 1,
        subjectId: sid,
        subject: clip(sm.Name, 20),
        initial: clip(sm.Initial, 4),
        teacher: clip(sm.TeacherName, 20),
        start: slots[i].start,
        end: slots[i].end,
      });
    }
    days.push({ dow, name: DOW_NAME[dow], timeLayoutId: tlId, lessons });
  }
  if (days.length === 0) return { ok: false, error: '这个课表群里没有可用的日课表' };
  days.sort((a, b) => a.dow - b.dow);
  if (days.length > MAX_DAYS) days.length = MAX_DAYS;

  // 作息：只带上本群用到的，按"课间也保留"翻译成人话列表
  const layouts = [];
  for (const [id, tl] of usedLayouts) {
    const items = [];
    let lessonNo = 0;
    let breakNo = 0;
    for (const x of tl.items) {
      const kind = layoutKind(x.TimeType);
      let label = '';
      if (kind === 'lesson') label = `第${++lessonNo}节`;
      else if (kind === 'break') label = `课间${++breakNo}`;
      else if (kind === 'action') label = '行动';
      items.push({
        kind,
        label,
        start: hhmm(x.StartTime),
        end: hhmm(x.EndTime),
        name: clip(x.BreakName, 20),
      });
    }
    layouts.push({ id, name: tl.name, items });
  }

  const subjects = [];
  const seen = new Set();
  for (const d of days) {
    for (const l of d.lessons) {
      if (!l.subjectId || seen.has(l.subjectId)) continue;
      seen.add(l.subjectId);
      subjects.push({ id: l.subjectId, name: l.subject, initial: l.initial, teacher: l.teacher });
    }
  }

  return {
    ok: true,
    normalized: {
      schema: TIMETABLE_SCHEMA,
      source: 'classisland',
      importedAt: new Date().toISOString(),
      group: { id: picked.id, name: picked.name },
      days,
      layouts,
      subjects,
    },
  };
}
