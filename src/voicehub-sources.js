// 星集控 · 外部广播站（VoiceHub）「正在播放 / 排期」主备源单一真源（2026-11-07）
// ─────────────────────────────────────────────────────────────────────────────
// 为什么要单独一个模块：
//   · 正在播放桥（voicehub-bridge.js）、点歌代发（song-request-proxy.js）、
//     排期读代理（broadcast-club.js）三处都要「外部广播站地址」，以前各自硬编码
//     `https://voicehub.245959623.xyz`（2026-10-22 Vercel 欠费停用 → 404 / 402），
//     三份必然漂移。抽成一处 = 改一次三处同时生效。
//
// 用户裁决（m13419「联动一下广播站」→ m13469 四问）：
//   · vh_primary = 「netlify 为主 + music 为备」 ⇒ 正在播放桥按此顺序 fallback；
//   · schedule_source = 「新源优先 + 8799 回退」 ⇒ 排期读把本机写代理地址放最后；
//   · prod_rollout = 「改代码 + 隔离实例验证，生产单独申请」 ⇒ 本模块只定默认值，
//     生产 .env 的 VOICEHUB_BASE_URL 覆盖与 8788 重启另起一轮，不在本批。
//
// 两源 2026-11-07 只读实测（GET /api/music/broadcast 均 200 且契约合规）：
//   · voicehub-2026.netlify.app  → {"broadcast":null,"serverTime":...}
//   · music.245959623.xyz        → {"broadcast":null,"serverTime":...}
//   两源 /api/songs/public 均返回空数组 []；本机 127.0.0.1:8799 有真实排期。

// 默认主源：Netlify 部署（ai-docs/memory/2026-11-01.md:107 记载的迁移目标）。
export const PRIMARY_BASE = 'https://voicehub-2026.netlify.app';
// 默认备源：Cloudflare 同 zone 的 music 子域（与已停用的 voicehub. 同 IP，但路由不同）。
export const FALLBACK_BASE = 'https://music.245959623.xyz';
// 冻结：外部模块只读，不许 push/splice 改默认值。
export const DEFAULT_BASES = Object.freeze([PRIMARY_BASE, FALLBACK_BASE]);

/**
 * 解析候选 base 列表。优先级（高 → 低）：
 *   1. opts.baseUrls（数组，隔离测试 / 显式指定）
 *   2. opts.baseUrl（单源字符串，兼容既有单测与旧调用方）
 *   3. env（逗号分隔，生产 VOICEHUB_BASE_URL 覆盖）
 *   4. DEFAULT_BASES（netlify → music）
 * 每个 base 去尾斜杠、去重、去空；全空时退回 [PRIMARY_BASE]（永不返回空数组，
 * 否则桥会「一次都不拉」却表现成 unknown）。
 * @param {{baseUrls?: string[], baseUrl?: string}} [opts]
 * @param {string} [env]
 * @returns {string[]}
 */
export function resolveBases(opts = {}, env) {
  const cands = Array.isArray(opts && opts.baseUrls)
    ? opts.baseUrls
    : (opts && opts.baseUrl ? [opts.baseUrl]
      : (env ? String(env).split(',').map((s) => s.trim()).filter(Boolean)
        : DEFAULT_BASES));
  const seen = new Set();
  const out = [];
  for (const s of cands) {
    const b = String(s == null ? '' : s).trim().replace(/\/+$/, '');
    if (b && !seen.has(b)) { seen.add(b); out.push(b); }
  }
  return out.length ? out : [PRIMARY_BASE];
}
