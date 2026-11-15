// 班级后台代码真源对账：文档 / 备份脚本 / 真源码三者不许各写一套。
// 运行：node tests/unit-class-catalog.mjs（由 package.json test:unit 串起）
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import {
  CATALOG_VERSION, ACTION_GROUPS, CLASS_ACTIONS, MODULES, FLOWS, BACKUP_ITEMS,
  STATE_CONTRACTS, renderCatalogBlocks, CATALOG_BLOCK_KEYS,
} from '../src/class-catalog.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..');
const ADMIN = join(ROOT, '..', 'Stelarith-admin-server');
let pass = 0, fail = 0;
function check(name, fn) {
  try { fn(); pass++; } catch (e) { fail++; console.error('FAIL ' + name + ' :: ' + (e && e.message ? e.message : e)); }
}
function read(p) { return readFileSync(p, 'utf8'); }
function quoted(text, re) {
  const m = text.match(re);
  if (!m) return [];
  return Array.from(m[1].matchAll(/'([^']+)'/g), (x) => x[1]);
}

const admin = read(join(ROOT, 'src', 'class-admin.js'));
const actions = Array.from(admin.matchAll(/action === '([a-z0-9-]+)'/g), (m) => m[1]);
check('class-admin.js 抽出 29 个 action', () => assert.equal(actions.length, 29));
check('CLASS_ACTIONS 与真源码逐项一致', () => assert.deepEqual([...CLASS_ACTIONS], actions));
check('ACTION_GROUPS.flat() 等于 CLASS_ACTIONS', () => assert.deepEqual(ACTION_GROUPS.flat(), [...CLASS_ACTIONS]));
check('ACTION_GROUPS 是 5 组', () => assert.equal(ACTION_GROUPS.length, 5));

const classes = read(join(ROOT, 'src', 'class-admin-classes.js'));
const publish = quoted(classes, /const PUBLISH_TYPES = \[([^\]]+)\]/);
check('PUBLISH_TYPES 对账', () => assert.deepEqual(publish, [...STATE_CONTRACTS.publish]));

const attendance = read(join(ROOT, 'src', 'class-attendance.js'));
const att = quoted(attendance, /export const ATTENDANCE_STATUSES = \[([^\]]+)\]/);
check('ATTENDANCE_STATUSES 对账', () => assert.deepEqual(att, [...STATE_CONTRACTS.attendance]));

const applyState = read(join(ADMIN, 'src', 'class-apply', 'state.js'));
const apply = quoted(applyState, /export const STATES = Object\.freeze\(\[([^\]]+)\]\);/);
check('班级申请 4 态对账', () => assert.deepEqual(apply, [...STATE_CONTRACTS.classApply]));

const interact = read(join(ROOT, 'src', 'interact.js'));
const song = quoted(interact, /const SONG_STATES = new Set\(\[([^\]]+)\]\);/);
const blessing = quoted(interact, /const BLESSING_STATES = new Set\(\[([^\]]+)\]\);/);
check('点歌 5 态对账', () => assert.deepEqual(song, [...STATE_CONTRACTS.song]));
check('祝福 3 态对账', () => assert.deepEqual(blessing, [...STATE_CONTRACTS.blessing]));

const roles = read(join(ROOT, 'src', 'class-roles.js'));
const roleKeys = quoted(roles, /export const ROLE_KEYS = Object\.freeze\(\[([^\]]+)\]\);/);
check('六角色对账', () => assert.deepEqual(roleKeys, [...STATE_CONTRACTS.roles]));

const store = read(join(ROOT, 'src', 'store.js'));
for (const s of STATE_CONTRACTS.instruction) {
  check('store.js 含指令态 ' + s, () => assert.ok(store.includes(s)));
}
check('store.js dispatched SQL', () => assert.ok(store.includes("state = 'dispatched'")));
check('store.js expired SQL', () => assert.ok(store.includes("state = 'expired'")));
check('store.js done/failed 回执', () => assert.ok(store.includes("result === 'done' ? 'done' : 'failed'")));
check('store.js 非 WAL', () => assert.ok(store.includes('PRAGMA journal_mode = DELETE')));

const cfg = read(join(ROOT, 'src', 'config.js'));
const dbDefaults = {
  QUEUE_DB_FILE: 'instruction-queue.db',
  CLASS_DIR_DB_FILE: 'class-dir.db',
  CLASS_ROOM_DB_FILE: 'class-room.db',
  INTERACT_DB_FILE: 'interact.db',
};
for (const [key, file] of Object.entries(dbDefaults)) {
  check('config.js 声明 ' + key, () => assert.ok(cfg.includes('export const ' + key + ' = ')));
  check('config.js 默认值 ' + file, () => assert.ok(cfg.includes("'" + file + "'")));
}
const dbItems = BACKUP_ITEMS.filter((i) => i.kind === 'db');
check('BACKUP_ITEMS 恰好 4 个库', () => assert.equal(dbItems.length, 4));
for (const i of dbItems) {
  check('备份项 ' + i.key + ' 指向 config 常量', () => assert.equal(dbDefaults[i.configKey], i.file));
}
check('BACKUP_ITEMS 共 6 项', () => assert.equal(BACKUP_ITEMS.length, 6));
check('events 轮转标记', () => assert.equal(BACKUP_ITEMS.find((i) => i.key === 'events').rotate, true));

const backup = read(join(ROOT, 'scripts', 'backup.mjs'));
check('backup.mjs 从真源取 BACKUP_ITEMS', () => assert.ok(backup.includes("import { BACKUP_ITEMS } from '../src/class-catalog.js';")));
check('backup.mjs 从 config 取 4 个库名', () => assert.ok(backup.includes('QUEUE_DB_FILE, CLASS_DIR_DB_FILE, CLASS_ROOM_DB_FILE, INTERACT_DB_FILE')));
check('backup.mjs 没有第二份 SOURCES', () => assert.ok(!backup.includes('const SOURCES')));
check('backup.mjs 遍历 BACKUP_ITEMS', () => assert.ok(backup.includes('for (const item of BACKUP_ITEMS)')));
check('backup.mjs 写 manifest', () => assert.ok(backup.includes('backup-manifest.json')));
check('backup.mjs 支持 --root', () => assert.ok(backup.includes("args.indexOf('--root')")));

const bridge = join(ROOT, 'scripts', 'class-catalog-blocks.mjs');
check('渲染桥存在', () => assert.ok(existsSync(bridge)));
const rendered = JSON.parse(execFileSync('node', [bridge], { encoding: 'utf8' }));
check('渲染桥版本', () => assert.equal(rendered.version, CATALOG_VERSION));
const blocks = renderCatalogBlocks();
check('渲染桥块内容一致', () => assert.deepEqual(rendered.blocks, blocks));
check('CATALOG_BLOCK_KEYS 五块', () => assert.deepEqual([...CATALOG_BLOCK_KEYS].sort(), Object.keys(blocks).sort()));
check('MODULES 18 项', () => assert.equal(MODULES.length, 18));
check('FLOWS 10 条', () => assert.equal(FLOWS.length, 10));
check('classroom 不计入已入库', () => assert.equal(MODULES.find((m) => m.id === 'classroom').source, null));
check('classroom 缺项写明 WIP', () => assert.ok(MODULES.find((m) => m.id === 'classroom').missing.includes('WIP')));

const docPath = join(ROOT, 'docs', '开发文档-班级后台-2026-11-15.md');
const doc = read(docPath);
const found = {};
const re = /<!-- CATALOG:begin:([a-z]+) -->\n([\s\S]*?)\n<!-- CATALOG:end:\1 -->/g;
let m;
while ((m = re.exec(doc)) !== null) found[m[1]] = m[2];
check('文档 5 个 CATALOG 标记齐全', () => assert.deepEqual(Object.keys(found).sort(), [...CATALOG_BLOCK_KEYS].sort()));
for (const k of CATALOG_BLOCK_KEYS) {
  check('文档块 ' + k + ' 与真源逐字一致', () => assert.equal(found[k], blocks[k]));
}
check('文档不再写 28 个 action', () => assert.ok(!doc.includes('28 个 action')));
check('文档不再写备份缺两库', () => assert.ok(!doc.includes('不含 `class-room.db`')));
check('文档标注 classroom 为 WIP', () => assert.ok(doc.includes('他线 WIP，未入库')));

const readme = read(join(ROOT, 'README.md'));
check('README 不再写 28 个 action', () => assert.ok(!readme.includes('28 个 action')));
check('README 不再写备份缺两库', () => assert.ok(!readme.includes('仍不含 `class-room.db`')));
check('README 有代码真源归档', () => assert.ok(readme.includes('### 17.6')));

const pkg = JSON.parse(read(join(ROOT, 'package.json')));
check('test:unit 挂上 catalog 单测', () => assert.ok(pkg.scripts['test:unit'].includes('unit-class-catalog.mjs')));
check('sync-class-doc.py 存在', () => assert.ok(existsSync(join(ROOT, 'scripts', 'sync-class-doc.py'))));
check('backup-restore-drill.py 存在', () => assert.ok(existsSync(join(ROOT, 'scripts', 'backup-restore-drill.py'))));

console.log('PASS ' + pass + ' / FAIL ' + fail);
if (fail) process.exit(1);
