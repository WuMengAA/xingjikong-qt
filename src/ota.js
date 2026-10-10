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
// 更新通道（2026-10-10 加）：除 ota.json（stable）外，可存在 ota-<channel>.json
// （如 ota-gray.json），形态完全一致。latestFor(product, channel) 读对应通道清单；
// 通道清单缺失即回落 null（该通道"无更新"），不会去读 stable。灰度发布 = 把候选版本
// 发到 ota-gray.json，只有 register 带 channel=gray 的设备拿得到，不惊动全网。

import { readFileSync } from 'node:fs';
import { OTA_MANIFEST_FILE } from './config.js';

/** 读取并解析清单；文件不存在/坏 JSON → null（调用方据此说"没发布过版本"）。
 *  @param {string} [channel] 更新通道：'stable'（默认）读 ota.json；其它值（如 'gray'）
 *    读同目录的 ota-<channel>.json（灰度清单）。灰度清单缺失即回落 null（设备当"无更新"），
 *    不会去读 stable 清单 —— 灰度必须是显式发布的，避免把 stable 误当成灰度推下去。 */
function loadManifest(channel) {
  const file = (channel && channel !== 'stable') ? `ota-${channel}.json` : OTA_MANIFEST_FILE;
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
 *  @param {string} product  产品名（'agent' / 'viewer'）
 *  @param {string} [channel] 更新通道（'stable' 读 ota.json；'gray' 读 ota-gray.json 等） */
export function latestFor(product, channel) {
  const m = loadManifest(channel);
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
