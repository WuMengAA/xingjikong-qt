// 星集控云端 · 点歌代发单元测试（2026-10-18）
// 跑法：node tests/unit-song-request.mjs
//
// 守的红线：
//   1. Key 只在 x-api-key 头里出现，**绝不出现在 body、不返回给调用方、不进错误信息**
//   2. 上游 4xx 原样透传、5xx/超时归 502（让端上能区分"我参数错了"和"服务端挂了"）
//   3. 本地先校验 title/artist/cover，少一次外网往返
//   4. 上游回显里若带 key，错误信息必须脱敏
import assert from 'node:assert/strict';
import { requestSongViaVoiceHub } from '../src/song-request-proxy.js';

let pass = 0, fail = 0;
const ok = (m) => { pass++; console.log(`  PASS  ${m}`); };
const bad = (m, d) => { fail++; console.log(`  FAIL  ${m}`); if (d) console.log(`        ↳ ${d}`); };
const check = (c, m, d) => (c ? ok(m) : bad(m, d));

const KEY = 'vhub_SECRETKEY1234567890ABCDEF';
let lastReq = null;
let reply = null;

globalThis.fetch = async (url, opts) => {
  lastReq = { url, opts };
  if (reply instanceof Error) throw reply;
  return {
    ok: reply.ok !== false,
    status: reply.status ?? 200,
    text: async () => (typeof reply.body === 'string' ? reply.body : JSON.stringify(reply.body ?? {})),
  };
};

async function main() {
  console.log('── 一、本地校验（不发请求）──');
  {
    lastReq = null;
    const r = await requestSongViaVoiceHub({ apiKey: '', title: 'a', artist: 'b' });
    check(r.ok === false && /凭据/.test(r.error), '无 Key 直接拒绝');
    check(lastReq === null, '没有发出任何网络请求');

    const r2 = await requestSongViaVoiceHub({ apiKey: KEY, title: '', artist: 'b' });
    check(r2.ok === false && /歌曲名称/.test(r2.error), '空标题被拒');
    check(lastReq === null, '空标题不联网');

    const r3 = await requestSongViaVoiceHub({ apiKey: KEY, title: 'a', artist: '' });
    check(r3.ok === false && /艺术家/.test(r3.error), '空艺术家被拒');

    const r4 = await requestSongViaVoiceHub({ apiKey: KEY, title: 'a'.repeat(201), artist: 'b' });
    check(r4.ok === false && /200/.test(r4.error), '超 200 字符被拒');

    const r5 = await requestSongViaVoiceHub({ apiKey: KEY, title: 'a', artist: 'b', cover: 'ftp://x' });
    check(r5.ok === false && /http/.test(r5.error), '非 http(s) 封面被拒');
  }

  console.log('── 二、成功路径：Key 只出现在请求头 ──');
  {
    reply = { ok: true, status: 200, body: { ok: true, id: 42 } };
    const r = await requestSongViaVoiceHub({
      apiKey: KEY, title: '稻香', artist: '周杰伦', cover: 'https://x/y.jpg',
      extra: { musicPlatform: 'netease', musicId: '123', garbage: 'DROP' },
    });
    check(r.ok === true, '调用成功');
    check(lastReq.opts.headers['x-api-key'] === KEY, 'Key 在 x-api-key 头里');
    const bodyStr = lastReq.opts.body;
    check(!bodyStr.includes(KEY), '**body 里不含 Key**');
    check(!JSON.stringify(r).includes(KEY), '**返回值里不含 Key**');
    const b = JSON.parse(bodyStr);
    check(b.title === '稻香' && b.artist === '周杰伦', '标题/歌手正确传递');
    check(b.cover === 'https://x/y.jpg', '封面正确传递');
    check(b.musicPlatform === 'netease' && b.musicId === '123', '白名单 extra 字段传递');
    check(!('garbage' in b), '白名单外的字段被丢弃');
    check(lastReq.url.endsWith('/api/open/songs/request'), 'URL 指向正确的上游端点');
    check(lastReq.opts.method === 'POST', '用 POST');
  }

  console.log('── 三、上游错误分类 ──');
  {
    reply = { ok: false, status: 429, body: { message: '点歌太频繁' } };
    let r = await requestSongViaVoiceHub({ apiKey: KEY, title: 'a', artist: 'b' });
    check(r.ok === false && r.code === 429, '4xx 原样透传（429 配额）');
    check(/频繁/.test(r.error), '带上游错误语义');

    reply = { ok: false, status: 500, body: { message: '内部错误' } };
    r = await requestSongViaVoiceHub({ apiKey: KEY, title: 'a', artist: 'b' });
    check(r.ok === false && r.code === 500, '5xx 如实带 code（由调用方归 502）');

    reply = new Error('network down');
    r = await requestSongViaVoiceHub({ apiKey: KEY, title: 'a', artist: 'b' });
    check(r.ok === false && r.code === 0, '网络异常归 code=0');
  }

  console.log('── 四、错误信息脱敏（上游回显带 key 也不能漏）──');
  {
    reply = { ok: false, status: 400, body: { message: `bad key ${KEY} rejected` } };
    const r = await requestSongViaVoiceHub({ apiKey: KEY, title: 'a', artist: 'b' });
    check(r.ok === false, '失败');
    check(!String(r.error).includes(KEY), '**错误信息里 Key 已脱敏**');
    check(/vhub_\*\*\*/.test(r.error), '脱敏成 vhub_***');
  }

  console.log(`\n════ ${pass} 通过 / ${fail} 失败 ════`);
  process.exit(fail ? 1 : 0);
}

main();
