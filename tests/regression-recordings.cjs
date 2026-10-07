// 班级监控 recordings 链路回归测试（2026-10-07 固化）
//
// 作用：验证被控端上传录制 → 管理端列表 → 静态播放，以及设备令牌鉴权边界。
// 覆盖 fe4529b 的四条关键路径：
//   1) 设备令牌上传 mp4 → 200 {ok,uid,fname}
//   2) 无令牌 / 错误令牌上传 → 401 AU1003
//   3) 管理端令牌列表 → 含刚上传的 uid
//   4) 静态下载 /recordings/<uid>/<fname> → 200 video/mp4
//
// 用法（先起隔离云端，再联测）：
//   CLOUD_WS_PORT=8095 ... node src/index.js
//   node tests/regression-recordings.cjs [port] [uid]
//
// 安全：只对隔离实例跑；测试结束尝试清理自己建的 recordings/<uid>（rm）；
//       不要把 CLOUD_WS_TOKEN 指到生产，否则测试文件写进生产目录。
// 返回码：0 全过；非 0 有失败。

const path = require('path');
const repo = path.resolve(__dirname, '..');
const http = require('http');
const fs = require('fs');

const PORT = Number(process.argv[2] || 8095);
const UID = String(process.argv[3] || 'REC' + Math.floor(Math.random() * 900 + 100));
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-viewer-token';

let pass = 0, fail = 0, uploadedFname = '';
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  \u2705 ${name}${detail ? ' \u2014 ' + detail : ''}`); }
  else { fail++; console.log(`  \u274c ${name}${detail ? ' \u2014 ' + detail : ''}`); }
}

function req(method, p, token, body, ctype) {
  return new Promise((resolve) => {
    const buff = body || Buffer.alloc(0);
    const opts = {
      host: HOST, port: PORT, path: p, method,
      headers: {
        'content-type': ctype || 'application/octet-stream',
        'content-length': buff.length,
        ...(token ? { authorization: 'Bearer ' + token } : {}),
      },
    };
    const r = http.request(opts, (res) => {
      const chunks = [];
      res.on('data', (c) => chunks.push(c));
      res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
    });
    r.on('error', (e) => resolve({ status: 0, body: Buffer.from(String(e)) }));
    r.write(buff); r.end();
  });
}

(async () => {
  // 假 mp4（>1KB）
  const fake = Buffer.alloc(1500, 0xab);

  // 1) 设备令牌上传
  let r = await req('POST', `/api/recordings/upload?uid=${UID}`, AGENT_TOKEN, fake);
  let j = null; try { j = JSON.parse(r.body.toString()); } catch {}
  check('设备令牌上传', r.status === 200 && j?.ok === true, `status=${r.status} uid=${j?.uid}`);
  uploadedFname = j?.fname || '';

  // 2) 无令牌上传 → 401
  r = await req('POST', `/api/recordings/upload?uid=${UID}`, null, fake);
  check('无令牌上传被拒', r.status === 401, `status=${r.status}`);

  // 3) 错误令牌上传 → 401
  r = await req('POST', `/api/recordings/upload?uid=${UID}`, 'wrong-token', fake);
  check('错误令牌上传被拒', r.status === 401, `status=${r.status}`);

  // 4) 管理端令牌列表 → 含 uid
  r = await req('GET', '/api/recordings', VIEWER_TOKEN, null, 'application/json');
  j = null; try { j = JSON.parse(r.body.toString()); } catch {}
  const recs = j?.recordings || {};
  const files = recs[UID] || [];
  check('管理端列表含刚上传录制', r.status === 200 && files.some((f) => f.name === uploadedFname),
    `status=${r.status} files=${files.length}`);

  // 5) 静态下载 → 200 video/mp4
  if (uploadedFname) {
    r = await req('GET', `/${encodeURIComponent('recordings')}/${UID}/${uploadedFname}`, null);
    check('静态下载 mp4', r.status === 200 && String(r.headers['content-type'] || '').startsWith('video/mp4'),
      `status=${r.status} type=${r.headers['content-type']} bytes=${r.body.length}`);
  } else {
    check('静态下载 mp4', false, '无 fname 无法下载');
  }

  // 6) 清理：删掉本测试建的 recordings/<uid>（只删自己的）
  try {
    fs.rmSync(path.join(repo, 'recordings', UID), { recursive: true, force: true });
    check('测试数据自清理', true, `删 ${UID}`);
  } catch (e) {
    check('测试数据自清理', false, e.message);
  }

  console.log(`\n\u2550\u2550 \u7ed3\u679c: ${pass} \u901a\u8fc7 / ${fail} \u5931\u8d25 \u2550\u2550`);
  process.exit(fail === 0 ? 0 : 1);
})();