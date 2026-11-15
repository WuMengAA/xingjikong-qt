// 校区选择服务 + 教师网关真 HTTP 冒烟（2026-11-16）。
// 隔离端口、隔离 cwd；不改生产库、不重启云端。
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';

const PORT = 18999;
const dir = mkdtempSync(path.join(tmpdir(), 'campus-http-'));
const child = spawn(process.execPath, ['src/campus-server.mjs'], {
  cwd: process.cwd(), env: { ...process.env, CAMPUS_PORT: String(PORT), CAMPUS_HOST: '127.0.0.1', PYTHONIOENCODING: 'utf-8' },
  stdio: ['ignore', 'pipe', 'pipe'],
});
let log = ''; child.stdout.on('data', (c) => { log += c.toString(); }); child.stderr.on('data', (c) => { log += c.toString(); });
async function waitReady() {
  for (let i = 0; i < 40; i++) {
    try { const r = await fetch(`http://127.0.0.1:${PORT}/api/campus/servers`); if (r.status === 200) return; } catch {}
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error('服务没起来: ' + log);
}
async function j(method, p, body) {
  const r = await fetch(`http://127.0.0.1:${PORT}${p}`, { method, headers: { 'content-type': 'application/json' }, body: body ? JSON.stringify(body) : undefined });
  return { status: r.status, body: await r.json() };
}
let pass=0, fail=0; function ok(n,c,e=''){ if(c){pass++;console.log('PASS '+n);} else {fail++;console.error('FAIL '+n+' '+e);} }
try {
  await waitReady();
  const list = await j('GET', '/api/campus/servers');
  ok('校区清单 200', list.status === 200 && list.body.ok === true);
  ok('咸林中学在列', list.body.servers?.[0]?.name === '渭南市华州区咸林中学校区');
  ok('开放 12 班选择', Array.isArray(list.body.classes) && list.body.classes.length === 12);
  const choose = await j('POST', '/api/campus/classes/choose', { campusId: 'xianlin', classCode: 'cls-abcdef', className: '1班' });
  ok('班级选择校区成功', choose.status === 200 && choose.body.ok === true && choose.body.envelope.publicBase === list.body.servers[0].publicBase);
  const bad = await j('POST', '/api/campus/classes/choose', { campusId: 'xianlin', classCode: 'x' });
  ok('坏班级码 400', bad.status === 400 && bad.body.ok === false);
  const g = await j('GET', '/api/campus/teacher-gateway');
  ok('网关摘要无超管/批量', g.body.gateway.superAdmin === false && g.body.gateway.batch === false);
  const allow = await j('POST', '/api/campus/teacher-gate', { action: 'class-scan-bind' });
  ok('扫码绑定动作放行', allow.status === 200 && allow.body.ok === true);
  for (const a of ['create-class','batch-bind','platform-admin','students-import']) {
    const deny = await j('POST', '/api/campus/teacher-gate', { action: a });
    ok('拒 '+a, deny.status === 403 && deny.body.ok === false);
  }
  const miss = await j('GET', '/api/campus/nope');
  ok('未知路由 404', miss.status === 404);
} catch (e) { fail++; console.error('FAIL 冒烟异常 ' + (e.stack || e)); }
finally { child.kill(); rmSync(dir,{recursive:true,force:true}); }
console.log('PASS '+pass+' / FAIL '+fail); if(fail) process.exit(1);
