// 互动数据链路回归测试（2026-10-15 固化 · 灵动岛扩展组件6）
//
// 作用：验证 学生上报 → 云端落库/限额 → 管理端查询 → 审批 → 广播弹幕 的完整链路：
//   agent 上报 song_request / feedback / blessing
//     → ack + viewer 收到 *-new 实时广播（待审队列不用轮询）
//   viewer 查询 song-requests.list / blessings.list / feedback.summary
//   viewer 审批 blessing.handle approve → 该班在线设备收到 notify kind=danmaku（走组件5 可信弹幕路径）
//   状态机只进不退（重复 handle → BAD_STATE）、限额闸门、坏载荷如实回错
//
// 用法（先起隔离云端，再联测；限额调小以便快速打到闸门）：
//   CLOUD_WS_PORT=8096 CLOUD_WS_HOST=127.0.0.1 \
//   CLOUD_INTERACT_DB_FILE=/tmp/c6/interact.db CLOUD_QUEUE_DB_FILE=/tmp/c6/iq.db \
//   CLOUD_INTERACT_DANMAKU_PER_HOUR=2 node src/index.js
//   node tests/regression-interact.cjs 8096
// 返回码：0 全过；非 0 有失败。

const path = require('path');
const repo = path.resolve(__dirname, '..');
const WebSocket = require(path.join(repo, 'node_modules/ws/index.js')).default
  || require(path.join(repo, 'node_modules/ws/index.js'));

const PORT = Number(process.argv[2] || 8096);
const UID = String(process.argv[3] || 'IT' + Math.floor(Math.random() * 900 + 100));
const HOST = '127.0.0.1';
const AGENT_TOKEN = process.env.CLOUD_WS_TOKEN || 'dev-cloud-token';
const VIEWER_TOKEN = process.env.CLOUD_VIEWER_TOKEN || 'dev-cloud-token';
// 第 4 个参数 = 隔离云端实际生效的每小时祝福上限（脚本自身读不到云端 env，需显式对齐）
const DANMAKU_PER_HOUR = Number(process.argv[4] || process.env.CLOUD_INTERACT_DANMAKU_PER_HOUR || 20);
const BLESSING_TEXT = '测试祝福：祝大家上课开心';

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  \u2705 ${name}${detail ? ' \u2014 ' + detail : ''}`); }
  else { fail++; console.log(`  \u274c ${name}${detail ? ' \u2014 ' + detail : ''}`); }
}
function waitFor(fn, timeoutMs = 6000) {
  return new Promise((resolve) => {
    const t0 = Date.now();
    const iv = setInterval(() => {
      const v = fn();
      if (v) { clearInterval(iv); resolve(v); }
      else if (Date.now() - t0 > timeoutMs) { clearInterval(iv); resolve(null); }
    }, 60);
  });
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  // ── 被控端 ──
  const agentMsgs = [];
  const agent = new WebSocket(`ws://${HOST}:${PORT}/ws/agent`);
  let agentSeq = 0;
  const agentSend = (type, payload) => agent.send(JSON.stringify({ v: 1, type, id: 'a' + (++agentSeq), payload }));
  agent.on('open', () => {
    agent.send(JSON.stringify({
      v: 1, type: 'register', id: 'a0',
      payload: { uid: UID, token: AGENT_TOKEN, fp: 'it-test', version: '9.9.9',
        actions: ['notify'], caps: { screen: true, input: true, power: true, actions: ['notify'] } },
    }));
  });
  agent.on('message', (raw, isBinary) => {
    if (isBinary) return;
    try { agentMsgs.push(JSON.parse(raw.toString())); } catch { /* 非 JSON 忽略 */ }
  });

  // ── 管理端 ──
  const viewerMsgs = [];
  const viewer = new WebSocket(`ws://${HOST}:${PORT}/ws/viewer`);
  let viewerSeq = 0;
  const viewerSend = (type, payload) => viewer.send(JSON.stringify({ v: 1, type, id: 'v' + (++viewerSeq), payload }));
  viewer.on('open', () => viewer.send(JSON.stringify({ v: 1, type: 'auth', id: 'v0', payload: { token: VIEWER_TOKEN } })));
  viewer.on('message', (raw) => { try { viewerMsgs.push(JSON.parse(raw.toString())); } catch { /* 忽略 */ } });

  // 1) 两端就绪
  const reg = await waitFor(() => agentMsgs.find((m) => m.type === 'registered' || m.type === 'registration-ok'));
  check('被控端注册成功', !!reg, UID);
  const authed = await waitFor(() => viewerMsgs.find((m) => m.type === 'viewer-ready' || m.type === 'authed'));
  check('管理端鉴权通过', !!authed);

  // 2) 点歌上报 → ack + 管理端实时收到 song-request-new
  agentSend('song_request', { title: '晴天', artist: '周杰伦', source: 'search' });
  const songAck = await waitFor(() => agentMsgs.find((m) => m.type === 'song-request-ack'));
  check('点歌 ack ok', !!songAck && songAck.payload?.ok === true, JSON.stringify(songAck?.payload));
  const songNew = await waitFor(() => viewerMsgs.find((m) => m.type === 'song-request-new'));
  check('管理端实时收到 song-request-new', !!songNew, `id=${songNew?.payload?.id}`);
  const songId = songNew?.payload?.id;

  // 3) 点赞上报 → ack + feedback-new
  agentSend('feedback', { kind: 'like', value: 1 });
  const fbAck = await waitFor(() => agentMsgs.find((m) => m.type === 'feedback-ack'));
  check('点赞 ack ok', !!fbAck && fbAck.payload?.ok === true, JSON.stringify(fbAck?.payload));
  const fbNew = await waitFor(() => viewerMsgs.find((m) => m.type === 'feedback-new'));
  check('管理端实时收到 feedback-new', !!fbNew);

  // 4) 祝福上报（限额内 2 条）→ ack + blessing-new
  agentSend('blessing', { text: BLESSING_TEXT });
  const bl1 = await waitFor(() => agentMsgs.find((m) => m.type === 'blessing-ack' && m.payload?.ok === true));
  check('祝福 #1 ack ok（限额内）', !!bl1, `id=${bl1?.payload?.id} state=${bl1?.payload?.state}`);
  check('祝福落库态为 pending（等人工审批）', bl1?.payload?.state === 'pending');
  const blNew = await waitFor(() => viewerMsgs.find((m) => m.type === 'blessing-new'));
  check('管理端实时收到 blessing-new', !!blNew, `id=${blNew?.payload?.id}`);
  const blessingId = blNew?.payload?.id;

  agentMsgs.length = 0;
  agentSend('blessing', { text: '测试祝福：第二条' });
  const bl2 = await waitFor(() => agentMsgs.find((m) => m.type === 'blessing-ack'));
  check('祝福 #2 ack ok（仍是限额内）', !!bl2 && bl2.payload?.ok === true);

  // 5) 限额闸门：第 3 条（每小时上限按环境变量调成 2）应被如实拒收
  agentMsgs.length = 0;
  agentSend('blessing', { text: '测试祝福：第三条应被限额拦下' });
  const bl3 = await waitFor(() => agentMsgs.find((m) => m.type === 'blessing-ack'));
  const limited = bl3?.payload?.ok === false && bl3?.payload?.code === 'RATE_LIMITED';
  if (DANMAKU_PER_HOUR <= 2) {
    check('超限额第 3 条被拒（RATE_LIMITED，如实回错不静默）', limited, JSON.stringify(bl3?.payload));
  } else {
    check('未压低限额（跳过限额断言）', true, `perHour=${DANMAKU_PER_HOUR}`);
  }

  // 6) 坏载荷：空祝福被如实回错
  agentMsgs.length = 0;
  agentSend('blessing', { text: '   ' });
  const blBad = await waitFor(() => agentMsgs.find((m) => m.type === 'blessing-ack'));
  check('空祝福被拒（BAD_PAYLOAD）', blBad?.payload?.ok === false && blBad?.payload?.code === 'BAD_PAYLOAD',
    JSON.stringify(blBad?.payload));

  // 7) 管理端查询：点歌待审队列
  viewerMsgs.length = 0;
  viewerSend('song-requests.list', { state: 'pending' });
  const songList = await waitFor(() => viewerMsgs.find((m) => m.type === 'song-requests.list'));
  check('查询点歌待审队列', !!songList && songList.payload?.ok === true && songList.payload.items.length >= 1,
    `条数=${songList?.payload?.items?.length}`);

  // 8) 管理端查询：祝福待审队列
  viewerMsgs.length = 0;
  viewerSend('blessings.list', { state: 'pending' });
  const blList = await waitFor(() => viewerMsgs.find((m) => m.type === 'blessings.list'));
  check('查询祝福待审队列', !!blList && blList.payload?.ok === true && blList.payload.items.length >= 1,
    `条数=${blList?.payload?.items?.length}`);

  // 9) 管理端查询：反馈汇总
  viewerMsgs.length = 0;
  viewerSend('feedback.summary', {});
  const fbSum = await waitFor(() => viewerMsgs.find((m) => m.type === 'feedback.summary'));
  check('查询反馈汇总（likeTotal≥1）', !!fbSum && fbSum.payload?.likeTotal >= 1,
    `likeTotal=${fbSum?.payload?.likeTotal} likeToday=${fbSum?.payload?.likeToday}`);

  // 10) 审批祝福 approve → 该班在线设备收到 notify kind=danmaku（组件5 可信弹幕路径）
  agentMsgs.length = 0;
  viewerMsgs.length = 0;
  viewerSend('blessing.handle', { id: blessingId, action: 'approve' });
  const blHandled = await waitFor(() => viewerMsgs.find((m) => m.type === 'blessing-handled'));
  check('祝福审批 approve 成功', !!blHandled && blHandled.payload?.ok === true,
    `state=${blHandled?.payload?.item?.state}`);
  // ⚠️ 云端下发统一走指令信封：type='instruction' + payload.action='notify'
  //    （见 registry.sendInstruction → wrapOutgoing(true,'instruction',{action,params})），
  //    不是扁平 type='notify' —— 断言必须按信封结构找。
  const danmakuNotify = await waitFor(() =>
    agentMsgs.find((m) => m.type === 'instruction' && m.payload?.action === 'notify'
      && JSON.stringify(m).includes(BLESSING_TEXT)));
  check('审批后设备收到 instruction/notify（kind=danmaku 含祝福原文）', !!danmakuNotify,
    danmakuNotify ? `kind=${danmakuNotify.payload?.params?.kind} content=${danmakuNotify.payload?.params?.content}` : '未收到');

  // 11) 状态机只进不退：重复处理同一条 → BAD_STATE
  viewerMsgs.length = 0;
  viewerSend('blessing.handle', { id: blessingId, action: 'reject' });
  const blAgain = await waitFor(() => viewerMsgs.find((m) => m.type === 'blessing-handled'));
  check('重复处理被拒（BAD_STATE，只进不退）',
    blAgain?.payload?.ok === false && blAgain?.payload?.code === 'BAD_STATE', JSON.stringify(blAgain?.payload));

  // 12) 点歌审批 approve（不广播弹幕，只回 handled）
  viewerMsgs.length = 0;
  viewerSend('song-request.handle', { id: songId, action: 'approve' });
  const songHandled = await waitFor(() => viewerMsgs.find((m) => m.type === 'song-request-handled'));
  check('点歌审批 approve 成功（state=approved）',
    songHandled?.payload?.ok === true && songHandled?.payload?.item?.state === 'approved',
    `state=${songHandled?.payload?.item?.state}`);

  // 13) 不存在的 id 如实回 NOT_FOUND
  viewerMsgs.length = 0;
  viewerSend('blessing.handle', { id: 999999, action: 'approve' });
  const notFound = await waitFor(() => viewerMsgs.find((m) => m.type === 'blessing-handled'));
  check('不存在 id 如实回 NOT_FOUND', notFound?.payload?.code === 'NOT_FOUND', JSON.stringify(notFound?.payload));

  await sleep(200);
  agent.close(); viewer.close();
  console.log(`\n\u2550\u2550 \u7ed3\u679c: ${pass} \u901a\u8fc7 / ${fail} \u5931\u8d25 \u2550\u2550`);
  process.exit(fail === 0 ? 0 : 1);
})();
