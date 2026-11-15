#!/usr/bin/env node
/**
 * 云端数据备份（2026-11-15 起：备份集由 src/class-catalog.js 单一真源派生）。
 *
 * 备份对象：BACKUP_ITEMS 的 6 类源 —— 4 个 node:sqlite 库（库名走 config.js 的
 * QUEUE_DB_FILE / CLASS_ROOM_DB_FILE / CLASS_DIR_DB_FILE / INTERACT_DB_FILE，允许 env 覆盖）
 * + events.log（含 events.log.* 轮转）+ repairs.json。改清单请改 src/class-catalog.js，
 * 不要在本文件里再写一份 SOURCES；tests/unit-class-catalog.mjs 会逐项对账。
 *
 * 用法：
 *   node scripts/backup.mjs                    # 备份到 backups/<UTC日期时间>/
 *   node scripts/backup.mjs --root <dir>      # 把读写全关进隔离目录（恢复演练用）
 *   node scripts/backup.mjs --keep 14         # 保留最近 14 份，其余删除（默认 30）
 *   node scripts/backup.mjs --list            # 只列出已有备份，不动作
 *
 * 恢复（手工，文档化步骤）：停服务 → 把备份目录里的文件覆盖回云端根目录 → 重启 →
 * 查 /api/instructions/pending。每份快照带 backup-manifest.json（原文件名、来源、字节数），
 * 库文件若用 CLOUD_*_DB_FILE 改过绝对路径，按 manifest 的 source 放回原处。
 *
 * 不做（第一版边界）：加密、异地/对象存储推送、定时器 —— 交给学校自己的运维。
 */
import { mkdirSync, readdirSync, copyFileSync, statSync, rmSync, existsSync, writeFileSync } from 'node:fs';
import { join, dirname, basename, isAbsolute, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { BACKUP_ITEMS } from '../src/class-catalog.js';
import { QUEUE_DB_FILE, CLASS_DIR_DB_FILE, CLASS_ROOM_DB_FILE, INTERACT_DB_FILE } from '../src/config.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DEFAULT_ROOT = join(__dirname, '..');
const args = process.argv.slice(2);
const ri = args.indexOf('--root');
const CLOUD_ROOT = ri >= 0 && args[ri + 1] ? resolve(args[ri + 1]) : DEFAULT_ROOT;
const BACKUP_ROOT = join(CLOUD_ROOT, 'backups');
const CONFIG_FILES = Object.freeze({ QUEUE_DB_FILE, CLASS_DIR_DB_FILE, CLASS_ROOM_DB_FILE, INTERACT_DB_FILE });

function sourcePathOf(item) {
  const rel = item.configKey ? CONFIG_FILES[item.configKey] : item.file;
  return isAbsolute(rel) ? rel : join(CLOUD_ROOT, rel);
}

function destNameOf(item) {
  return basename(sourcePathOf(item));
}

function listBackups() {
  if (!existsSync(BACKUP_ROOT)) { console.log('（还没有备份目录）'); return []; }
  return readdirSync(BACKUP_ROOT)
    .filter((n) => /^\d{8}T\d{6}\d*Z$/.test(n))
    .sort();
}

if (args.includes('--list')) {
  const bs = listBackups();
  if (!bs.length) { console.log('尚无备份'); process.exit(0); }
  console.log('已有备份：');
  for (const b of bs) {
    const dir = join(BACKUP_ROOT, b);
    const total = readdirSync(dir).reduce((s, f) => s + statSync(join(dir, f)).size, 0);
    console.log(`  ${b}  ${(total / 1024 / 1024).toFixed(1)} MB`);
  }
  process.exit(0);
}

let keep = 30;
const ki = args.indexOf('--keep');
if (ki >= 0 && args[ki + 1]) keep = Number(args[ki + 1]) || 30;

mkdirSync(BACKUP_ROOT, { recursive: true });
const stamp = new Date().toISOString().replace(/[-:.]/g, '');
const dest = join(BACKUP_ROOT, stamp);
mkdirSync(dest, { recursive: true });

const manifest = { version: 1, createdAt: new Date().toISOString(), root: CLOUD_ROOT, items: [] };
let copied = 0;
for (const item of BACKUP_ITEMS) {
  const src = sourcePathOf(item);
  const rel = item.configKey ? CONFIG_FILES[item.configKey] : item.file;
  const name = destNameOf(item);
  if (existsSync(src)) {
    copyFileSync(src, join(dest, name));
    copied++;
    manifest.items.push({ key: item.key, label: item.label, file: name, source: rel, kind: item.kind, configKey: item.configKey, bytes: statSync(src).size });
    console.log(`  ✓ ${name} → backups/${stamp}/${name}`);
  }
}

// events.log 的轮转归档也一起带（存在的话）
const rotated = readdirSync(CLOUD_ROOT).filter((n) => n.startsWith('events.log.'));
for (const n of rotated) {
  const from = join(CLOUD_ROOT, n);
  if (!statSync(from).isFile()) continue;
  copyFileSync(from, join(dest, n));
  copied++;
  manifest.items.push({ key: 'eventsRotate', label: '审计事件轮转归档', file: n, source: n, kind: 'log-rotate', configKey: null, bytes: statSync(from).size });
  console.log(`  ✓ ${n} → backups/${stamp}/${n}`);
}

if (!copied) {
  console.log('没有可备份的文件（云端还没写过数据？）');
  rmSync(dest, { recursive: true, force: true });
  process.exit(1);
}

writeFileSync(join(dest, 'backup-manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
console.log(`  ✓ backup-manifest.json（${manifest.items.length} 条）`);

// 保留策略：删最旧的超出 keep 份
const bs = listBackups();
const excess = bs.slice(0, Math.max(0, bs.length - keep));
for (const b of excess) {
  rmSync(join(BACKUP_ROOT, b), { recursive: true, force: true });
  console.log(`  ✗ 清理过期备份 ${b}`);
}

const sum = readdirSync(dest).reduce((s, f) => s + statSync(join(dest, f)).size, 0);
console.log(`备份完成 → backups/${stamp}/ （${(sum / 1024).toFixed(1)} KB，${manifest.items.length} 项，保留最近 ${keep} 份）`);
