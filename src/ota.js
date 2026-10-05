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

import { readFileSync } from 'node:fs';
import { OTA_MANIFEST_FILE } from './config.js';

/** 读取并解析清单；文件不存在/坏 JSON → null（调用方据此说"没发布过版本"）。 */
function loadManifest() {
  try {
    const raw = readFileSync(OTA_MANIFEST_FILE, 'utf8');
    const obj = JSON.parse(raw);
    if (!obj || typeof obj !== 'object') return null;
    return obj;
  } catch (e) {
    // 首次部署时文件不存在是正常态，不打 error 刷屏；只在"文件在但坏了"时才值得注意。
    if (e && e.code === 'ENOENT') return null;
    console.error(`[cloud] FAIL 读取 OTA 清单失败（${OTA_MANIFEST_FILE}）：${e.message}`);
    return null;
  }
}

/** 某产品的最新版本条目；没有清单/没有该产品 → null。 */
export function latestFor(product) {
  const m = loadManifest();
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
