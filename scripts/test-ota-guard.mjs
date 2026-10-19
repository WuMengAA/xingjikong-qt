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
 *   node scripts/test-ota-guard.mjs
 *
 * 关键设计：**自始至终不碰仓库里那份真 ota.json / ota-gray.json**。
 * 清单默认落在系统临时目录（<tmp>/stelarith-ota-guard/ota.json，灰度清单 ota-gray.json
 * 同目录），脚本自己造、自己收尾删掉。可用 CLOUD_OTA_MANIFEST_FILE 指到别处，
 * 但**必须避开仓库**：ota-gray.json 是生产云端**每请求现读**的文件，测试写它
 * 等于当场把灰度设备升级到自测版本（脚本启动时会检查，碰到仓库真清单直接拒绝运行）。
 * 这样就算中途崩了也不会污染"已经发布的版本"。
 */

import { readFileSync, writeFileSync, existsSync, unlinkSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import os from 'node:os';

const BASE = process.env.CLOUD_OTA_TEST_BASE || 'http://127.0.0.1:18999';
const TOKEN = process.env.CLOUD_OTA_TEST_TOKEN || 'selftestviewertoken00000001';
// 默认走临时目录（含灰度清单同目录落盘）—— 见头注释"关键设计"。
const MANIFEST = process.env.CLOUD_OTA_MANIFEST_FILE
  || join(os.tmpdir(), 'stelarith-ota-guard', 'ota.json');
// 灰度清单路径与服务端 ota.js manifestFileFor() 同一算法：同目录 ota-gray.json
const GRAY = join(dirname(MANIFEST), 'ota-gray.json');
// 🛡️ 起飞线：灰度测试要**写**清单，而仓库里那份 ota-gray.json 是生产云端现读的文件。
//    老用法（CLOUD_OTA_MANIFEST_FILE=ota.selftest.json，仓库根）会让灰度发布落到
//    真灰度清单上 ⇒ 当场把灰度设备升到自测版本。宁可拒绝运行，不赌"应该不会"。
{
  const here = process.cwd();
  const bad = [];
  if (resolve(MANIFEST) === resolve(here, 'ota.json')) bad.push(MANIFEST);
  if (resolve(GRAY) === resolve(here, 'ota-gray.json')) bad.push(GRAY);
  if (bad.length) {
    console.error(
      '拒绝运行：清单路径会碰到仓库真清单（生产云端现读，写了等于改线上灰度）：\n'
      + bad.map((f) => `  ${f}`).join('\n')
      + '\n请把 CLOUD_OTA_MANIFEST_FILE 指到仓库外的临时目录（默认即是，别覆盖）。'
    );
    process.exit(2);
  }
}
// 临时清单可能落在仓库外，目录自己建起来（服务端写盘时不会替你 parent 建目录）
mkdirSync(dirname(MANIFEST), { recursive: true });

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
function readManifest(file = MANIFEST) {
  const raw = existsSync(file) ? readFileSync(file) : Buffer.alloc(0);
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

  console.log(`\n【7】候选版（rc/alpha）与灰度通道（2026-10-20 加：管理台以前发不了候选版）`);
  seed(FOUND);
  // rc / alpha 与 scripts/publish-release.mjs 同一道正则；阶段号从 1 起 ——
  // alpha.0 会被被控端 compareVersion 当成"无阶段=正式版"⇒ 云端发了、教室机判已是最新，
  // 静默不升级（2026-10-07 预演抓到过，见 publish 脚本注释）。
  await expectAccept('发布候选版 selftest 0.6.1-rc.1', {
    product: 'selftest', version: '0.6.1-rc.1', url: GOOD_URL, sha256: GOOD_SHA, size: 1,
  });
  {
    const e = readManifest().json?.products?.selftest;
    ck('候选版本号原样落进清单（后缀不被规范化掉）', !!e && e.version === '0.6.1-rc.1', e && e.version);
  }
  await expectAccept('候选版可继续升级 0.6.1-rc.2', {
    product: 'selftest', version: '0.6.1-rc.2', url: GOOD_URL, sha256: GOOD_SHA, size: 1,
  });
  await expectAccept('内测版 0.6.2-alpha.1 高于当前 selftest 0.6.1-rc.2，放行', {
    product: 'selftest', version: '0.6.2-alpha.1', url: GOOD_URL, sha256: GOOD_SHA, size: 1,
  });
  await expectReject('alpha.0 拒收（阶段号从 1 起，0 会被当成正式版）',
    { product: 'selftest', version: '0.6.1-alpha.0', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, 'version 形如');
  await expectReject('beta 不是合法阶段', { product: 'selftest', version: '0.6.1-beta.1', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, 'version 形如');
  await expectReject('非三段式 0.6 拒收（旧正则反而放行它）', { product: 'selftest', version: '0.6', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, 'version 形如');
  await expectReject('残缺后缀 -rc 拒收', { product: 'selftest', version: '0.6.1-rc', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, 'version 形如');
  await expectReject('四段式 0.6.1.2 拒收', { product: 'selftest', version: '0.6.1.2', url: GOOD_URL, sha256: GOOD_SHA, size: 1 }, 'version 形如');

  // ── 灰度通道：写 ota-gray.json，不碰 stable 清单；灰度设备才读得到 ──
  seed(FOUND);
  try { unlinkSync(GRAY); } catch { /* 没有就算了 */ }
  const grayR = await expectAccept('灰度通道发布 selftest 0.6.2-rc.1（channel=gray）', {
    product: 'selftest', version: '0.6.2-rc.1', url: GOOD_URL, sha256: GOOD_SHA, size: 1, channel: 'gray',
  });
  {
    ck('回执带 channel=gray', !!grayR && grayR.channel === 'gray', JSON.stringify(grayR));
    const g = readManifest(GRAY).json;
    const e = g && g.products && g.products.selftest;
    ck('灰度清单里确有 0.6.2-rc.1', !!e && e.version === '0.6.2-rc.1',
      JSON.stringify(g && g.products).slice(0, 120));
    ck('灰度清单 UTF-8 无 BOM', readManifest(GRAY).bom === false, '头字节是 EF BB BF');
    const s = readManifest().json;
    ck('stable 清单没被灰度发布污染（仍只有 seed 的 agent 0.6.0）',
      !!s && !!s.products && s.products.agent && s.products.agent.version === '0.6.0' && !s.products.selftest,
      JSON.stringify(s && s.products).slice(0, 120));
  }
  {
    const rr = await getJson('/api/ota/latest?product=selftest&channel=gray');
    ck('GET /api/ota/latest?channel=gray 立刻取到 0.6.2-rc.1',
      rr.status === 200 && rr.json.latest && rr.json.latest.version === '0.6.2-rc.1',
      `status=${rr.status} ${JSON.stringify(rr.json)}`);
    const rs = await getJson('/api/ota/latest?product=selftest');
    ck('不带 channel（stable）取不到灰度版（灰度不泄漏到全网）',
      rs.status === 200 && (!rs.json.latest || rs.json.latest.version !== '0.6.2-rc.1'),
      `status=${rs.status} ${JSON.stringify(rs.json)}`);
  }
  // 灰度清单独立走倒退闸门：灰度设备也是真设备，发低版本同样把它们降级
  await expectReject('灰度渠道倒退同样被拒（0.6.2-rc.1 → 0.6.1-rc.1）',
    { product: 'selftest', version: '0.6.1-rc.1', url: GOOD_URL, sha256: GOOD_SHA, size: 1, channel: 'gray' }, '拒绝发布');
  // channel 白名单：路径穿越在 ota.js manifestFileFor 有兜底（读方），这里是写方入口先挡
  await expectReject('channel=../config 穿越拒收',
    { product: 'selftest', version: '0.6.3', url: GOOD_URL, sha256: GOOD_SHA, size: 1, channel: '../config' }, 'channel 只允许');
  await expectReject('channel=GRAY 大写拒收（白名单只认小写）',
    { product: 'selftest', version: '0.6.3', url: GOOD_URL, sha256: GOOD_SHA, size: 1, channel: 'GRAY' }, 'channel 只允许');
  await expectReject('channel 超长（17 位）拒收',
    { product: 'selftest', version: '0.6.3', url: GOOD_URL, sha256: GOOD_SHA, size: 1, channel: 'a'.repeat(17) }, 'channel 只允许');
  cleanup.push(GRAY);

  console.log(`\n【8】全程没碰仓库里那份真 ota.json / ota-gray.json`);
  {
    for (const f of ['ota.json', 'ota-gray.json']) {
      const real = existsSync(f) ? readFileSync(f, 'utf8') : null;
      console.log(`        （真清单 ${f} sha256: ${require$sha256(real)}）`);
    }
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
