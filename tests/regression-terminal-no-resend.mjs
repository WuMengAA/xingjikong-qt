// 回归：关机/重启类指令「执行即终止进程」⇒ 回执永远收不到 ⇒ 绝不能进补发队列。
//
// 2026-10-07 恶性BUG：这些指令原本照常进 pending，设备每次重连都被补发一遍 ——
//   shutdown → 被控端进程死 → 云端判未回执 → 重连补发 shutdown → 又关 → 又死
//   现象：老师点一次关机，机器反复重启、被控软件一连上就立刻重启。
//
// 验收口径：
//   ① 新下发的 shutdown/reboot/schedule_*/self_update 一律 pendingFor 查不到；
//   ② 普通指令（ping 之类）补发行为不受影响；
//   ③ 历史遗留的关机类未回执指令，expireTerminalHistory() 能一次清干净（否则线上止血失败）。

import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const DB = path.join(os.tmpdir(), `test-noresend-${process.pid}.db`);
process.env.CLOUD_QUEUE_DB_FILE = DB;
for (const k of fs.readdirSync(os.tmpdir())) {
  if (k.startsWith(path.basename(DB).replace('.db', ''))) {
    try { fs.rmSync(path.join(os.tmpdir(), k), { force: true }); } catch { /* 别人占着就跳过 */ }
  }
}

const st = await import('../src/store.js');
st.initStore();

let pass = 0;
const check = (name, cond, extra = '') => {
  if (cond) { pass++; console.log(`  ok   ${name}`); }
  else { console.log(`  FAIL ${name} ${extra}`); process.exitCode = 1; }
};

// ── ① 新下发的关机类指令：落库了，但不进补发查询 ────────────────────────────
const TERMINALS = ['shutdown', 'reboot', 'schedule_shutdown', 'schedule_reboot', 'self_update'];
for (const a of TERMINALS) {
  const id = st.enqueueDispatch('UID-LOOP', a, {});
  assert.ok(id, `落盘失败 ${a}`);
}
check('5 条关机/重启/升级类指令均已落库', st.pendingFor('UID-LOOP').length === 0,
  `pendingFor=${JSON.stringify(st.pendingFor('UID-LOOP'))}`);
check('listPendingAll 同样过滤掉关机类', Object.keys(st.listPendingAll()).length === 0,
  JSON.stringify(st.listPendingAll()));

// ── ② 普通指令的补发行为不受影响 ───────────────────────────────────────────
const pingId = st.enqueueDispatch('UID-LOOP', 'ping', {});
const pend = st.pendingFor('UID-LOOP');
check('普通指令 ping 仍可补发', pend.length === 1 && pend[0].action === 'ping', JSON.stringify(pend));
check('补发保持了原 id（回执按 id 配对）', pend[0]?.id === pingId, `pend[0].id=${pend[0]?.id} pingId=${pingId}`);

// 回执照样能销账（被控端去重后回一个 skipped 也要能 settle）
check('回执能正常 settle', st.settle(pingId, 'done') === true);
check('settle 后可回执的记录不再补发', st.pendingFor('UID-LOOP').length === 0);

// ── ③ 历史遗留止血 ─────────────────────────────────────────────────────────
// 模拟：加 no_resend 之前就压在库里的关机类未回执指令 —— 显式 noResend=0 造出旧行
const LEGACY = ['shutdown', 'reboot'];
const legacyIds = LEGACY.map((a) => st.enqueueDispatch('UID-OLD', a, {}, 0));
const rawAfterLegacy = st.pendingFor('UID-OLD');
check('（前提）历史遗留关机指令会被普通查询捞到', rawAfterLegacy.length === LEGACY.length,
  JSON.stringify(rawAfterLegacy));

const n = st.expireTerminalHistory();
check('expireTerminalHistory 一次清干净历史关机类', n >= LEGACY.length, `n=${n}`);
check('清完后历史关机指令不再补发', st.pendingFor('UID-OLD').length === 0);
check('被作废的历史记录仍留在表里（未删）', legacyIds.every((id) => st.paramsOf(id) !== null));

// 幂等：再跑一次不应报错、也不该重复记
check('expireTerminalHistory 可重复调用', st.expireTerminalHistory() === 0);

st.closeStore();
try { fs.rmSync(DB, { force: true }); } catch { /* 临时库，删不掉就算了 */ }

console.log(`\n${process.exitCode ? 'FAILED' : 'PASSED'}  ${pass} 项通过`);
