// 画面帧治理（2026-11-13 · 四单点①「无人监控仍传画面 / 链路稳定性」）
//
// 这一层只做**云端能独立裁决**的事，不碰被控端（control-qt/src/main.cpp 此刻是他线 WIP，
// 且 check-main-size 已红 4441>台账 4399、pre-commit 会拦，AGENTS §0/§2 不许碰）。
//
// ── 查到的事实（不是推演）──────────────────────────────────────────
//   · registry.js:918 autoSubscribeViewers(uid) 给**每个 auto=true 的管端**补订每台新上线的设备；
//     而 pinViewer() 只在管端**手动**发 subscribe 时把 auto 置 false（index.js:2688）。
//     ⇒ 一个从没挑过设备的管端永远 auto=true，40 台设备依次上线就会攒出 40 条订阅。
//   · pushWatchToAgent() 按「subs.has(uid) 的管端数」算 watchers ⇒ 那 40 台全被判「有人看」，
//     全部持续抓屏上传；broadcastFrame 又按订阅把 40 路帧全中继给同一个管端（它只显示 1 台）。
//     events.log 实测 88 条「已把观看结论推给设备」watchers:0，就是这条链的另一端。
//   · 帧洪流的代价不只是带宽：被控端 captureAndSend 的注释记着 2026-10-18 实测
//     ~1.2MB/s 上行把 10s 一条的心跳挤到 28s 后到 ⇒ 云端 30s 判超时踢人 ⇒ 当日 342 次重连。
//     砍掉幽灵订阅 = 砍掉多余帧 = 给心跳让路，这就是「链路稳定性」的因果链。
//
// ── 行业同构（一手来源）──────────────────────────────────────────
//   · 背压 / 尽力而为：registry.js 既有 relayBlocked(8MB) 已确立「丢新帧保连接」语义，本层沿用。
//   · ftop README「A reading the hardware does not give is shown as unavailable, never as zero」：
//     没人看就是「没有观看者」，不写「0 路帧」假装零流量；idleVerdict 只说有几台空转。
//   · RFC 7231 §6.5.8 的冲突思路在这里反过来用：订阅是**资源当前状态**，累加前先看状态，
//     不许无条件 add 出一个和界面实际不符的状态。
//
// 纪律：纯函数、零 IO、可单测；所有 env 默认值都从函数参数注入，测试不依赖进程环境。

/** 一个「从没挑过设备」的管端最多自动订阅几台。1 = 只补第一台（index.js:2453 的原始意图）。 */
export const AUTO_SUBSCRIBE_MAX = 1;
/** 单台设备的帧最多中继给几个管端，防 N×M 扇出把云端出口打满。 */
export const RELAY_FANOUT_MAX = 8;

const envInt = (name, def) => {
  const v = Number(process.env[name]);
  return Number.isFinite(v) && v > 0 ? Math.floor(v) : def;
};
export const autoMax = () => envInt('CLOUD_AUTO_SUBSCRIBE_MAX', AUTO_SUBSCRIBE_MAX);
export const fanoutMax = () => envInt('CLOUD_RELAY_FANOUT_MAX', RELAY_FANOUT_MAX);

/**
 * 该管端要不要自动补订这台设备。
 * 判据只有两条：还没被 pin（auto=true）、且当前订阅数没到上限。
 * 不判 uid 是否已存在也安全 —— Set.add 幂等，且上限拦住的是「第 2 台」不是「同一台」。
 */
export function shouldAutoSubscribe(viewer, uid, max = autoMax()) {
  if (!viewer || viewer.auto !== true) return false;
  if (!(viewer.subs instanceof Set)) return false;   // 结构不完整的 viewer 一律不补，宁可少订不攒幽灵
  return viewer.subs.size < max;
}

/**
 * 真正在看这台设备的管端数（只数活连接、只数订阅了它的）。
 * 与 pushWatchToAgent 原先的手写循环同语义，抽出来是为了可断言、不再两处各写一遍。
 */
export function watchersOf(viewers, uid) {
  const key = String(uid || '');
  let n = 0;
  for (const [ws, v] of viewers) {
    if (ws.readyState !== 1) continue;
    if (!v.subs || !v.subs.has(key)) continue;
    n++;
  }
  return n;
}

/** 帧扇出上限：超过就把 hits 夹住，别让一台设备喂饱整排管端。 */
export function relayBudget(hits, cap = fanoutMax()) { return Math.max(0, Math.min(hits, cap)); }

/**
 * 空转结论：几台设备在没人看的情况下仍占着链路。
 * 没有设备时说「还没有」，不写「0 台空转」（0 与没读数不是一回事）。
 */
export function idleVerdict(deviceCount, idleCount) {
  const n = Number(deviceCount);
  if (!Number.isFinite(n) || n <= 0) return '还没有设备';
  const i = Math.max(0, Math.min(Number(idleCount) || 0, n));
  return i === 0 ? n + ' 台设备都有观看者' : i + ' / ' + n + ' 台设备没人看，仍在占链路';
}

/** 诊断快照：给 devices×viewers 算一遍谁空转，供 /api 或事件聚合用（纯函数）。 */
export function idleSnapshot(uids, viewers) {
  const idle = [];
  for (const u of uids) if (watchersOf(viewers, u) === 0) idle.push(String(u));
  return { deviceCount: uids.length, idle, verdict: idleVerdict(uids.length, idle.length) };
}

/** 上次给每台设备下发的「帧推送开关」结论，只在**变化**时下发，不刷指令队列。 */
const gateState = new Map();   // uid -> enabled(boolean)

/**
 * watchers 变化 → 用既有 instruction 通道让设备停/启抓屏（四单点①的最后一环）。
 *
 * 为什么走 instruction 而不是新造一个 type：被控端 control-qt/src/main.cpp 此刻是他线 WIP
 * 且 check-main-size 已红、pre-commit 会拦（AGENTS §0/§2），**不许碰**；但干净的
 * src/action/execute_media.cpp 能加一条 frame_push 指令，翻转 main.cpp:3062 每帧都读的
 * STE_ENABLE_WS_FRAME_PUSH 环境变量 —— 同进程 qputenv 即刻生效，不重启、不改脏文件。
 *
 * 三条纪律：
 *   · 只在结论 0↔非0 变化时发（订阅变更不算频繁，但设备上下线会反复触发）。
 *   · sendInstruction 返回非 sent（设备离线）时**不记状态**，等它上线由 markConnected
 *     末尾的 pushWatchToAgent 重算再发 —— 否则会永久停在「发过了」的假状态上。
 *   · 老版本被控端不认这条指令会回 failed；这里不假装治好了，由设备侧如实回执。
 *
 * @returns {{sent:boolean, enabled:boolean, changed:boolean}}
 */
export function pushFrameGate(uid, watchers, sendInstructionFn) {
  const key = String(uid || '').trim();
  const enabled = watchers > 0;
  const prev = gateState.get(key);
  if (prev === enabled) return { sent: false, enabled, changed: false };
  if (typeof sendInstructionFn !== 'function') return { sent: false, enabled, changed: true };
  const r = sendInstructionFn(key, 'frame_push', { enabled });
  const sent = !!r && r.state === 'sent';
  if (sent) gateState.set(key, enabled);   // 离线不记，等上线重算
  return { sent, enabled, changed: true };
}

/** 测试/诊断用：清掉状态，保证单测可重复（不进生产路径）。 */
export function resetGateState(uid) {
  if (uid === undefined) gateState.clear();      // 无参 = 全清（单测用）
  else gateState.delete(String(uid).trim());     // ★ 设备上线必调：端侧 qputenv 不落盘，
                                                 //   重启后运行期开关回落默认「开」，
                                                 //   云端若还记着「已停」就永不重发 = 永久空转。
}
