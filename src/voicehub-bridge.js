// 星集控 · VoiceHub 广播状态桥（2026-10-18）
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

const DEFAULT_BASE = 'https://voicehub.245959623.xyz';
// 轮询间隔：30 秒。进度靠本地外推，视觉上仍然每秒在走，所以不需要更勤。
// （契约也把轮询列为 SSE 之外的兜底节奏；Vercel 上 SSE 流式响应会被截断，故不依赖 SSE。）
const POLL_MS = 30_000;
// 无新快照的容忍上限（服务端 90s + 5s 余量）
const STALE_MS = 95_000;
// 请求超时：VoiceHub 在冷启动时可能十几秒才回（实测过一次 20s 超时），
// 给 12s 让它先失败，别把整条轮询链拖住。
const FETCH_TIMEOUT_MS = 12_000;

/**
 * 广播状态桥。
 * 用法：
 *   const bridge = createVoiceHubBridge({ onUpdate: (state) => broadcastToAgents(state) });
 *   bridge.start();
 */
export function createVoiceHubBridge(opts = {}) {
  const base = (opts.baseUrl || process.env.VOICEHUB_BASE_URL || DEFAULT_BASE).replace(/\/+$/, '');
  const onUpdate = typeof opts.onUpdate === 'function' ? opts.onUpdate : () => {};
  const log = typeof opts.log === 'function' ? opts.log : () => {};

  // 内部状态：snapshot 是"服务端最后一次给的快照"，anchor 用于外推
  let snapshot = null;          // 原始快照（可能为 null = 没有广播）
  let anchorAt = 0;             // 收到快照的本地时刻（ms）
  let lastOkAt = 0;             // 最后一次成功拿到响应（含 broadcast:null）
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

  /** 拉一次快照。失败不清空上一条（避免大屏/岛一闪一闪），只记错。 */
  async function pollOnce() {
    if (inflight) return;
    inflight = true;
    const ctrl = new AbortController();
    const to = setTimeout(() => ctrl.abort(), FETCH_TIMEOUT_MS);
    try {
      const r = await fetch(`${base}/api/music/broadcast`, {
        signal: ctrl.signal,
        headers: { accept: 'application/json' },
      });
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const j = await r.json();
      const b = j && j.broadcast ? j.broadcast : null;
      // 只在"拿到有效响应"时更新锚点：position 是响应那一刻的权威值
      snapshot = b;
      anchorAt = Date.now();
      lastOkAt = anchorAt;
      lastErr = null;
      onUpdate(current());
    } catch (e) {
      lastErr = String(e && e.message ? e.message : e);
      // ⚠️ 关键：失败**不动 snapshot**。契约要求"解析失败保留上一次有效状态"，
      //    否则教室机会因为一次抖动把正在播的标记闪掉。
      log(`[voicehub] 拉取失败：${lastErr}（保留上一次状态）`);
      onUpdate(current());
    } finally {
      clearTimeout(to);
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
      log(`[voicehub] 已启动：${base} 轮询 ${POLL_MS / 1000}s，过期阈值 ${STALE_MS / 1000}s`);
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
    /** 诊断快照：给 /api/voicehub/status 之类用 */
    diagnostics() {
      return {
        baseUrl: base,
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
