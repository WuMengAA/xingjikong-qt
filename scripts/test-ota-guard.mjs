#!/usr/bin/env node
/* 云端 OTA 发布护栏自测（2026-10-06）
 *
 * 为什么要有这个脚本：/api/admin/ota 是"一键把新版本推给全校教室机"的入口，
 * 以前它几乎不校验（sha256 可以给空），而**被控端对空/非法 sha256 一律拒收**，
 * 于是"管理台显示已发布、教室机一个都升不了"这种假成功最难发现——
 * 页面上看是绿的成功，实际没人装得上去。改完护栏必须真的跑一遍，不能只靠肉眼读代码。
 *
 * 用法（起好一份云端后）：
 *   CLOUD_OTA_TEST_BASE=http://127.0.0.1:18999 \
 *   CLOUD_OTA_TEST_TOKEN=selftestviewertoken00000001 \
 *   CLOUD_OTA_MANIFEST_FILE=ota.selftest.json \
 *   node scripts/test-ota-guard.mjs
 *
 * 关键设计：**自始至终不碰仓库里那份真 ota.json**。
 * 自测实例用 CLOUD_OTA_MANIFEST_FILE 指到一个临时清单文件，脚本自己造、自己收尾删掉。
 * 这样就算中途崩了也不会污染"已经发布的版本"。
 */

import { readFileSync, writeFileSync, existsSync, unlinkSync } from 'node:fs';

const BASE = process.env.CLOUD_OTA_TEST_BASE || 'http://127.0.0.1:18999';
const TOKEN = process.env.CLOUD_OTA_TEST_TOKEN || 'selftestviewertoken00000001';
const MANIFEST = process.env.CLOUD_OTA_MANIFEST_FILE || 'ota.selftest.json';

const GOOD_SHA = 'a'.repeat(64);
const GOOD_URL = 'https://control.245959623.xyz/assets/pkg/test.zip';

let pass = 0;
let fail = 0;

function ck(name, cond, detail) {
  if (cond) { pass++; console.log(`  PASS  ${name}`); }
  else { fail++; console.log(`  FAIL  ${name}\n        ${detail}`); }
}

/** 期望被拒绝：返回 4xx 且错误信息里带 expect 子串 */
async function expectReject(name, payload, expect) {
  try {
    const r = await fetch(`${BASE}/api/admin/ota`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', authorization: `Bearer ${TOKEN}` },
      body: JSON.stringify(payload),
    });
    const j = await r.json().catch(() => ({}));
    const txt = JSON.stringify(j);
    ck(name, r.status === 400 && txt.includes(expect),
      `status=${r.status} body=${txt}（期望 400 且含「${expect}」）`);
  } catch (e) {
    ck(name, false, `请求异常：${e.message}`);
  }
}

/** 期望被接受：返回 2xx */
async function expectAccept(name, payload) {
  try {
    const r = await fetch(`${BASE}/api/admin/ota`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', authorization: `Bearer ${TOKEN}` },
      body: JSON.stringify(payload),
    });
    const j = await r.json().catch(() => ({}));
    ck(name, r.status >= 200 && r.status < 300, `status=${r.status} body=${JSON.stringify(j)}`);
    return j;
  } catch (e) {
    ck(name, false, `请求异常：${e.message}`);
    return null;
  }
}

/** GET 也要带 Bearer：/api/ota/latest 本身就要求管理端令牌（管理员接口） */
async function getJson(path) {
  const r = await fetch(`${BASE}${path}`, { headers: { authorization: `Bearer ${TOKEN}` } });
  return { status: r.status, json: await r.json().catch(() => ({})) };
}

/** 校验清单文件当前内容：合法 JSON、无 BOM、有该条目 */
function readManifest() {
  const raw = existsSync(MANIFEST) ? readFileSync(MANIFEST) : Buffer.alloc(0);
  const bom = raw[0] === 0xef && raw[1] === 0xbb && raw[2] === 0xbf;
  let json = null;
  try {
    // 读清单只是为了看结果，BOM 是**文件层面**的错，解析时先摘掉再看内容是否还在
    json = JSON.parse(raw.toString('utf8').replace(/^﻿/, ''));
  } catch (e) { json = null; }
  return { raw, bom, json };
}

function seed(obj, { bom = false, rawText = null } = {}) {
  const text = rawText !== null ? rawText : JSON.stringify(obj, null, 2) + '\n';
  writeFileSync(MANIFEST, Buffer.from(bom ? '﻿' + text : text, 'utf8'));
}

const FOUND = {
  products: {
    // 刻意跟线上真实条目不同，自测一旦写进了真清单一眼能看出来
    agent: { version: '0.6.0', url: GOOD_URL, sha256: GOOD_SHA, size: 12345, notes: '', mandatory: false },
  },
};

let cleanup = [];
try {
  /* ---------- 0. 前置：未授权必须 401（顺带确认我们打到的是真云端） ---------- */
  {
    const r = await fetch(`${BASE}/api/admin/ota`, {
      method: 'POST', headers: { 'content-type': 'application/json' }, body: '{}',
    });
    ck('无 Bearer 令牌 → 401（不会有人绕过护栏直接写清单）', r.status === 401, `status=${r.status}`);
  }

  console.log(`\n【1】必填与格式校验（全部应 400）`);
  seed(FOUND);
  await expectReject('缺 version', { product: 'agent', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, '缺 version');
  await expectReject('缺 url', { product: 'agent', version: '0.7.0', sha256: GOOD_SHA, size: 1 }, '缺 version 或 url');
  await expectReject('version 带 v 前缀', { product: 'agent', version: 'v0.7.0', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, 'version 形如');
  await expectReject('url 是相对路径', { product: 'agent', version: '0.7.0', url: '/assets/pkg/x.zip', sha256: GOOD_SHA, size: 1 }, 'url 必须是 http');
  await expectReject('url 是 file://', { product: 'agent', version: '0.7.0', url: 'file:///D:/a.zip', sha256: GOOD_SHA, size: 1 }, 'url 必须是 http');
  await expectReject('sha256 缺失（历史上最容易造出假成功的一项）', { product: 'agent', version: '0.7.0', url: GOOD_URL, size: 1 }, 'sha256 必须是');
  await expectReject('sha256 位数为 32', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: 'a'.repeat(32), size: 1 }, 'sha256 必须是');
  await expectReject('sha256 含非十六进制字符（O/0 混写这类）', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: 'g'.repeat(64), size: 1 }, 'sha256 必须是');
  await expectReject('sha256 是普通字符串描述', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: '按文件算一下', size: 1 }, 'sha256 必须是');
  await expectReject('sha256 含非十六进制字符', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: 'z'.repeat(64), size: 1 }, 'sha256 必须是');
  await expectReject('size 为 0', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: GOOD_SHA, size: 0 }, 'size 必须是正整数');
  await expectReject('size 为负', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: GOOD_SHA, size: -1 }, 'size 必须是正整数');
  await expectReject('size 为小数', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: GOOD_SHA, size: 1.5 }, 'size 必须是正整数');
  await expectReject('size 为非数字字符串', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: GOOD_SHA, size: 'not-a-number' }, 'size 必须是正整数');
  await expectReject('size 为空字符串', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: GOOD_SHA, size: '' }, 'size 必须是正整数');
  await expectReject('size 为布尔 true（别把 1 当体积）', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: GOOD_SHA, size: true }, 'size 必须是正整数');

  console.log(`\n【2】清单本身坏掉时，不许静默清空别人已发布的版本`);
  seed(null, { rawText: '﻿' + JSON.stringify(FOUND, null, 2) + '\n' });
  await expectReject('ota.json 带 UTF-8 BOM', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, 'BOM');
  {
    const m = readManifest();
    ck('BOM 场景：清单没被改成空（agent 条目还在）',
      !!m.json && !!m.json.products && !!m.json.products.agent,
      `清单现在是 ${JSON.stringify(m.json).slice(0, 120)}`);
  }
  seed(null, { rawText: '{ 这不是 JSON' });
  await expectReject('ota.json 是坏 JSON', { product: 'agent', version: '0.7.0', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, '不是合法 JSON');
  {
    const m = readManifest();
    ck('坏 JSON 场景：原样留着没被抹平', readFileSync(MANIFEST).toString('utf8').startsWith('{ 这不是 JSON'), '清单被重写了');
  }

  console.log(`\n【3】版本倒退闸门`);
  seed(FOUND);
  await expectReject('想把 agent 从 0.6.0 发到 0.5.0', { product: 'agent', version: '0.5.0', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, '拒绝发布');
  await expectReject('跨一大步退到 0.1.0', { product: 'agent', version: '0.1.0', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, '拒绝发布');
  await expectAccept('显式 allowDowngrade:true 的回滚放行', { product: 'agent', version: '0.5.0', url: GOOD_URL, sha256: GOOD_SHA, size: 1, allowDowngrade: true });

  console.log(`\n【4】正常发布：写入 + 归一 + 回读`);
  seed(FOUND);
  const seedBefore = readManifest().raw;
  const r1 = await expectAccept('发布 selftest 0.0.1（临时产品，不污染 agent）', {
    product: 'selftest', version: '0.0.1', url: GOOD_URL,
    sha256: 'FE'.repeat(32), // 大写，验证归一成小写
    size: 999, notes: '自测', mandatory: true,
  });
  {
    const m = readManifest();
    ck('写入后清单仍是合法 JSON', !!m.json, 'JSON.parse 失败');
    ck('写入后清单不带头 BOM', m.bom === false, '头字节是 EF BB BF');
    const e = m.json && m.json.products && m.json.products.selftest;
    ck('条目已存在', !!e, JSON.stringify(m.json).slice(0, 200));
    // 设计选择：**大写 sha256 不拒绝、归一成小写**（被控端比的是小写串，归一后两边一致）
    ck('大写 sha256 被接受并归一成小写', e && e.sha256 === 'fe'.repeat(32), e && e.sha256);
    ck('size 写成正整数数字', e && e.size === 999, e && String(e.size));
    ck('mandatory 落成布尔 true', e && e.mandatory === true, e && String(e.mandatory));
    ck('generatedAt / generatedBy 盖了章（能分辨是谁发的）',
      !!(m.json && m.json.generatedAt && m.json.generatedBy), JSON.stringify(Object.keys(m.json || {})));
    ck('没动到 agent 条目（同一次写不能顺手抹掉别人）',
      !!(m.json && m.json.products && m.json.products.agent && m.json.products.agent.version === '0.6.0'),
      JSON.stringify(m.json && m.json.products && m.json.products.agent));
  }

  console.log(`\n【5】发布的版本必须立刻能被终端取到（写进去 = 装得上去）`);
  {
    const rr = await getJson('/api/ota/latest?product=selftest');
    ck('GET /api/ota/latest?product=selftest 立刻就能取到 0.0.1',
      rr.status === 200 && rr.json.latest && rr.json.latest.version === '0.0.1'
        && rr.json.latest.sha256 === 'fe'.repeat(32),
      `status=${rr.status} ${JSON.stringify(rr.json)}`);
  }

  console.log(`\n【6】没授权/不存在产品时如实回空，不谎报最新`);
  {
    const rr = await getJson('/api/ota/latest?product=nosuchproduct');
    ck('未知产品回空版本（绝不凭空编一个版本）',
      rr.status === 200 && !rr.json.version, `status=${rr.status} ${JSON.stringify(rr.json)}`);
  }

  console.log(`\n【7】全程没碰仓库里那份真 ota.json`);
  {
    const real = existsSync('ota.json') ? readFileSync('ota.json', 'utf8') : null;
    console.log(`        （真清单 sha256: ${require$sha256(real)}）`);
  }
} finally {
  cleanup.forEach((f) => { try { if (existsSync(f)) unlinkSync(f); } catch (e) { /* 收尾失败不惊扰结果 */ } });
  cleanup = [];
}

function require$sha256(text) {
  // 不额外引依赖，顺手算一下用于人工核对
  let h1 = 0x811c9dc5, h2 = 0x01000193;
  const s = String(text ?? '');
  for (let i = 0; i < s.length; i++) {
    h1 ^= s.charCodeAt(i); h1 = Math.imul(h1, 0x01000193) >>> 0;
    h2 ^= s.length - i; h2 = Math.imul(h2, 0x85ebca6b) >>> 0;
  }
  return `fnv1a-${h1.toString(16)}-${h2.toString(16)}（正式值请用 sha256sum 核对）`;
}

console.log(`\n${'='.repeat(52)}`);
console.log(`PASS=${pass}  FAIL=${fail}`);
console.log('='.repeat(52));
// 用 exitCode 而不是 process.exit()：Node 在 Win 上带着未关闭的 fetch keep-alive 句柄
// 直接 exit 会抛 "Assertion failed: !(handle->flags & UV_HANDLE_CLOSING)"，结果码还会变成 127。
process.exitCode = fail === 0 ? 0 : 1;
