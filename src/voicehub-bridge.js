// 星集控 · VoiceHub 广播状态桥（2026-10-18，2026-11-07 多源主备）
// ─────────────────────────────────────────────────────────────────────────────
// 目的：把 VoiceHub（校园点歌/广播站）的「正在播放」状态接进星集控，
//       让教室被控端的灵动岛能显示「谁在播、播到哪、下一首、多少人在听」。
//
// 为什么放在**云端**而不是让被控端直连 VoiceHub：
//   · 60 台教室机各连一次外部服务 = 60 条长连接 + 60 路外网流量，教室断网就全瞎；
//   · VoiceHub 只该被服务一次，缓存后所有设备共享同一份真相；
//   · 断网时教室仍能显示云端最后一次有效状态（比"什么都没有"更有用）。
//
// 数据源（公开只读，无需鉴权 —— 契约见 Stelarith-voicehub-component/AI-必读-广播正在播放-桥接提示词.md）：
//   GET /api/music/broadcast  →  { broadcast: <快照|null>, serverTime }
//   快照字段：songId/title/artist/duration/position/isPlaying/publisherName/
//             listenerCount/nextUp/queueRemaining/updatedAt/sessionStartedAt
//
// ⚠️ 三个必须守住的口径（都来自契约，不是拍脑袋）：
//   1. **进度本地外推**：服务端 position 已按服务器时间外推到「响应那一刻」，
//      客户端记锚点后本地按秒加。**绝不每秒发请求**（会把 VoiceHub 打爆）。
//   2. **过期判定 95 秒**：isPlaying 且超过 95s 没有新快照 ⇒ 视为已停播，撤掉标记。
//      服务端自己的 PLAYING_STALE_MS 是 90s，客户端留 5s 余量。
//   3. **只读**：绝不调 POST /api/music/broadcast{,/queue,/authority,/listeners}。
//
// ⚠️ 一个已知的语义盲区（读源码确认，别当 bug 查）：
//   broadcast.get.ts 在 authority.enabled === false 时**直接返回 broadcast:null**，
//   所以"总开关关着"和"没人播"在 API 层面**不可区分**，一律按"没有广播"处理。
//
// 2026-11-07 多源主备（用户 m13419「联动一下广播站」→ m13469 裁决）：
//   · 旧实现把 base 硬编码成 https://voicehub.245959623.xyz（2026-10-22 Vercel 欠费
//     停用 → 404/402），生产桥因此恒 unknown、岛「正在播放」永远不亮。
//   · 现在基址统一由 voicehub-sources.js 的 resolveBases() 给出：
//     默认 netlify 主 → music 备；env VOICEHUB_BASE_URL（逗号分隔）可覆盖；
//     单测/隔离实例用 opts.baseUrl / opts.baseUrls 显式指定。
//   · fallback 只在**拉不到**（网络错/非 200/JSON 坏）时发生；主源一旦返回
//     合法响应（含 broadcast:null = 权威「没在播」）即为权威，**不继续打备源**
//     —— 否则主源说停播、备源有旧快照时会被误判成"还在播"。
import { resolveBases } from './voicehub-sources.js';

// 轮询间隔：30 秒。进度靠本地外推，视觉上仍然每秒在走，所以不需要更勤。
// （契约也把轮询列为 SSE 之外的兜底节奏；Vercel 上 SSE 流式响应会被截断，故不依赖 SSE。）
const POLL_MS = 30_000;
// 无新快照的容忍上限（服务端 90s + 5s 余量）
const STALE_MS = 95_000;
// 单次请求超时：VoiceHub 在冷启动时可能十几秒才回（实测过一次 20s 超时），
// 给 12s 让它先失败，别把整条轮询链拖住。多源时每个源各用一份这个预算。
const FETCH_TIMEOUT_MS = 12_000;

/**
 * 广播状态桥。
 * 用法：
 *   const bridge = createVoiceHubBridge({ onUpdate: (state) => broadcastToAgents(state) });
 *   bridge.start();
 */
export function createVoiceHubBridge(opts = {}) {
  const bases = resolveBases(opts, process.env.VOICEHUB_BASE_URL);
  const onUpdate = typeof opts.onUpdate === 'function' ? opts.onUpdate : () => {};
  const log = typeof opts.log === 'function' ? opts.log : () => {};

  // 内部状态：snapshot 是"服务端最后一次给的快照"，anchor 用于外推
  let snapshot = null;          // 原始快照（可能为 null = 没有广播）
  let anchorAt = 0;             // 收到快照的本地时刻（ms）
  let lastOkAt = 0;             // 最后一次成功拿到响应（含 broadcast:null）
  let activeBase = null;        // 最后一次成功用的是哪个源（诊断用；null = 从未成功）
  let lastErr = null;
  let timer = null;
  let inflight = false;         // 防重入：上一轮还没回来就跳过这一轮
  let started = false;

  /** 当前对外可见的状态（含本地外推后的 position）——给被控端/管理端用 */
  function current() {
    const now = Date.now();
    // 从未拿到过任何响应：明确"未知"，而不是伪装成"没有广播"
    if (!lastOkAt) return { status: 'unknown', broadcast: null };
    // 过期判定：有快照、在播、且太久没刷新 ⇒ 视为停播
    if (snapshot && snapshot.isPlaying && now - lastOkAt > STALE_MS) {
      return { status: 'stale', broadcast: null };
    }
    if (!snapshot) return { status: 'idle', broadcast: null };

    // 进度外推：服务端 position 是"响应那一刻"的值，之后按墙上钟累加
    const el = (now - anchorAt) / 1000;
    const duration = Number(snapshot.duration) || 0;
    const position = Math.max(0, Math.min(
      duration > 0 ? duration : Number.MAX_SAFE_INTEGER,
      Number(snapshot.position || 0) + (snapshot.isPlaying ? el : 0)
    ));

    return {
      status: 'playing',
      broadcast: {
        songId: snapshot.songId ?? null,
        scheduleId: snapshot.scheduleId ?? null,
        title: snapshot.title || '',
        artist: snapshot.artist || '',
        cover: snapshot.cover || null,
        duration,
        position,
        // progress 供被控端直接画进度条：0..1；duration 未知时给 -1（不画）
        progress: duration > 0 ? Math.max(0, Math.min(1, position / duration)) : -1,
        isPlaying: !!snapshot.isPlaying,
        publisherName: snapshot.publisherName || null,
        // ⚠️ publisherName 只用于显示，**绝不用于任何鉴权判断**（契约 §8 红线）
        listenerCount: Number(snapshot.listenerCount) || 0,
        queueRemaining: Number(snapshot.queueRemaining) || 0,
        nextUp: snapshot.nextUp
          ? {
              songId: snapshot.nextUp.songId ?? null,
              title: snapshot.nextUp.title || '',
              artist: snapshot.nextUp.artist || '',
              duration: Number(snapshot.nextUp.duration) || 0,
            }
          : null,
        updatedAt: Number(snapshot.updatedAt) || 0,
      },
    };
  }

  /**
   * 向**单个**源拉一次快照。
   * 成功（含 broadcast:null）返回 { broadcast }；拉不到（网络/非 200/坏 JSON）抛错。
   * 把"合法空"与"失败"分开，fallback 才不会把 authoritative null 当成断线。
   */
  async function fetchBroadcast(base) {
    const ctrl = new AbortController();
    const to = setTimeout(() => ctrl.abort(), FETCH_TIMEOUT_MS);
    try {
      const r = await fetch(`${base}/api/music/broadcast`, {
        signal: ctrl.signal,
        headers: { accept: 'application/json' },
      });
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const j = await r.json();
      return { broadcast: j && j.broadcast ? j.broadcast : null };
    } finally {
      clearTimeout(to);
    }
  }

  /** 拉一次快照：按 bases 顺序试，第一个成功（含 null）即权威。失败不清空上一条。 */
  async function pollOnce() {
    if (inflight) return;
    inflight = true;
    let got = null;               // { broadcast, base } 或 null（全失败）
    const errors = [];
    try {
      for (const base of bases) {
        try {
          const r = await fetchBroadcast(base);
          got = { broadcast: r.broadcast, base };
          break;                  // 合法响应即权威；broadcast:null 也算，不继续打备源
        } catch (e) {
          errors.push(`${base}: ${String(e && e.message ? e.message : e)}`);
        }
      }
      if (got) {
        snapshot = got.broadcast;
        anchorAt = Date.now();
        lastOkAt = anchorAt;
        activeBase = got.base;
        lastErr = null;
        onUpdate(current());
      } else {
        lastErr = errors.join(' | ') || 'no source';
        // ⚠️ 关键：失败**不动 snapshot / anchorAt / lastOkAt**。契约要求"解析失败保留上一次
        //    有效状态"，否则教室机会因为一次抖动把正在播的标记闪掉；95s 后由 current()
        //    自然判 stale。onUpdate 仍发一次 current()（内容没变会被上层 sig 去重）。
        log(`[voicehub] 所有源拉取失败：${lastErr}（保留上一次状态）`);
        onUpdate(current());
      }
    } finally {
      inflight = false;
    }
  }

  return {
    start() {
      if (started) return false;
      started = true;
      pollOnce();                                  // 立刻拉一次，不等 30s
      timer = setInterval(pollOnce, POLL_MS);
      timer.unref?.();                             // 别因为这个定时器挡住进程退出
      log(`[voicehub] 已启动：${bases.join(' → ')} 轮询 ${POLL_MS / 1000}s，过期阈值 ${STALE_MS / 1000}s`);
      return true;
    },
    stop() {
      if (timer) clearInterval(timer);
      timer = null;
      started = false;
      log('[voicehub] 已停止');
    },
    /** 供 HTTP 端点/诊断用 */
    current,
    /** 诊断快照：给 /api/public/voicehub 之类用 */
    diagnostics() {
      return {
        bases,
        baseUrl: bases[0],
        activeBase,
        pollMs: POLL_MS,
        staleMs: STALE_MS,
        started,
        hasSnapshot: !!snapshot,
        lastOkAt,
        lastOkAgoMs: lastOkAt ? Date.now() - lastOkAt : null,
        lastError: lastErr,
        state: current(),
      };
    },
  };
}
