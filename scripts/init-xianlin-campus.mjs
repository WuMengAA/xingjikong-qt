// 咸林中学校区初始化（2026-11-16 · 用户 m33515）。
// 隔离运行：所有 DB / 租户文件落在 --work-dir 指定目录；不碰生产 8788、不重启服务。
// 12 班课表来自 ClassIsland 全校档案：24 群 / 72 plans；按 ClassPlans 首次出现序拆 1..12 班。
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

const DEFAULT_PROFILE = 'D:/Stelarith/_backup/classisland-2026-10-06/instance2/Profiles/Default.json';
function arg(name, fallback = '') {
  const i = process.argv.indexOf(name);
  return i >= 0 ? String(process.argv[i + 1] || '') : fallback;
}
const workDir = arg('--work-dir') || mkdtempSync(path.join(tmpdir(), 'xianlin-init-'));
const profilePath = arg('--profile', DEFAULT_PROFILE);
mkdirSync(workDir, { recursive: true });
process.env.CLOUD_TENANTS_FILE = path.join(workDir, 'tenants.json');
process.env.CLOUD_CLASS_DIR_DB_FILE = path.join(workDir, 'class-dir.db');
process.env.CLOUD_CLASS_ROOM_DB_FILE = path.join(workDir, 'class-room.db');
process.env.NODE_TEST_CONTEXT = 'child-v8';

const S = (n) => import(pathToFileURL(path.join(process.cwd(), 'src', n)).href);
const tenant = await S('tenant.js');
const classDir = await S('class-dir.js');
const classRoom = await S('class-room.js');
const classData = await S('class-data.js');
const timetable = await S('class-timetable.js');
const deviceTimetable = await S('device-timetable.js');
const campus = await S('campus-registry.js');

const profile = JSON.parse(readFileSync(profilePath, 'utf8').replace(/^\uFEFF/, ''));
const meta = timetable.classIslandGroupMeta(profile);
if (meta.groups.length !== 12) throw new Error(`档案不是 12 班全校课表：groups=${meta.groups.length}`);
if (!campus.XIANLIN_CAMPUS || campus.XIANLIN_CAMPUS.classroomCount !== 12) throw new Error('校区班级数不是 12');

tenant.initTenants(process.env.CLOUD_TENANTS_FILE);
const school = tenant.registerTenant({ name: campus.XIANLIN_CAMPUS.tenantName });
if (!school.ok) throw new Error('校区租户注册失败：' + school.error);
const tn = tenant.normalizeTenantId(school.tenant.id);
classDir.initClassDir(process.env.CLOUD_CLASS_DIR_DB_FILE);
classRoom.initClassRoom(process.env.CLOUD_CLASS_ROOM_DB_FILE);
classData.initClassData();

const rows = [];
for (let i = 1; i <= 12; i++) {
  const name = `${i}班`;
  const r = classDir.resolveClassCode({ name, source: 'campus-init', tenantId: tn });
  if (!r.ok) throw new Error(`班级注册失败 ${name}: ${r.error}`);
  const norm = timetable.normalizeClassIslandProfile(profile, String(i));
  if (!norm.ok) throw new Error(`课表归一化失败 ${name}: ${norm.error}`);
  const saved = classData.saveTimetable(r.code, norm.normalized, 'xianlin-init', tn);
  if (!saved.ok) throw new Error(`课表落库失败 ${name}: ${saved.error}`);
  rows.push({ index: i, name, classCode: r.code, group: norm.normalized.group, days: norm.normalized.days.length, bytes: saved.bytes });
}

// 模拟 12 台设备各自按 uid 拉自己班课表：注入依赖，不碰 registry / 在线表。
const devices = rows.map((r) => ({ uid: `xianlin-${String(r.index).padStart(2, '0')}`, classCode: r.classCode, tenantId: tn }));
const pulls = devices.map((d) => {
  const res = deviceTimetable.deviceTimetable({
    uid: d.uid,
    classCodeOf: (u) => devices.find((x) => x.uid === u)?.classCode || '',
    tenantOf: () => tn,
    read: (code, tenantId) => classData.getTimetable(code, tenantId),
  });
  return { uid: d.uid, classCode: d.classCode, status: res.status, ok: res.body.ok,
           reason: res.body.reason || '', days: Array.isArray(res.body.profile?.days) ? res.body.profile.days.length : 0 };
});
const good = pulls.filter((p) => p.status === 200 && p.ok && p.days > 0 && !p.reason).length;
const report = {
  ok: good === 12,
  campus: campus.XIANLIN_CAMPUS.name,
  tenantId: tn,
  workDir,
  profile: { path: profilePath, groups: meta.groups.length, plans: Object.keys(profile.ClassPlans || {}).length, nameSource: meta.nameSource },
  classes: rows,
  pulls,
  verified: { classCount: rows.length, timetableCount: rows.filter((r) => r.days > 0).length, devicePullable: good },
};
writeFileSync(path.join(workDir, 'xianlin-init-report.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify({ ok: report.ok, campus: report.campus, tenantId: tn, workDir,
  groups: report.profile.groups, plans: report.profile.plans, classes: rows.length, devicePullable: good }, null, 2));
if (!report.ok) process.exit(1);
