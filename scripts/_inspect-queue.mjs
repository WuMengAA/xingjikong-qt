// 只读取证：把指令队列表里"已下发未回执"的行摊开看，专门盯关机/重启/升级这类
// 「执行即终止进程」的动作 —— 它们回执物理上收不到，一旦还躺在 dispatched 状态，
// 每次设备重连都会被补发一次，就是"关机指令一直发、机器一直重启"的元凶。
//
// 只读：只 SELECT，一个字节都不改。要看清 Cloud 端现在是不是正压着这种雷。

import { DatabaseSync } from 'node:sqlite';
import fs from 'node:fs';

const DB = process.env.CLOUD_QUEUE_DB_FILE || 'instruction-queue.db';
if (!fs.existsSync(DB)) { console.log(`[miss] ${DB} 不存在（云端可能换了目录或还没落过库）`); process.exit(0); }

const db = new DatabaseSync(DB, { readOnly: true });
const q = (sql, ...a) => { try { return db.prepare(sql).all(...a); } catch (e) { return [`ERR ${e.message}`]; } };

console.log(`=== ${DB}（只读） ===\n`);

// 表结构：确认有没有 no_resend 列（加列之前的库还没这列）
const cols = q("PRAGMA table_info(instructions)").map((c) => c.name);
console.log('列：', cols.join(', '), cols.includes('no_resend') ? '' : '\n  ⚠️ 没有 no_resend 列 ⇒ 这是加该列之前的旧库，历史关机类指令不会被自动排除');

const now = Date.now();
const TERM = ['shutdown', 'reboot', 'schedule_shutdown', 'schedule_reboot', 'self_update'];
for (const st of ['dispatched']) {
  // 旧库没有 no_resend 列：带上它 SQL 会直接报错，所以按列清单拼——拼错了等于什么都没查到
  const hasFlag = cols.includes('no_resend');
  const rows = q(
    `SELECT id, uid, action, params, dispatched_at${hasFlag ? ', no_resend' : ''} FROM instructions
      WHERE state = ? ORDER BY uid, id ASC`, st);
  console.log(`\n--- state=${st} 共 ${rows.length} 条 ---`);
  for (const r of rows) {
    const ageH = ((now - r.dispatched_at) / 3600000).toFixed(1);
    const isTerm = TERM.includes(r.action);
    const flag = isTerm ? (r.no_resend === 1 ? '  ✅已禁补发' : '  ⛔雷：未回执的关机类，重连必补发') : '';
    console.log(`  #${r.id} ${r.uid} ${r.action} (${ageH}h前)${flag}`);
    console.log(`       params=${String(r.params).slice(0, 90)}`);
  }
}

// 汇总：每台设备压了多少条
const byUid = q(`SELECT uid, COUNT(*) n FROM instructions WHERE state='dispatched' GROUP BY uid ORDER BY n DESC`);
console.log('\n--- 按设备汇总（未回执） ---');
for (const r of byUid) console.log(`  ${r.uid}: ${r.n} 条`);

db.close();
