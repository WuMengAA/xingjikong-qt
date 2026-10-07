#!/usr/bin/env node
/**
 * 云端数据备份（《全量设计文档》10.3「数据备份与恢复」落地，2026-10-07）
 *
 * 备份对象（云端唯一需要保的数据，丢了会丢指令队列与审计流水）：
 *   - instruction-queue.db  —— 已下发未回执的指令队列（SQLite）
 *   - events.log / events.log.* —— 审计事件流水（追加式，含轮转归档）
 *
 * 用法：
 *   node scripts/backup.mjs              # 备份到 backups/<UTC日期时间>/
 *   node scripts/backup.mjs --keep 14    # 保留最近 14 份，其余删除（默认 30）
 *   node scripts/backup.mjs --list       # 只列出已有备份，不动作
 *
 * 恢复（手工，文档化步骤）：
 *   1) 停云端；2) 把 backups/<时间>/instruction-queue.db 覆盖回 cloud-ws 根目录；
 *   3) events.log* 同理；4) 重启云端。指令队列与审计即恢复。
 *
 * 不做（第一版边界）：加密（备份是本机目录，靠目录权限保护）、
 * 异地/对象存储推送（需要凭据，学校自配）、定时器（交给计划任务/看门狗）。
 */
import { mkdirSync, readdirSync, copyFileSync, statSync, rmSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const CLOUD_ROOT = join(__dirname, '..');
const BACKUP_ROOT = join(CLOUD_ROOT, 'backups');
const SOURCES = ['instruction-queue.db', 'events.log', 'repairs.json'];

const args = process.argv.slice(2);

function listBackups() {
  if (!existsSync(BACKUP_ROOT)) { console.log('（还没有备份目录）'); return []; }
  // 目录名 = 时间戳（YYYYMMDDTHHMMSS[毫秒]Z，兼容两种）
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

// 解析保留份数
let keep = 30;
const ki = args.indexOf('--keep');
if (ki >= 0 && args[ki + 1]) keep = Number(args[ki + 1]) || 30;

mkdirSync(BACKUP_ROOT, { recursive: true });
// 时间戳：YYYYMMDDTHHMMSSZ（去掉 ISO 里的冒号/点/横杠，保留 T 与 Z）
const stamp = new Date().toISOString().replace(/[-:.]/g, '').replace('T', 'T');
const dest = join(BACKUP_ROOT, stamp);
mkdirSync(dest, { recursive: true });

let copied = 0;
for (const name of SOURCES) {
  const src = join(CLOUD_ROOT, name);
  if (existsSync(src)) {
    copyFileSync(src, join(dest, name));
    copied++;
    console.log(`  ✓ ${name} → backups/${stamp}/${name}`);
  }
}
// events.log 的轮转归档也一起带（存在的话）
const ev = readdirSync(CLOUD_ROOT).filter((n) => n.startsWith('events.log.'));
for (const n of ev) {
  copyFileSync(join(CLOUD_ROOT, n), join(dest, n));
  copied++;
  console.log(`  ✓ ${n} → backups/${stamp}/${n}`);
}

if (!copied) {
  console.log('没有可备份的文件（云端还没写过数据？）');
  rmSync(dest, { recursive: true, force: true });
  process.exit(1);
}

// 保留策略：删最旧的超出 keep 份
const bs = listBackups();
const excess = bs.slice(0, Math.max(0, bs.length - keep));
for (const b of excess) {
  rmSync(join(BACKUP_ROOT, b), { recursive: true, force: true });
  console.log(`  ✗ 清理过期备份 ${b}`);
}

const sum = readdirSync(dest).reduce((s, f) => s + statSync(join(dest, f)).size, 0);
console.log(`备份完成 → backups/${stamp}/ （${(sum / 1024).toFixed(1)} KB，保留最近 ${keep} 份）`);
