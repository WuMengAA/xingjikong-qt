// 云端 OTA 版本清单（2026-10-05 · OTA 本期落地）
//
// 设计取舍：
//   · 云端**只做"最新版本是多少"的权威声明**，不亲自托管安装包大文件——
//     安装包由站点/对象存储托管（定位书：网页 8090 承载 OTA 资源），清单里给 URL + sha256。
//   · 清单是磁盘上的 ota.json，**每次请求现读**，运维改完即生效、不必重启云端。
//   · 没有 ota.json 时不是错误：latestFor 返回 null，接口如实回 {latest:null}——
//     运维一眼看出"还没发布过版本"，而不是把"没配"当成"已是最新"。
//
// ota.json 形态（products 里每个产品一条）：
// {
//   "products": {
//     "agent":  { "version":"0.5.0", "url":"https://host/ota/stelarith-agent-setup-0.5.0.exe",
//                 "sha256":"<64 hex>", "size":17208133, "notes":"...", "mandatory":false },
//     "viewer": { ... }
//   }
// }
//
// 更新通道（2026-11-16 用户裁决「合并更新渠道 / 取消灰度」）：
//   ota.json 是唯一对外清单；任何 alpha/gray/beta/rc/自定义 channel 都归到 stable。
//   构建注册表 ota-builds.json 仍保留历史审计字段，但分发时**不再按档位等待**：
//   所有设备拿同一份「最新版本」，不再出现 5 小时 / 24 小时的可见性差。
//   旧 ota-<channel>.json 文件只作为历史遗留文件保留，运行时不再读取。
//   路径穿越防护仍保留：channel 永远不参与文件路径拼接。

import { readFileSync, existsSync } from 'node:fs';
import path from 'node:path';
import { OTA_MANIFEST_FILE } from './config.js';
import { loadBuilds, hasBuilds, recordBuild } from './ota-promote.js';

/** 单一渠道判据：所有请求都归到 stable，旧名只用于兼容日志与审计。 */
export const OTA_CHANNELS_UNIFIED = true;
export function unifiedChannel() { return 'stable'; }

/**
 * 通道 → 清单文件路径（2026-10-20 抽为单一真源）。
 *
 * 为什么抽出来：读方（loadManifest/latestFor）与写方（POST /api/admin/ota 管理台发布）
 * 此前各拼各的文件名 —— 管理端只写 ota.json，灰度清单 ota-gray.json 只能运维手工编辑。
 * 两边一旦对不上（发到 A 文件、设备读 B 文件），表现是"发布成功、设备不升级"的
 * **静默错位**，比报错难查得多。现在两边都走这一个函数。
 *
 *   'stable' / 空 / 不合规 → OTA_MANIFEST_FILE（ota.json）
 *   灰度通道（gray 等）    → **同目录**的 ota-<channel>.json
 *
 * ⚠️ 白名单在这里兜底（2026-10-13 三端审计修复点）：channel 来自 URL query 与设备
 *    register 载荷，曾直接拼进文件名 ⇒ 传 `channel=../secret` 就能读任意 *.json
 *    （路径穿越）。不合规一律回落 stable 清单，**绝不**把 ../ 之类拼进文件名。
 */
export function manifestFileFor(channel) {
  // 2026-11-16 取消灰度：channel 只用于日志/兼容，**绝不拼进文件名**。
  // 这是比旧白名单更强的形态：alpha/gray/beta/rc/../secret 都落回同一个稳定清单。
  void channel;
  return OTA_MANIFEST_FILE;
}

/** 单渠道下从构建注册表挑最新：按版本序优先，发布时间只作同版本/兜底排序。 */
export function pickUnifiedBuild(reg, product, now = Date.now()) {
  const builds = (reg && reg.builds) || {};
  let best = null;
  let bestAt = -Infinity;
  for (const rec of Object.values(builds)) {
    if (!rec || rec.product !== product) continue;
    if (!parseVersion(rec.version)) continue;
    const raw = Date.parse(rec.publishedAt);
    const at = Number.isFinite(raw) ? raw : -Infinity;
    if (!best) { best = rec; bestAt = at; continue; }
    const cmp = cmpVersion(rec.version, best.version);
    if (cmp > 0 || (cmp === 0 && at > bestAt)) { best = rec; bestAt = at; }
  }
  return best;
}

/** 读取并解析清单；文件不存在/坏 JSON → null（调用方据此说"没发布过版本"）。
 *  @param {string} [channel] 更新通道：'stable'（默认）读 ota.json；其它值（如 'gray'）
 *    读同目录的 ota-<channel>.json（灰度清单）。灰度清单缺失即回落 null（设备当"无更新"），
 *    不会去读 stable 清单 —— 灰度必须是显式发布的，避免把 stable 误当成灰度推下去。 */
function loadManifest(channel) {
  // ⚠️ 2026-10-13 三端审计修复：channel 来自 URL query 与设备 register 载荷，此前**直接拼进文件名**
  //    ⇒ 传 `channel=../secret` 就能读任意 *.json（路径穿越；虽限 .json，仍足以泄露配置）。
  //    在**唯一真源** manifestFileFor() 处白名单收口（所有调用点自动受益）：不合规一律回落 stable 清单。
  const file = manifestFileFor(channel);
  try {
    const raw = readFileSync(file, 'utf8');
    const obj = JSON.parse(raw);
    if (!obj || typeof obj !== 'object') return null;
    return obj;
  } catch (e) {
    // 首次部署时文件不存在是正常态，不打 error 刷屏；只在"文件在但坏了"时才值得注意。
    if (e && e.code === 'ENOENT') return null;
    console.error(`[cloud] FAIL 读取 OTA 清单失败（${file}）：${e.message}`);
    return null;
  }
}

/* ── 版本比较（2026-10-08 加）──────────────────────────────────────────
 * 为什么放在云端：管理端/被控端都要回答"要不要升级"，而这个判断如果两端各写一遍
 * C++ 实现，就会重演 0.6.23 之前那个静默 bug（core 打平后「正式版 < 候选版」，
 * 结果是云端发了正式版、客户端判成"已是最新"、教室机永远不升级）。
 * 现在**只有这一份实现**，客户端带 `?current=` 来问，直接拿 hasUpdate。
 *
 * 命名规范（docs/版本号命名规范-2026-10-07.md）：
 *   X.Y.Z          正式版
 *   X.Y.Z-rc.N     候选（N ≥ 1）
 *   X.Y.Z-alpha.N  内测（N ≥ 1）
 * 阶段序：alpha(0) < rc(1) < 正式(2)。
 */
const STAGE_RANK = { alpha: 0, rc: 1, '': 2 };

/** 解析版本串；不合规范 → null（调用方按"没法比"处理，不抛）。 */
export function parseVersion(s) {
  const m = /^(\d+)\.(\d+)\.(\d+)(?:-(alpha|rc)\.([1-9]\d*))?$/.exec(String(s || '').trim());
  if (!m) return null;
  return {
    core: [Number(m[1]), Number(m[2]), Number(m[3])],
    stage: m[4] || '',
    n: m[5] ? Number(m[5]) : 0,
  };
}

/**
 * 比较两个版本号：a < b → -1，相等 → 0，a > b → 1。
 * **任何一侧解析不出来一律返回 0**（"不比"）：宁可不说有新版，也不能因为一个
 * 格式没认出来就把升级推给全网 —— 静默错比漏提示危险得多。
 */
export function cmpVersion(a, b) {
  const A = parseVersion(a);
  const B = parseVersion(b);
  if (!A || !B) return 0;
  for (let i = 0; i < 3; i++) {
    if (A.core[i] !== B.core[i]) return A.core[i] < B.core[i] ? -1 : 1;
  }
  const ra = STAGE_RANK[A.stage];
  const rb = STAGE_RANK[B.stage];
  if (ra !== rb) return ra < rb ? -1 : 1;
  if (A.n !== B.n) return A.n < B.n ? -1 : 1;
  return 0;
}

/** 某产品的最新版本条目；没有清单/没有该产品 → null。
 *
 * 2026-11-02 渠道自动晋升（用户点单）：**构建不变、只变渠道可见性**。
 * 判定顺序：
 *   ① 构建注册表 ota-builds.json 里若有该产品的登记构建 → 按「发布时间 + 渠道档位」
 *      算此刻该渠道能拿到哪个构建（alpha 立即可见 / beta 满 5h / stable 满 24h）。
 *   ② 注册表没有该产品（老部署、从未走过新发布链）→ 回落旧逻辑：读 ota-<channel>.json。
 * 两条路返回同一形状，调用方无从分辨 —— 平滑升级，不需要一次性数据迁移。
 *
 * @param {string} product  产品名（'agent' / 'viewer'）
 * @param {string} [channel] 设备渠道（alpha/gray/beta/rc/stable/空/未知）
 *  @param {number} [now]   当前毫秒时间戳（注入以便单测；省略即 Date.now()） */
export function latestFor(product, channel, now = Date.now()) {
  // ① 新链路：构建注册表。取消灰度后**不再看时间晋升/档位**，只拿版本最新的一份。
  const reg = loadBuilds(OTA_MANIFEST_FILE);
  if (hasBuilds(reg, product)) {
    const pick = pickUnifiedBuild(reg, product, now);
    if (pick) {
      return {
        version: String(pick.version || ''),
        url: String(pick.url || ''),
        sha256: String(pick.sha256 || ''),
        size: Number(pick.size || 0),
        notes: String(pick.notes || ''),
        mandatory: !!pick.mandatory,
        // 附带给客户端/管理台看的信息，不影响既有字段消费方
        channel: unifiedChannel(),
        publishedAt: String(pick.publishedAt || ''),
        appearAfterMs: 0,
        unifiedChannels: OTA_CHANNELS_UNIFIED,
      };
    }
    return null;
  }
  // ② 兼容链路：只有一份 stable 清单；channel 参数在 manifestFileFor() 已被归掉。
  const m = loadManifest(unifiedChannel());
  if (!m || !m.products || typeof m.products !== 'object') return null;
  const p = m.products[product];
  if (!p || typeof p !== 'object') return null;
  return {
    version: String(p.version || ''),
    url: String(p.url || ''),
    sha256: String(p.sha256 || ''),
    size: Number(p.size || 0),
    notes: String(p.notes || ''),
    mandatory: !!p.mandatory,
  };
}

/** 发布时登记构建（供 publish-release.mjs 与管理台发布端点调用）。
 *  幂等：同一 version+sha 重复发布不重置 publishedAt（否则永远晋升不了）。
 *  @param {string} releasedTier 本次是**显式发布到哪一档**（stable/rc/gray…）。
 *    传了就意味着「人工干预」：该档立刻可见，不再等 5h/24h 晋升窗口。
 *    不传 = 只登记计时起点（纯自动晋升语义），保持旧调用方的行为不变。 */
export function registerBuild(product, entry, now = Date.now(), releasedTier = '') {
  return recordBuild(OTA_MANIFEST_FILE, product, entry, now, releasedTier);
}

/** 某渠道此刻的晋升进度（管理台展示「还有多久进 beta/stable」用）。 */
export function promotionStatus(product, channel, now = Date.now()) {
  const reg = loadBuilds(OTA_MANIFEST_FILE);
  const builds = Object.values((reg && reg.builds) || {})
    .filter((r) => r && r.product === product)
    .sort((a, b) => Date.parse(b.publishedAt) - Date.parse(a.publishedAt));
  const latest = builds[0] || null;
  if (!latest) return null;
  const at = Date.parse(latest.publishedAt);
  const ageMs = now - at;
  return {
    version: latest.version,
    publishedAt: latest.publishedAt,
    ageMs,
    // 历史字段保留给旧管理端解析；单渠道下三档同视，不再分批。
    unifiedChannels: OTA_CHANNELS_UNIFIED,
    channel: unifiedChannel(),
    inAlpha: true,
    inBeta: true,
    inStable: true,
    betaAt: new Date(at).toISOString(),
    stableAt: new Date(at).toISOString(),
  };
}

/**
 * OTA 资产健康检查（2026-10-31 加 · 404 事故闭环）。
 * 云端启动时跑一次：遍历唯一正式清单里每个产品的 url，
 * 只对「指向本机资产目录」的 url 核对本地文件存在性 —— 资产被清/漏发会立刻告警，
 * 而不是等教室机报"更新失败"。
 *
 * ⚠️ 为什么区分本地/外部：本轮 OTA 现代化把资产迁移到了 GitHub Release（gh 代理），
 * 清单 url 变成外部地址，本地 assets/pkg 有文件反而是旧的。所以只查**本地指向**的
 * url（路径含 /assets/），外部 url 认为"由外部托管"、跳过 —— 不误报。
 *
 * @returns {{missing: Array<{product:string, channel:string, version:string, url:string}>, checked: number}}
 */
export function assetHealthCheck() {
  const missing = [];
  let checked = 0;
  // 取消灰度后只扫唯一清单：历史 ota-<channel>.json 不再影响启动自检。
  const channels = [''];

  for (const ch of channels) {
    const m = loadManifest(ch);
    if (!m || !m.products) continue;
    for (const [product, e] of Object.entries(m.products)) {
      if (!e || typeof e !== 'object' || !e.url) continue;
      const url = String(e.url);
      // 只校验指向本地 /assets/ 的 url（attr: 资产在 assets/pkg/ 下）
      const am = /\/assets\/[^?#]*/.exec(url.split('?')[0]);
      if (!am) continue;
      const rel = am[0].replace(/^\/assets\//, '');
      const file = path.join(path.dirname(OTA_MANIFEST_FILE), 'assets', rel.split('/').join(path.sep));
      checked++;
      if (!existsSync(file)) {
        missing.push({ product, channel: ch || 'stable', version: String(e.version || ''), url });
      }
    }
  }
  return { missing, checked };
}
