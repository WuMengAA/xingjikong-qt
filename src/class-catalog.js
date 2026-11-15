// 班级后台「模块拆分 + 流程状态 + 功能进度 + 备份清单」唯一代码真源（2026-11-15）。
// 口径：本文件只放数据与渲染，不参与任何运行时判定；判定真源仍是 perms.js / class-admin-shared.js。
// 三处消费者：docs/开发文档-班级后台-2026-11-15.md（由 scripts/sync-class-doc.py 生成）、
// scripts/backup.mjs（备份集从这里派生）、tests/unit-class-catalog.mjs（与真源码对账）。
// 改了这里必须跑：python scripts/sync-class-doc.py && node tests/unit-class-catalog.mjs

export const CATALOG_VERSION = 1;

/** /api/class/<action> 的 29 个 action，按既有代码里的顺序分组（class-admin.js 正则可逐项复核）。 */
export const ACTION_GROUPS = Object.freeze([
  Object.freeze(['create', 'rename', 'delete', 'unbind', 'bind', 'member-add', 'member-remove', 'members', 'my-classes']),
  Object.freeze(['bindcode', 'bindcode-revoke', 'publish']),
  Object.freeze(['students', 'student-add', 'student-update', 'student-remove', 'students-import']),
  Object.freeze(['timetable', 'timetable-save', 'timetable-import', 'timetable-groups', 'timetable-push', 'timetable-delivery']),
  Object.freeze(['attendance-record', 'attendance-batch', 'attendance-list', 'attendance-range', 'attendance-stats', 'attendance-remove']),
]);
export const CLASS_ACTIONS = Object.freeze(ACTION_GROUPS.flat());

/**
 * 模块清单：source 为 null = 没有已入库代码真源（不拿设计稿/他线 WIP 冒充进度）。
 * 同一份数据渲染两个视图：§1.1 模块拆分表（只列有 source 的）与 §3 功能进度表（全列）。
 */
export const MODULES = Object.freeze([
  Object.freeze({ id: 'class', label: '班级', source: 'src/class-admin-classes.js · src/class-dir.js', duty: '班级身份/目录/接入码/发布', status: '✅', can: '建/改/删/绑定/成员/接入码/发布/snapshot；多租户按 tenantId 隔离', missing: '年级维度只有 grade scope 前缀，无独立年级实体' }),
  Object.freeze({ id: 'student', label: '学生', source: 'src/class-admin-students.js', duty: '名单与课表写入', status: '✅', can: '名单 CRUD、批量导入、≤500 人/班', missing: '照片、学籍号校验规则、软删除' }),
  Object.freeze({ id: 'teacher', label: '教师', source: 'src/class-admin-shared.js', duty: '成员表角色与权限判定', status: '✅', can: 'class_members 的 owner/teacher 角色 + member-add/remove', missing: '独立教师档案、跨班任课排表' }),
  Object.freeze({ id: 'timetable', label: '课表', source: 'src/class-timetable.js · src/class-timetable-push.js', duty: '档案归一化与下发', status: '✅', can: 'ClassIsland 档案归一化(classisland/v1)、保存、导入预览群、下发、送达视图', missing: '双向联动时间表（全校作息）' }),
  Object.freeze({ id: 'attendance', label: '考勤', source: 'src/class-attendance.js · src/class-admin-attendance.js', duty: '四态考勤读写', status: '✅', can: '四态 present/late/leave/absent、单条/批量/区间/统计', missing: '请假审批流、家长签收' }),
  Object.freeze({ id: 'homework', label: '作业', source: null, duty: '—', status: '🔶', can: 'publish 的 homework 类型（全屏文字 + Timeline 留痕）', missing: '收缴、提交、批改、截止状态全无' }),
  Object.freeze({ id: 'notice', label: '通知', source: 'src/class-admin-classes.js', duty: '四类发布', status: '🔶', can: '四类发布 + instructions/notice 查设备回执 + 全屏通知', missing: '通知已读回执（人维度）没有；notify 回执只证明设备收到' }),
  Object.freeze({ id: 'score', label: '成绩', source: null, duty: '—', status: '⬜', can: '—', missing: '表、API、发布流程全无；不是「预留字段」能糊过去的' }),
  Object.freeze({ id: 'parent', label: '家长绑定', source: null, duty: '—', status: '⬜', can: '—', missing: '无家长实体、无绑定码、无可见范围规则' }),
  Object.freeze({ id: 'perm', label: '权限', source: 'src/perms.js · src/class-roles.js', duty: '租户闸与角色预设', status: '✅', can: 'perms.js 租户闸 + scopes；六角色预设 class-roles.js；maskDevice 9 字段脱敏；fail-closed', missing: '独立登录日志表（当前审计在 events.log，静态令牌匿名）' }),
  Object.freeze({ id: 'audit', label: '日志/审计', source: 'src/class-events.js · events.log', duty: '按班事件与运维流水', status: '✅', can: 'events.log 追加 + 10MB 轮转留 3 份；class_events 200 条环形；dev/events 结构化', missing: '登录专用表、异地归档、告警' }),
  Object.freeze({ id: 'backup', label: '备份', source: 'scripts/backup.mjs', duty: '本地快照与保留策略', status: '✅', can: '6 类源（4 库 + 流水 + 报修）由本真源派生；轮转归档；--root 隔离演练', missing: '异地/加密/定时器交给学校自配；删库→恢复演练见 §4.5' }),
  Object.freeze({ id: 'softdelete', label: '软删除', source: null, duty: '—', status: '⬜', can: '—', missing: '全班 class-*.js 与 admin-server schema 无 deleted_at；删班是真删' }),
  Object.freeze({ id: 'noise', label: '噪音按班', source: 'src/class-events.js（端侧 c2a3df7）', duty: '按班语义事件', status: '✅', can: '上课自动开 + 超阈/存证 class-event 按班上报', missing: '真机端到端未跑' }),
  Object.freeze({ id: 'device', label: '设备/指令', source: 'src/store.js · src/registry.js', duty: '队列与在线态', status: '✅', can: '注册、心跳 10s/超时 30s、指令 dispatched→done/failed/expired、重连补发', missing: '60 班并发压测未做' }),
  Object.freeze({ id: 'instance', label: '实例自治', source: 'src/instance.js · src/provision.js', duty: '主权契约与装机地址', status: '✅', can: '不回落我方域名；未配 CLOUD_PUBLIC_BASE 装机包 fail-closed', missing: '独立部署只静态自检，未真机另装' }),
  Object.freeze({ id: 'interact', label: '互动内容', source: 'src/interact.js', duty: '点歌/祝福/弹幕三表', status: '✅', can: '点歌 5 态、祝福 3 态、每小时限额、待审不扫', missing: '审批界面由他线 UI 迁移决定' }),
  Object.freeze({ id: 'classroom', label: '班级一站式单元', source: null, duty: '—', status: '⬜', can: '—', missing: 'src/classroom-*.js 是他线未提交 WIP，不计入已入库能力（§2.4 只作接入约定）' }),
]);

/** 流程与状态词：先有流程，别做一堆 Excel 页面（用户第 5 条）。 */
export const FLOWS = Object.freeze([
  Object.freeze({ id: 'join', label: '接入', line: '签发 join-code → 设备 join → 云端 knownDevices 绑班 → register 回 classCode', states: Object.freeze([]) }),
  Object.freeze({ id: 'apply', label: '班级申请', line: 'pending --approve--> approved --bind--> bound；pending --reject(必填理由)--> rejected', states: Object.freeze(['pending', 'approved', 'rejected', 'bound']) }),
  Object.freeze({ id: 'instruction', label: '指令', line: 'dispatched ──设备回执──> done / failed；TTL 或超上限 ──> expired（不是「没结案」）', states: Object.freeze(['dispatched', 'done', 'failed', 'expired']) }),
  Object.freeze({ id: 'timetable', label: '课表', line: 'timetable-save（落盘）→ timetable-push（下发）→ delivered[] / pending[]（没收到 ≠ 收到失败）', states: Object.freeze(['delivered', 'pending']) }),
  Object.freeze({ id: 'attendance', label: '考勤', line: 'record/batch → list/range/stats → remove；四态之外的值直接拒', states: Object.freeze(['present', 'late', 'leave', 'absent']) }),
  Object.freeze({ id: 'notice', label: '通知', line: 'publish（四类之一）→ 设备 notify 回执 → /api/instructions/notice 查；人维度已读暂无', states: Object.freeze(['student-notice', 'class-notice', 'fullscreen-notice', 'homework']) }),
  Object.freeze({ id: 'noise', label: '噪音', line: '上课 → 连续 3 次超阈 → noise_alert（每周期 1 次）→ 存证 noise_incident（每节次 1 次）', states: Object.freeze(['noise_alert', 'noise_incident']) }),
  Object.freeze({ id: 'song', label: '点歌', line: 'pending → approved / rejected / played / cancelled（只进不退）', states: Object.freeze(['pending', 'approved', 'rejected', 'played', 'cancelled']) }),
  Object.freeze({ id: 'blessing', label: '祝福', line: 'pending → approved / rejected（只进不退）', states: Object.freeze(['pending', 'approved', 'rejected']) }),
  Object.freeze({ id: 'backup', label: '备份恢复', line: 'backup.mjs 取快照 → 停服务覆盖回根目录 → 重启 → 查 /api/instructions/pending', states: Object.freeze([]) }),
]);

/** 备份集：db 类从 config.js 的 *_DB_FILE 派生（含 env 覆盖），非 db 类按默认文件名。 */
export const BACKUP_ITEMS = Object.freeze([
  Object.freeze({ key: 'queue', label: '指令队列', configKey: 'QUEUE_DB_FILE', file: 'instruction-queue.db', kind: 'db' }),
  Object.freeze({ key: 'classRoom', label: '班级数据（学生/课表/考勤/事件）', configKey: 'CLASS_ROOM_DB_FILE', file: 'class-room.db', kind: 'db' }),
  Object.freeze({ key: 'classDir', label: '班级目录', configKey: 'CLASS_DIR_DB_FILE', file: 'class-dir.db', kind: 'db' }),
  Object.freeze({ key: 'interact', label: '互动内容（点歌/祝福/弹幕）', configKey: 'INTERACT_DB_FILE', file: 'interact.db', kind: 'db' }),
  Object.freeze({ key: 'events', label: '审计事件流水', configKey: null, file: 'events.log', kind: 'log', rotate: true }),
  Object.freeze({ key: 'repairs', label: '报修台账', configKey: null, file: 'repairs.json', kind: 'json' }),
]);

/** 状态词契约：单测逐项与真源码对账，防止文档写一套、代码跑另一套。 */
export const STATE_CONTRACTS = Object.freeze({
  instruction: Object.freeze(['dispatched', 'done', 'failed', 'expired']),
  classApply: Object.freeze(['pending', 'approved', 'rejected', 'bound']),
  attendance: Object.freeze(['present', 'late', 'leave', 'absent']),
  publish: Object.freeze(['student-notice', 'class-notice', 'fullscreen-notice', 'homework']),
  song: Object.freeze(['pending', 'approved', 'rejected', 'played', 'cancelled']),
  blessing: Object.freeze(['pending', 'approved', 'rejected']),
  roles: Object.freeze(['superAdmin', 'schoolAdmin', 'classOwner', 'teacher', 'student', 'parent']),
});

function mdTable(head, rows) {
  const bar = '|' + head.map(function () { return '---'; }).join('|') + '|';
  return ['| ' + head.join(' | ') + ' |', bar].concat(rows.map(function (r) { return '| ' + r.join(' | ') + ' |'; })).join('\n');
}

function esc(s) {
  return String(s).replace(/\|/g, '\\|');
}

/** 五个文档块的唯一渲染出口；sync-class-doc.py 与单测都取这里的结果，不各写一份。 */
export function renderCatalogBlocks() {
  const actions = ['```text']
    .concat(ACTION_GROUPS.map(function (g) { return g.join(' / '); }))
    .concat(['```', '', '> 共 ' + CLASS_ACTIONS.length + ' 个 action。本块由 src/class-catalog.js 生成；',
      '> 改 class-admin.js 后先改真源，再跑 python scripts/sync-class-doc.py，单测会逐项对账。'])
    .join('\n');

  const modules = mdTable(['模块', '代码真源', '职责'], MODULES.filter(function (m) { return m.source; })
    .map(function (m) { return [m.label, '`' + esc(m.source) + '`', esc(m.duty)]; }))
    + '\n\n> source 为空的模块（作业 / 成绩 / 家长绑定 / 软删除 / 一站式单元）没有已入库代码真源，'
    + '\n> 不列进本表；进度见 §3。`src/classroom-*.js` 是他线未提交 WIP，不算已入库能力。';

  const progress = mdTable(['模块', '状态', '现在能做什么', '明确缺什么'], MODULES.map(function (m) {
    return [m.label, m.status, esc(m.can), esc(m.missing)];
  })) + '\n\n图例：✅ 有可用 API 且跑过断言 ｜ 🔶 部分（有发布/下发，无完整流程）｜ ⬜ 没有（不虚构）';

  const flows = ['```text'].concat(FLOWS.map(function (f) { return f.label + '：' + f.line; }))
    .concat(['```', '', mdTable(['流程', '状态词'], FLOWS.filter(function (f) { return f.states.length; })
      .map(function (f) { return [f.label, f.states.map(function (s) { return '`' + s + '`'; }).join(' / ')]; }))])
    .join('\n');

  const backup = mdTable(['备份项', '来源 / 默认文件', '类型'], BACKUP_ITEMS.map(function (i) {
    return [i.label, '`' + (i.configKey ? 'config.js:' + i.configKey : i.file) + '` → `' + i.file + '`', i.kind];
  }))
    + '\n\n```bash'
    + '\nnode scripts/backup.mjs            # → backups/<UTC>/（上表 6 类 + events.log.* 轮转）'
    + '\nnode scripts/backup.mjs --list'
    + '\nnode scripts/backup.mjs --keep 14  # 只留最近 14 份'
    + '\npython scripts/sync-class-doc.py --check    # 文档与真源是否漂移'
    + '\npython scripts/backup-restore-drill.py      # 隔离目录里删库→快照→恢复→回读'
    + '\n```'
    + '\n\n恢复四步：停服务 → 把备份目录里的文件覆盖回云端根目录 → 重启 → 查 `/api/instructions/pending`。'
    + '\n文件不存在就跳过（新装的实例可能还没生成某个库），`--root <dir>` 可把整个动作关进隔离目录做演练。'
    + '\n**已知边界**：不做加密、异地/对象存储推送、定时器；`CLOUD_*_DB_FILE` 改了库名/路径时备份会跟着 config 走，'
    + '\n但绝对路径文件在恢复时要人工放回原处。新线 `POST /api/dev/backup` 是 VACUUM INTO 热备份，只覆盖 admin-server 那一个库。';

  return Object.freeze({ actions, modules, progress, flows, backup });
}

export const CATALOG_BLOCK_KEYS = Object.freeze(Object.keys(renderCatalogBlocks()));
