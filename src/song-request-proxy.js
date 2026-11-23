// 星集控 · 班级点歌代发（2026-10-18）
// ─────────────────────────────────────────────────────────────────────────────
// 为什么由云端代发（用户拍板 A1：被控端各自绑定一个账号，点歌跟随账号同步）：
//   被控端只能"请求点歌"，不能"直接点"——Key 不下发到端上，
//   所有点歌都必须经过云端的权限校验与频控，再由云端用该班的 Key 发往 VoiceHub。
//
// 上游接口（已存在，无需改 VoiceHub）：
//   POST https://<voicehub>/api/open/songs/request
//   header: x-api-key: vhub_xxx        （scope: songs:request）
//   body:   { title, artist, cover?, ... }   —— title/artist 必填 1..200
//
// ⚠️ 只读契约的反面：这是本项目**唯一**会写 VoiceHub 的地方，且：
//   · 只调 /api/open/songs/request（点歌申请），**绝不**调 /broadcast/queue、
//     /broadcast/authority、/broadcast/listeners 这些播控接口；
//   · 点歌进来是"**申请**"，由 VoiceHub 的音乐管理员审核/排期 —— 我们不替人做决定。
//
// ⚠️ 日志红线：**绝不打印 Key**；错误信息里若带上游回显，也要先脱敏。

const DEFAULT_BASE = 'https://voicehub.245959623.xyz';
const TIMEOUT_MS = 15_000;

/** 把上游错误文本里的疑似 key 抹掉（vhub_xxx 或 32+ 位十六进制）。 */
function scrub(s) {
  return String(s || '')
    .replace(/vhub_[A-Za-z0-9_-]{6,}/g, 'vhub_***')
    .replace(/\b[A-Fa-f0-9]{32,}\b/g, '***');
}

/**
 * 代发一条点歌申请。
 * @param {object} o
 * @param {string} o.baseUrl       VoiceHub 基址
 * @param {string} o.apiKey        该班的 Key（**只在内存里，不进日志**）
 * @param {string} o.title
 * @param {string} o.artist
 * @param {string} [o.cover]
 * @param {object} [o.extra]       其它允许字段（musicPlatform/musicId/playUrl/submissionNote…）
 * @returns {Promise<{ok:boolean, code?:number, error?:string, data?:any}>}
 */
export async function requestSongViaVoiceHub(o = {}) {
  const base = String(o.baseUrl || process.env.VOICEHUB_BASE_URL || DEFAULT_BASE).replace(/\/+$/, '');
  const key = String(o.apiKey || '').trim();
  const title = String(o.title || '').trim();
  const artist = String(o.artist || '').trim();

  // 本地先校验：上游也是 1..200，这里提前拦掉能省一次外网请求，且错误信息更友好。
  if (!key) return { ok: false, error: '该班级没有配置 VoiceHub 点歌凭据', code: 0 };
  if (!title || title.length > 200) return { ok: false, error: '歌曲名称不能为空且不超过 200 字符', code: 0 };
  if (!artist || artist.length > 200) return { ok: false, error: '艺术家不能为空且不超过 200 字符', code: 0 };
  if (o.cover && !/^https?:\/\//i.test(String(o.cover))) {
    return { ok: false, error: '封面地址必须以 http(s):// 开头', code: 0 };
  }

  // 上游只认白名单字段，这里只拼我们确认支持的几个，避免把垃圾字段发过去。
  const body = { title, artist };
  if (o.cover) body.cover = String(o.cover).trim();
  const extra = o.extra && typeof o.extra === 'object' ? o.extra : {};
  for (const f of ['musicPlatform', 'musicId', 'playUrl', 'submissionNote', 'preferredPlayTimeId', 'cardCode']) {
    if (extra[f] !== undefined && extra[f] !== null && String(extra[f]).trim() !== '') {
      body[f] = String(extra[f]).trim();
    }
  }

  const ctrl = new AbortController();
  const to = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  try {
    const r = await fetch(`${base}/api/open/songs/request`, {
      method: 'POST',
      signal: ctrl.signal,
      headers: {
        'content-type': 'application/json',
        // ⚠️ 这行是唯一把 Key 送出去的地方，且只送往 VoiceHub。
        'x-api-key': key,
        accept: 'application/json',
      },
      body: JSON.stringify(body),
    });

    const text = await r.text();
    let data = null;
    try { data = text ? JSON.parse(text) : null; } catch { /* 非 JSON 就保留原文片段供排障 */ }

    if (!r.ok) {
      // 上游可能回显请求内容；统一脱敏，绝不把 Key 漏进日志/回执。
      const msg = (data && (data.message || data.error)) || (text || '').slice(0, 200) || `HTTP ${r.status}`;
      return { ok: false, code: r.status, error: scrub(msg) };
    }
    return { ok: true, code: r.status, data };
  } catch (e) {
    const aborted = e && e.name === 'AbortError';
    return { ok: false, code: 0, error: aborted ? 'VoiceHub 请求超时' : scrub(e && e.message) };
  } finally {
    clearTimeout(to);
  }
}
