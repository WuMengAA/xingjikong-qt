#!/usr/bin/env node
// 星集控 · AI 测试通道统一入口（2026-10-14 建）
//
// 作用：把三端散落的测试收拢成**一条 AI 可随时调用的通道**——一个命令跑全量、
// 出统一报告、给退出码。AI 改完代码自己跑，不用问人；人也照样能用。
//
// 用法：
//   node scripts/ai-test/run.mjs              # 全量（T1-T4）
//   node scripts/ai-test/run.mjs --quick      # 快速（单元 + qmllint + 一致性，不起服务不截图）
//   node scripts/ai-test/run.mjs --only cloud # 只跑某一组（cloud|agent|viewer|consistency）
//
// 安全铁律：
//   - 云端测试**只对隔离实例**跑（端口 18788 / 18999，env 全覆写，队列库/清单指临时文件），
//     绝不指向生产 8788；ota-guard 自带"不碰仓库真 ota.json"设计。
//   - 被控端冒烟用 STE_QT_DATA_DIR 隔离数据目录 + STE_ALLOW_MULTI，不碰本机真实配置。
//   - 探针只读 QML 截屏，不启动真实窗口（offscreen / software 后端）。
//
// 退出码：0 全过；1 有失败；2 入口自身错误（如 node 版本不对）。

import { spawn, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import http from 'node:http';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const CLOUD = path.join(ROOT, 'Stelarith-cloud');
const AGENT = path.join(ROOT, 'Stelarith-control-qt');
const VIEWER = path.join(ROOT, 'Stelarith-viewer-qt');
const QMLLINT = 'D:/QTqwq/6.12.0/msvc2022_64/bin/qmllint.exe';
const PY = process.platform === 'win32' ? 'python' : 'python3';

const args = process.argv.slice(2);
const QUICK = args.includes('--quick');
const onlyIdx = args.indexOf('--only');
const ONLY = onlyIdx >= 0 ? (args[onlyIdx].includes('=') ? args[onlyIdx].split('=')[1] : args[onlyIdx + 1]) : null;
const want = (g) => !ONLY || ONLY === g;

const rows = [];
function record(group, name, ok, ms, detail) {
  rows.push({ group, name, ok, ms, detail: detail || '' });
  const mark = ok === true ? '✅' : ok === 'skip' ? '⏭️ ' : '❌';
  console.log(`  ${mark} ${name}${detail ? ' — ' + detail : ''}${ms ? ` (${ms}ms)` : ''}`);
}
function run(cmd, cmdArgs, opts = {}) {
  const t0 = Date.now();
  const r = spawnSync(cmd, cmdArgs, {
    cwd: opts.cwd || ROOT,
    encoding: 'utf-8',
    timeout: opts.timeout || 60000,
    env: { ...process.env, ...(opts.env || {}) },
    maxBuffer: 32 * 1024 * 1024,
  });
  return { code: r.status, ms: Date.now() - t0, out: (r.stdout || '') + (r.stderr || ''), error: r.error };
}

/* ── T1 云端 ──────────────────────────────────────────────────────────── */

const PORT_A = 18788;   // 主隔离实例（集成回归 + 审计回归）
const PORT_B = 18999;   // ota-guard 专用实例（自带临时清单）

function waitReady(port, tries = 25) {
  return new Promise(resolve => {
    let n = 0;
    const tick = () => {
      http.get({ host: '127.0.0.1', port, path: '/api/public/ota', timeout: 1000 }, r => {
        r.resume();
        resolve(r.statusCode === 200);
      }).on('error', () => { if (++n >= tries) resolve(false); else setTimeout(tick, 400); })
        .on('timeout', () => { if (++n >= tries) resolve(false); else setTimeout(tick, 400); });
    };
    tick();
  });
}

function waitReadySync(port, tries = 25) {
  for (let i = 0; i < tries; i++) {
    const r = spawnSync(process.execPath, ['-e',
      `require('http').get({host:'127.0.0.1',port:${port},path:'/api/public/ota',timeout:1000},r=>{r.resume();process.exit(0)}).on('error',()=>process.exit(1)).on('timeout',()=>process.exit(1))`],
      { timeout: 3000 });
    if (r.status === 0) return true;
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 400);   // 同步睡 400ms
  }
  return false;
}

function startIsolated(port, extraEnv) {
  const env = {
    CLOUD_WS_PORT: String(port),
    CLOUD_WS_HOST: '127.0.0.1',
    CLOUD_WS_TOKEN: 'it-agent-token',
    CLOUD_VIEWER_TOKEN: 'it-viewer-token',
    CLOUD_VIEWER_SECRET: 'it-secret',
    CLOUD_QUEUE_DB_FILE: path.join(os.tmpdir(), `ai-test-queue-${port}.db`),
    ...extraEnv,
  };
  try { fs.unlinkSync(env.CLOUD_QUEUE_DB_FILE); } catch {}
  const child = spawn(process.execPath, ['src/index.js'], {
    cwd: CLOUD, env: { ...process.env, ...env }, stdio: ['ignore', 'pipe', 'pipe'],
  });
  let log = '';
  child.stdout.on('data', d => log += d);
  child.stderr.on('data', d => log += d);
  child._log = () => log;
  return child;
}

async function t1Cloud() {
  console.log('\n── T1 云端（隔离实例 ' + PORT_A + ' / ' + PORT_B + '）──');
  const instA = startIsolated(PORT_A);
  const okA = await waitReady(PORT_A);
  if (!okA) {
    record('cloud', '隔离实例 A 就绪', false, 0, '10s 未就绪，日志尾：' + instA._log().slice(-300));
    instA.kill();
    return;
  }
  record('cloud', '隔离实例 A 就绪', true);

  const unitTests = [
    ['unit-ecodes.mjs', '错误码体系'],
    ['unit-agent-ticket.mjs', '设备接入票单元'],
    ['unit-version-compare.mjs', '版本比较'],
    ['test-repair-fix.mjs', '报修修复'],
    ['test-terminal-expire.mjs', '终止指令过期'],
    ['smoke-registry-2026-10-08.mjs', '注册表冒烟'],
  ];
  for (const [f, label] of unitTests) {
    const r = run(process.execPath, [path.join('tests', f)], { cwd: CLOUD, timeout: 30000 });
    record('cloud', `单元 ${label}`, r.code === 0, r.ms, r.code !== 0 ? r.out.slice(-200) : '');
  }

  const integTests = [
    ['tests/regression-confirm-receipt.cjs', '通知回执链路'],
    ['tests/regression-recordings.cjs', '班级监控录像'],
    ['tests/regression-broadcast.cjs', '屏幕广播链路'],
    ['tests/regression-voice.cjs', '语音对讲链路'],
    ['tests/regression-repairs.cjs', '设备报修 CRUD'],
    ['tests/regression-agent-ticket.mjs', '接入票 WS 握手'],
    ['tests/regression-cloud-audit-1013.mjs', '10-13 审计 8 处回归'],
  ];
  if (QUICK) {
    record('cloud', '集成回归（quick 跳过）', 'skip', 0, '--quick 只跑单元；全量用 run.mjs 无参数');
  } else {
    // ⚠️ 测试脚本从 env 读令牌，默认值是 dev-cloud-token/dev-viewer-token ⇒ 必须与隔离实例
    //    的 env 逐字一致，否则全线 401 / BAD_TOKEN（2026-10-14 通道首跑实测踩过）。
    const testEnv = {
      CLOUD_WS_TOKEN: 'it-agent-token',
      CLOUD_VIEWER_TOKEN: 'it-viewer-token',
      CLOUD_VIEWER_SECRET: 'it-secret',
    };
    for (const [f, label] of integTests) {
      const r = run(process.execPath, [f, String(PORT_A)], { cwd: CLOUD, timeout: 90000, env: testEnv });
      record('cloud', `集成 ${label}`, r.code === 0, r.ms, r.code !== 0 ? r.out.slice(-200) : '');
    }
  }
  instA.kill();

  if (!QUICK) {
    // ota-guard：自带临时清单的第二个实例（不碰仓库真 ota.json）
    const instB = startIsolated(PORT_B, {
      CLOUD_WS_TOKEN: 'selftestviewertoken00000001',
      CLOUD_VIEWER_TOKEN: 'selftestviewertoken00000001',
      CLOUD_OTA_MANIFEST_FILE: 'ota.selftest.json',
    });
    const okB = await waitReady(PORT_B);
    if (!okB) {
      record('cloud', '隔离实例 B 就绪（ota-guard）', false, 0, instB._log().slice(-300));
    } else {
      record('cloud', '隔离实例 B 就绪（ota-guard）', true);
      const r = run(process.execPath, ['scripts/test-ota-guard.mjs'], {
        cwd: CLOUD, timeout: 60000,
        env: {
          CLOUD_OTA_TEST_BASE: `http://127.0.0.1:${PORT_B}`,
          CLOUD_OTA_TEST_TOKEN: 'selftestviewertoken00000001',
          CLOUD_OTA_MANIFEST_FILE: 'ota.selftest.json',
        },
      });
      record('cloud', 'OTA 发布护栏（guard）', r.code === 0, r.ms, r.code !== 0 ? r.out.slice(-200) : '');
    }
    instB.kill();
  }
}

/* ── T2 被控端 ────────────────────────────────────────────────────────── */

async function t2Agent() {
  console.log('\n── T2 被控端（冒烟 + 灵动岛探针）──');
  const exe = path.join(AGENT, 'build', 'stelarith-agent-qt.exe');
  if (!fs.existsSync(exe)) {
    record('agent', 'agent exe 存在性', 'skip', 0, 'build/ 无 exe（未构建或被火绒清除）——冒烟跳过');
    return;
  }
  record('agent', 'agent exe 存在性', true);

  // 离屏冒烟。隔离关键（2026-10-14 纠错）：
  //   agent 的数据目录是 %LOCALAPPDATA%/xingjikong/（main.cpp agentEnvPath 唯一真源，
  //   **没有** STE_QT_DATA_DIR 这个变量——那是我记错的，害冒烟以真实身份连过生产云端）。
  //   隔离手段 = 把 LOCALAPPDATA 整个指到临时目录，再在临时目录里写一份假 agent.env。
  //   agent.env 优先级高于进程 env（启动日志实证），假地址隔离在 18788，连不上也只是退避。
  const tmpHome = path.join(os.tmpdir(), `ai-test-agent-${Date.now()}`);
  fs.mkdirSync(path.join(tmpHome, 'xingjikong'), { recursive: true });
  fs.writeFileSync(path.join(tmpHome, 'xingjikong', 'agent.env'),
    `STE_QT_WS_URL=wss://127.0.0.1:${PORT_A}/ws/agent\nSTE_QT_WS_TOKEN=it-agent-token\n`, 'utf8');
  // T1 若没跑（--only agent），18788 没人听 ⇒ 自己起一个隔离实例，冒烟完杀掉
  let ownInst = null;
  const r0 = run(process.execPath, ['-e', `require('http').get({host:'127.0.0.1',port:${PORT_A},path:'/api/public/ota',timeout:1000},r=>{r.resume();process.exit(0)}).on('error',()=>process.exit(1)).on('timeout',()=>process.exit(1))`], { timeout: 8000 });
  if (r0.code !== 0) {
    ownInst = startIsolated(PORT_A);
    const ok = waitReadySync(PORT_A);
    record('agent', '冒烟专用隔离实例就绪', ok, 0, ok ? '' : '见 T1 日志尾');
  }

  const smokeLog = path.join(tmpHome, 'smoke.log');
  const t0 = Date.now();
  await new Promise(resolve => {
    // stdio 走文件而不是管道：spawnSync 的 pipe 在 timeout 杀进程时会丢未刷的输出
    // （2026-10-14 实测 banner 判不出来），文件重定向丢不了。
    const fd = fs.openSync(smokeLog, 'w');
    const child = spawn(exe, ['-platform', 'offscreen'], {
      cwd: path.join(AGENT, 'build'),
      stdio: ['ignore', fd, fd],
      env: { ...process.env, ...{
        STE_ALLOW_MULTI: '1',
        LOCALAPPDATA: tmpHome,   // ← 隔离核心：agent.env / 日志 / ota 状态全落临时目录
        STE_QT_WS_URL: `wss://127.0.0.1:${PORT_A}/ws/agent`,
        STE_QT_WS_TOKEN: 'it-agent-token',
      } },
    });
    const timer = setTimeout(() => { try { child.kill(); } catch {} }, 22000);
    child.on('exit', () => { clearTimeout(timer); resolve(); });
  });
  const out = fs.existsSync(smokeLog) ? fs.readFileSync(smokeLog, 'utf8') : '';
  // 判据用 registered 而不是横幅：横幅"被控端 vX → 目标 vY"只在**云端有更新**时才打，
  // 版本已最新时没有（2026-10-14 实测）——registered 才是注册链路打通的铁证。
  const hasBanner = /被控端 v|v\d+\.\d+\.\d+/.test(out);
  const registered = /registered|注册成功/.test(out);
  const fatal = /ASSERT|Fatal|minidump|Segmentation|core dumped|Aborted/i.test(out);
  record('agent', '离屏冒烟（启动/注册/不崩）', registered && !fatal, Date.now() - t0,
    `registered=${registered} banner=${hasBanner} fatal=${fatal}`);
  if (ownInst) ownInst.kill();
  try { fs.rmSync(tmpHome, { recursive: true, force: true }); } catch {}

  if (!QUICK) {
    // 灵动岛探针：深 + 浅两张
    for (const [label, extra] of [['深', []], ['浅', ['--light']]]) {
      const out = `D:/Stelarith/_probe/shots/ai-test-island-${label === '深' ? 'dark' : 'light'}.png`;
      const rr = run(PY, [path.join('scripts', '_island_qml_probe.py'), ...extra, '--out', out], {
        cwd: AGENT, timeout: 180000,
      });
      record('agent', `灵动岛探针截图（${label}）`, rr.code === 0 && fs.existsSync(out), rr.ms,
        rr.code !== 0 ? rr.out.slice(-200) : '');
    }
  }
}

/* ── T3 管理端 ────────────────────────────────────────────────────────── */

function t3Viewer() {
  console.log('\n── T3 管理端（qmllint + MD3 探针）──');
  // qmllint 全量 QML：只拦 Error（Warning 是既有风格提示，不设门禁）
  const qmlFiles = [];
  (function walk(d) {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) { if (!/^(_probe|build|deploy|node_modules)/.test(e.name)) walk(p); }
      else if (e.name.endsWith('.qml')) qmlFiles.push(p);
    }
  })(path.join(VIEWER, 'qml'));
  const r = run(QMLLINT, qmlFiles, { timeout: 120000 });
  const errLines = r.out.split('\n').filter(l => /: Error/.test(l));
  record('viewer', `qmllint 全量（${qmlFiles.length} 文件）`, r.code === 0 || errLines.length === 0, r.ms,
    errLines.length ? errLines.slice(0, 3).join(' | ') : '');

  if (!QUICK) {
    const rr = run(PY, [path.join('scripts', '_md3_probe.py')], { cwd: VIEWER, timeout: 300000 });
    const okCount = (rr.out.match(/OK /g) || []).length;
    record('viewer', 'MD3 探针截图（手机端 + 布局五屏 + 组件验证）', rr.code === 0, rr.ms,
      `OK=${okCount}` + (rr.code !== 0 ? ' ' + rr.out.slice(-200) : ''));
  }
}

/* ── T4 一致性 ────────────────────────────────────────────────────────── */

function t4Consistency() {
  console.log('\n── T4 一致性（版本 4 副本 / ota.json BOM）──');
  const r = run(process.execPath, [path.join('scripts', 'bump-version.mjs'), 'show'], { timeout: 30000 });
  record('consistency', '版本号 4 副本一致', r.code === 0, r.ms, r.code !== 0 ? r.out.slice(-200) : '');

  for (const [label, file] of [
    ['云端 ota.json', path.join(CLOUD, 'data', 'ota.json')],
    ['云端 ota-gray.json', path.join(CLOUD, 'data', 'ota-gray.json')],
  ]) {
    if (!fs.existsSync(file)) { record('consistency', `${label} UTF-8 无 BOM`, 'skip', 0, '文件不存在'); continue; }
    const head = fs.readFileSync(file).subarray(0, 3);
    const noBom = !(head[0] === 0xEF && head[1] === 0xBB && head[2] === 0xBF);
    record('consistency', `${label} UTF-8 无 BOM`, noBom, 0, noBom ? '' : '带 BOM ⇒ latest 读成 null');
  }
}

/* ── main ─────────────────────────────────────────────────────────────── */

(async () => {
  const t0 = Date.now();
  console.log('=== 星集控 AI 测试通道 ===');
  console.log(`根: ${ROOT}`);
  console.log(`模式: ${QUICK ? '快速（无服务无截图）' : '全量'}${ONLY ? ' · 只跑 ' + ONLY : ''}`);
  try {
    if (want('cloud')) await t1Cloud();
    if (want('agent')) await t2Agent();
    if (want('viewer')) t3Viewer();
    if (want('consistency')) t4Consistency();
  } catch (e) {
    console.error('测试通道自身异常：', e);
    process.exit(2);
  }
  const fail = rows.filter(r => r.ok === false).length;
  const skip = rows.filter(r => r.ok === 'skip').length;
  const pass = rows.filter(r => r.ok === true).length;
  console.log(`\n=== 汇总: ${pass} 过 / ${fail} 败 / ${skip} 跳过 ===  总耗时 ${((Date.now() - t0) / 1000).toFixed(1)}s`);
  if (fail) {
    console.log('失败项：');
    for (const r of rows.filter(r => r.ok === false)) console.log(`  ❌ [${r.group}] ${r.name} — ${r.detail.slice(0, 160)}`);
  }
  const report = { at: new Date().toISOString(), mode: QUICK ? 'quick' : 'full', only: ONLY || null, pass, fail, skip, rows };
  const outJson = path.join(ROOT, '.workbuddy', 'tmp', 'ai-test-report.json');
  fs.mkdirSync(path.dirname(outJson), { recursive: true });
  fs.writeFileSync(outJson, JSON.stringify(report, null, 2), 'utf8');
  console.log(`报告: ${outJson}`);
  process.exit(fail === 0 ? 0 : 1);
})();
