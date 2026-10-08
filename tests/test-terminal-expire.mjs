// 验证 expireTerminalHistory 修复：只作废旧 shutdown/reboot，不误杀正常指令（2026-10-08）
// 运行：cd cloud-ws && node tests/test-terminal-expire.mjs
import { rmSync, readdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, basename } from 'node:path';

// ⚠️ 必须在动态 import store.js 之前设好环境变量——config.js 在 import 时读它，
//    静态 import 会先于赋值的 env 执行，导致误写真实 instruction-queue.db。
const DB = join(tmpdir(), `test-terminal-expire-${process.pid}.db`);
process.env.CLOUD_QUEUE_DB_FILE = DB;
for (const k of readdirSync(tmpdir())) {
  if (k.startsWith(basename(DB).replace('.db', ''))) {
    try { rmSync(join(tmpdir(), k), { force: true }); } catch { /* 占用就跳过 */ }
  }
}

const st = await import('../src/store.js');
st.initStore();

let pass = 0, fail = 0;
const ok = (n, c, x) => { if (c) { pass++; console.log(`  ✅ ${n}`); } else { fail++; console.log(`  ❌ ${n}`, x || ''); } };

// 一条旧 shutdown(no_resend=0 模拟加列前遗留) + 两条正常指令 + 一条新版已标 no_resend 的 reboot
st.enqueueDispatch('d1', 'shutdown', {}, 0);
st.enqueueDispatch('d2', 'screen_record_stop', {});
st.enqueueDispatch('d3', 'notify', { notice_id: 7 });
st.enqueueDispatch('d4', 'reboot', {}, 1);

const n = st.expireTerminalHistory();
ok('expire 返回 1（只作了 shutdown）', n === 1, `got ${n}`);

ok('shutdown 不再 pending', st.pendingFor('d1').length === 0,
   `d1 pending=${st.pendingFor('d1').length}`);
ok('screen_record_stop 未被误删', st.pendingFor('d2').length === 1,
   `d2 pending=${st.pendingFor('d2').length}`);
ok('notify 未被误删', st.pendingFor('d3').length === 1,
   `d3 pending=${st.pendingFor('d3').length}`);
ok('已标 no_resend 的 reboot 本就不进 pending', st.pendingFor('d4').length === 0,
   `d4 pending=${st.pendingFor('d4').length}（设计如此，非误删）`);

ok('幂等（第二次 expire 返回 0）', st.expireTerminalHistory() === 0);

try { rmSync(DB, { force: true }); } catch { /* 忽略 */ }
console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);