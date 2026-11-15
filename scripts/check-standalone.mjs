#!/usr/bin/env node
// 独立部署自检（2026-11-15 · 用户点单「单独部署，而不是依靠我们系统」）
//
//   node scripts/check-standalone.mjs            # 本实例地址 + 本地凭据
//   node scripts/check-standalone.mjs --strict   # 额外拒绝官方域名 / 跨云端控制键
// 只读、不写、不连网、不打印令牌明文。--env-file 或同目录 .env 均可。
import { readFileSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  SOVEREIGNTY_CONTRACT, provisionReadiness, forbiddenConfigured, foreignBaseConfigured,
} from '../src/instance.js';

const strict = process.argv.includes('--strict');
const env = Object.assign({}, process.env);
const envFile = resolve(process.cwd(), '.env');
if (existsSync(envFile)) {
  const text = readFileSync(envFile, 'utf8');
  for (const raw of text.split('\n')) {
    const line = String(raw || '').trim();
    if (!line || line.startsWith('#')) continue;
    const eq = line.indexOf('=');
    if (eq < 1) continue;
    const key = line.slice(0, eq).trim();
    const val = line.slice(eq + 1).trim();
    if (!(key in env)) env[key] = val;
  }
}

const fails = [];
const warns = [];
function fail(m) { fails.push(m); }
function warn(m) { warns.push(m); }

// 0) 运行环境：node:sqlite 是零依赖持久化的硬前提。
const major = Number(String(process.versions.node).split('.')[0]);
if (!(major >= 22)) fail('Node 版本过低：需要 22.5+（node:sqlite），当前 ' + process.versions.node);
try { await import('node:sqlite'); } catch (e) { fail('node:sqlite 不可用：' + e.message); }
try { await import('ws'); } catch { fail('缺少 ws 依赖：先执行 npm install --omit=dev'); }

// 1) 本实例身份：必须显式声明自己的对外地址，绝不回落。
const readiness = provisionReadiness(env);
if (!readiness.ok) fail('未配置 CLOUD_PUBLIC_BASE（或格式不合法）：请在 .env 填本实例自己的 http(s) 地址');

// 2) 本地凭据：不能用开发默认值，设备与管理端令牌不能相同。
const weak = new Set(['dev-cloud-token', 'dev-admin-agent-token', 'dev-admin-viewer-token', 'dev-local-token-secret', '']);
for (const k of ['CLOUD_WS_TOKEN', 'CLOUD_VIEWER_TOKEN', 'CLOUD_VIEWER_SECRET']) {
  const v = String(env[k] || '').trim();
  if (weak.has(v)) fail(k + ' 未配置或仍是开发默认值');
  else if (v.length < 16) fail(k + ' 太短（至少 16 位随机值）');
}
if (String(env.CLOUD_WS_TOKEN || '') === String(env.CLOUD_VIEWER_TOKEN || '')) {
  fail('设备令牌与管理端令牌不能相同');
}

// 3) 实例自治：禁止任何「上级云端」形态的配置键。
const badKeys = forbiddenConfigured(env);
if (badKeys.length) fail('检测到跨云端控制键：' + badKeys.join(', '));

// 4) 严格模式：面向别的学校 / 电教委员，连官方域名也不许写。
if (strict && foreignBaseConfigured(env)) {
  fail('CLOUD_PUBLIC_BASE 仍指向本项目官方域名；独立部署必须填学校自己的域名');
}
if (strict && String(env.STE_SITE_URL || '').trim()) {
  warn('STE_SITE_URL 已配置：管理台会依赖外部站点账号登录；纯独立部署建议留空，改用手敲静态令牌');
}
if (strict && String(env.CLOUD_WS_HOST || '0.0.0.0').trim() === '127.0.0.1') {
  warn('CLOUD_WS_HOST=127.0.0.1：只有本机能连；对外服务请设 0.0.0.0 并自备反向代理');
}
if (strict && String(env.VOICEHUB_BASE_URL || '').trim()) {
  warn('VOICEHUB_BASE_URL 是可选内容源（点歌），不是控制通道；留空即关闭，不影响班级后台');
}

if (fails.length) {
  console.error('STANDALONE_CHECK FAIL  reasons=' + fails.length);
  for (const f of fails) console.error(' - ' + f);
  process.exit(1);
}
for (const w of warns) console.log('WARN  ' + w);
console.log('STANDALONE_CHECK PASS  strict=' + strict + '  node=' + process.versions.node);
console.log('  contract=' + SOVEREIGNTY_CONTRACT.version + '  crossCloudControl=' + SOVEREIGNTY_CONTRACT.crossCloudControl);
console.log('  publicBase=' + readiness.publicBase);
console.log('  agentWsUrl=' + readiness.agentWsUrl);
console.log('  viewerWsUrl=' + readiness.viewerWsUrl);
console.log('  statement=' + SOVEREIGNTY_CONTRACT.statement);
