// 语音对讲 P1 出口带宽实测（2026-10-21）
//
// 目的：把「640B 裸 PCM/帧 → ~40B Opus 包/帧」换算成**云端出口真实字节**，
//   并顺手量出 v1 JSON 帧头的固定开销（它决定 Opus 压缩有多少被头吃掉）。
//
// 方法：1 台管理端推 50 帧/秒 × 3 秒；N 台假被控端只收。统计各接收端收到的
//   二进制总字节 → 单台 kbps 与 N 台合计 Mbps。
//
// 用法：node tests/bench-voice-egress.cjs [port] [devices=60]
// 只对隔离实例跑。

const path = require('path');
const repo = path.resolve(__dirname, '..');
const WebSocket = require(path.join(repo, 'node_modules/ws/index.js')).default
  || require(path.join(repo, 'node_modules/ws/index.js'));

const PORT = Number(process.argv[2] || 18788);
const N = Number(process.argv[3] || 60);
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'test-viewer-token';
const FPS = 50, SECONDS = 3;

function makeFrame(mime, seq, payload) {
  const header = Buffer.from(JSON.stringify({
    v: 1, type: 'frame', uid: 'bench', seq, ts: Date.now(), mime, bytes: payload.length,
  }), 'utf8');
  const out = Buffer.alloc(3 + header.length + payload.length);
  out[0] = 1;
  out[1] = (header.length >> 8) & 0xFF;
  out[2] = header.length & 0xFF;
  header.copy(out, 3);
  payload.copy(out, 3 + header.length);
  return { buf: out, headerLen: header.length };
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const agents = [];
  const stats = [];
  for (let i = 0; i < N; i++) {
    const uid = `BENCH${String(i).padStart(2, '0')}`;
    const st = { uid, bytes: 0, frames: 0 };
    stats.push(st);
    const ws = new WebSocket(`ws://${HOST}:${PORT}/ws/agent`);
    ws.on('open', () => ws.send(JSON.stringify({
      v: 1, type: 'register', id: 'r',
      payload: { uid, token: AGENT_TOKEN, fp: 'bench', version: '9.9.9', actions: ['notify'] },
    })));
    ws.on('message', (raw, isBinary) => {
      if (isBinary) { st.bytes += raw.length; st.frames += 1; }
    });
    agents.push(ws);
  }
  await sleep(1500);

  const viewer = new WebSocket(`ws://${HOST}:${PORT}/ws/viewer`);
  const vMsgs = [];
  viewer.on('message', (raw) => { try { vMsgs.push(JSON.parse(raw.toString())); } catch {} });
  await new Promise((r) => viewer.once('open', r));
  viewer.send(JSON.stringify({ v: 1, type: 'auth', id: 'a', payload: { token: VIEWER_TOKEN } }));
  await sleep(800);
  viewer.send(JSON.stringify({ v: 1, type: 'audio.start', id: 's', payload: { duplex: true } }));
  await sleep(800);

  async function bench(label, mime, payloadSize) {
    stats.forEach((s) => { s.bytes = 0; s.frames = 0; });
    const payload = Buffer.alloc(payloadSize, 0x5A);
    let headerLen = 0;
    const t0 = Date.now();
    const total = FPS * SECONDS;
    for (let i = 0; i < total; i++) {
      const f = makeFrame(mime, 1000 + i, payload);
      headerLen = f.headerLen;
      viewer.send(f.buf, { binary: true });
      await sleep(1000 / FPS);
    }
    await sleep(600);
    const dur = (Date.now() - t0) / 1000;
    const perDev = stats.reduce((a, s) => a + s.bytes, 0) / N / dur;   // B/s per device
    const aggMbps = (perDev * N * 8) / 1e6;
    console.log(`\n[${label}] mime=${mime} payload=${payloadSize}B header=${headerLen}B`);
    console.log(`  单帧线上字节 ≈ ${payloadSize + headerLen + 3}B（含 3B 前导）`);
    console.log(`  单台下行 ≈ ${((perDev * 8) / 1000).toFixed(1)} kbps`);
    console.log(`  ${N} 台合计 ≈ ${aggMbps.toFixed(2)} Mbps`);
    console.log(`  帧数（单台）= ${stats[0].frames}`);
    return { perDevKbps: (perDev * 8) / 1000, aggMbps, headerLen };
  }

  const pcm = await bench('裸 PCM（改造前）', 'audio/pcm', 640);
  const opus = await bench('Opus 16kbps（改造后）', 'audio/opus', 40);
  const hdrRatio = (pcm.headerLen / (640 + pcm.headerLen + 3)) * 100;

  console.log('\n══ 结论 ══');
  console.log(`  出口下降：${pcm.aggMbps.toFixed(2)} → ${opus.aggMbps.toFixed(2)} Mbps `
    + `（${(pcm.aggMbps / opus.aggMbps).toFixed(2)}×，降幅 ${(100 - (opus.aggMbps / pcm.aggMbps) * 100).toFixed(1)}%）`);
  console.log(`  JSON 帧头在 PCM 帧里占 ${hdrRatio.toFixed(1)}%，在 Opus 帧里占 `
    + `${((opus.headerLen / (40 + opus.headerLen + 3)) * 100).toFixed(1)}%（下一步紧凑头要吃掉的就是这块）`);

  agents.forEach((w) => w.close());
  viewer.close();
  process.exit(0);
})();
