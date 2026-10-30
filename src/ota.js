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
// （2026-10-20：管理台 POST /api/admin/ota 也支持带 channel 直接写灰度清单，
//  走 manifestFileFor() 同一真源，不再只能运维手工编辑文件。）

import { readFileSync, existsSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { OTA_MANIFEST_FILE } from './config.js';

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
  const ch = String(channel || '').trim();
  if (ch === '' || ch === 'stable') return OTA_MANIFEST_FILE;
  if (!/^[a-z0-9_-]{1,16}$/.test(ch)) return OTA_MANIFEST_FILE;
  return path.join(path.dirname(OTA_MANIFEST_FILE), `ota-${ch}.json`);
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

/**
 * OTA 资产健康检查（2026-10-31 加 · 404 事故闭环）。
 * 云端启动时跑一次：遍历 stable + 全部 ota-<channel>.json 清单里每个产品的 url，
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
  const channels = [''];
  // 扫同目录的 ota-<channel>.json（stable 之外的灰度/候选通道）
  try {
    const dir = path.dirname(OTA_MANIFEST_FILE);
    for (const f of readdirSync(dir)) {
      const m = /^ota-([a-z0-9_-]{1,16})\.json$/.exec(f);
      if (m) channels.push(m[1]);
    }
  } catch { /* 读目录失败就当只有 stable */ }

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
