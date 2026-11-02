// 渠道自动晋升（2026-11-02 · 用户点单「构建不变，只变渠道」）
//
// 语义（用户原话）：
//   「灰度渠道（标识 alpha）实时跟进最新构建，今日最新更新 5 小时后自动切换到测试渠道（beta）。
//    1 天后（自灰度开始）切换到正式发行渠道（渠道名不变）。如果没有人工干预，那就是客户端显示的
//    版本渠道更新，版本号不变，只是渠道状态改变；可以选择保持此渠道进行该渠道的持续更新；
//    注意构建不变只是渠道变化的涵义。」
//
// ⇔ 工程含义：
//   · **构建（version/url/sha256/notes/size）一个字都不改**，只是"哪些渠道的设备现在能拿到它"。
//   · alpha 渠道：构建一发布就可见（age ≥ 0）。
//   · beta  渠道：构建发布满 5 小时后可见。
//   · stable 渠道：构建发布满 24 小时后可见。
//
// 为什么**读时计算**、不写调度器：
//   调度器要定时器 + 进程内状态 + 进程重启就丢，还得处理"重启期间到点没晋升"。而晋升规则
//   本身是**纯函数**（发布时间 ∨ 现在时刻），读时算一遍即可 —— 无状态、重启无副作用、
//   回溯可复现（给个固定 now 就能断言任何时刻的行为，单测才好写）。
//
// 为什么**不覆盖 ota-*.json**：
//   alpha→beta→stable 若靠"把条目拷进下一个清单"，就会出现同一构建在三个文件里各存一份，
//   哪天手改一份就对不上（历史坑：读方写方各拼文件名导致"发布成功、设备不升级"）。
//   注册表 ota-builds.json 只记"这个构建什么时候第一次给 alpha 看"，渠道可见性由时间算，
//   **永远只有一份构建事实**。
//
// 兼容性（旧版本适配）：老设备 register 报的 channel 可能是 'gray'/'stable'/空。规则：
//   · 'stable' / 空 → stable 规则（最保守，绝不让灰度构建提前打到全校）
//   · 'gray' / 'alpha' → alpha 规则（gray 与 alpha 视为同一档，后者是用户新命名）
//   · 'beta' / 'rc'    → beta 规则（同理，rc 是旧叫法）
//   · 其它未知渠道名 → 回落 stable 规则，**不**当成 alpha（未知渠道不该拿到最新未验证构建）

import { createHash } from 'node:crypto';
import { existsSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import path from 'node:path';

/** 构建发布后多久进 beta（5 小时，用户点单） */
export const PROMOTE_BETA_MS = 5 * 60 * 60 * 1000;
/** 自 alpha 发布算起多久进 stable（24 小时，用户点单） */
export const PROMOTE_STABLE_MS = 24 * 60 * 60 * 1000;

/** 渠道名归一：把别名收敛到 alpha / beta / stable 三档。未知渠道 → stable（保守）。 */
export function normalizeChannelName(channel) {
  const ch = String(channel || '').trim().toLowerCase();
  if (ch === 'alpha' || ch === 'gray') return 'alpha';
  if (ch === 'beta' || ch === 'rc') return 'beta';
  return 'stable';
}

/** 某档渠道在某构建年龄下是否可见。
 *  @param {boolean} grandfathered 存量豁免：规则上线**之前**就已直发 stable 的构建。
 *    这类构建没有 alpha 起点可算，若按新规则回溯会造成"线上已发布的版本突然从 stable 消失"
 *    （全网教室机以为没版本、管理台显示未发布），属不可接受的回归。故对它们一律全渠道可见，
 *    并保留 publishedAt 真实时间以便审计。新构建不带此标记，必须走时间晋升。
 *  @param {string[]|null} manualTiers **人工干预**：该构建被显式发布到过哪些档。
 *    用户原话「如果没有人工干预，那就是……渠道状态改变」—— 反过来说，人一旦显式把某个包
 *    发到 stable，就不该再被 24 小时窗口挡在门外（否则"我发的正式版为什么设备看不到"，
 *    现场唯一解释是等明天，等于把一次人工操作变成一次静默失败）。
 *    注意：只对**被显式发过的那一档**生效，不会顺带解锁其它档 —— 发 stable 不等于发 beta。 */
export function visibleInChannel(tier, ageMs, grandfathered = false, manualTiers = null) {
  if (grandfathered) return true;
  const t = normalizeChannelName(tier);
  if (Array.isArray(manualTiers) && manualTiers.includes(t)) return true;
  switch (t) {
    case 'alpha': return true;
    case 'beta': return ageMs >= PROMOTE_BETA_MS;
    default: return ageMs >= PROMOTE_STABLE_MS;
  }
}

/** 渠道档位 → 门槛毫秒（给客户端/文档展示用，避免三处硬编码 5h/24h）。 */
export function thresholdFor(tier) {
  switch (normalizeChannelName(tier)) {
    case 'alpha': return 0;
    case 'beta': return PROMOTE_BETA_MS;
    default: return PROMOTE_STABLE_MS;
  }
}

function buildsFile(manifestFile) {
  return path.join(path.dirname(manifestFile), 'ota-builds.json');
}

/** 读构建注册表；不存在/坏 JSON → 空表（不抛，调用方回落旧清单逻辑）。 */
export function loadBuilds(manifestFile) {
  const file = buildsFile(manifestFile);
  if (!existsSync(file)) return { builds: {} };
  try {
    const obj = JSON.parse(readFileSync(file, 'utf8'));
    if (!obj || typeof obj !== 'object') return { builds: {} };
    if (!obj.builds || typeof obj.builds !== 'object') obj.builds = {};
    return obj;
  } catch (e) {
    // 坏表不静默清空：喊出来，然后按空表回落（宁可多推一次 stable，也不要把发布彻底卡死）
    console.error(`[cloud] FAIL 构建注册表损坏（${file}）：${e.message} —— 本次按空表回落旧清单逻辑`);
    return { builds: {} };
  }
}

/** 构建指纹：同一份产物（版本+sha）无论从哪个渠道发布都算同一个构建，不重复计时。 */
export function buildKey(product, entry) {
  const sha = String(entry && entry.sha256 || '').trim().toLowerCase();
  const ver = String(entry && entry.version || '').trim();
  if (sha) return `${product}:${ver}:${sha.slice(0, 16)}`;
  return `${product}:${ver}:${createHash('sha256').update(String(entry && entry.url || '')).digest('hex').slice(0, 16)}`;
}

/**
 * 记录一个构建（幂等）：已存在则**保留首次发布时间**（这是晋升计时的起点，
 * 重复发布会把它往后推 = 永远晋升不了，是这条链最阴的坑）。
 * @returns {{entry:object, created:boolean}}
 */
export function recordBuild(manifestFile, product, entry, now = Date.now(), releasedTier = '') {
  const reg = loadBuilds(manifestFile);
  const key = buildKey(product, entry);
  const prev = reg.builds[key];
  const tier = releasedTier ? normalizeChannelName(releasedTier) : '';
  if (prev && prev.publishedAt) {
    // 已有：只更新可变字段（notes/mandatory 允许运维改公告），publishedAt 绝不动
    prev.notes = String(entry.notes || prev.notes || '');
    prev.mandatory = entry.mandatory == null ? prev.mandatory : !!entry.mandatory;
    prev.url = String(entry.url || prev.url || '');
    prev.size = Number(entry.size || prev.size || 0);
    // 人工干预记录：同一构建每被显式发布到一个新档就记下（合并去重，不清旧值）
    if (tier) {
      const prevTiers = Array.isArray(prev.manualTiers) ? prev.manualTiers : [];
      prev.manualTiers = Array.from(new Set([...prevTiers, tier]));
    }
    writeRegistry(manifestFile, reg);
    return { entry: prev, created: false };
  }
  const rec = {
    product: String(product),
    version: String(entry.version || ''),
    url: String(entry.url || ''),
    sha256: String(entry.sha256 || ''),
    size: Number(entry.size || 0),
    notes: String(entry.notes || ''),
    mandatory: !!entry.mandatory,
    publishedAt: new Date(now).toISOString(),
  };
  // 首次登记就带人工档位（例如直接发 stable），同样立即对该档可见
  if (tier) rec.manualTiers = [tier];
  reg.builds[key] = rec;
  writeRegistry(manifestFile, reg);
  return { entry: rec, created: true };
}

function writeRegistry(manifestFile, reg) {
  const file = buildsFile(manifestFile);
  reg.generatedBy = 'src/ota-promote.js';
  reg.generatedAt = new Date().toISOString();
  const text = JSON.stringify(reg, null, 2) + '\n';
  const tmp = file + '.tmp';
  writeFileSync(tmp, Buffer.from(text, 'utf8'));
  renameSync(tmp, file); // 原子替换：写一半崩掉不会留下坏表
}

/**
 * 某渠道此刻应看到的构建（纯函数，无 IO）。
 * @param {object} reg        loadBuilds() 结果
 * @param {string} product    产品名
 * @param {string} channel    设备渠道（alpha/gray/beta/rc/stable/空/未知）
 * @param {number} now        当前毫秒时间戳（注入以便单测任意时刻）
 * @returns {object|null}     命中的构建记录（含 version/url/sha256/notes/mandatory/publishedAt）
 */
export function pickBuildFor(reg, product, channel, now = Date.now()) {
  const builds = (reg && reg.builds) || {};
  const tier = normalizeChannelName(channel);
  let best = null;
  let bestAt = -Infinity;
  for (const rec of Object.values(builds)) {
    if (!rec || rec.product !== product) continue;
    const gf = rec.grandfathered === true;
    const raw = Date.parse(rec.publishedAt);
    const at = Number.isFinite(raw) ? raw : -Infinity;
    // 时间不可解析的**新**构建直接跳过（宁可不推，也不要把时间戳坏掉的条目当成已催熟）
    if (!Number.isFinite(raw) && !gf) continue;
    if (!visibleInChannel(tier, now - at, gf, rec.manualTiers)) continue;
    if (at > bestAt) { best = rec; bestAt = at; }
  }
  return best;
}

/** 该产品是否已有任何登记构建（无则调用方应回落旧 ota-*.json 逻辑）。 */
export function hasBuilds(reg, product) {
  const builds = (reg && reg.builds) || {};
  return Object.values(builds).some((r) => r && r.product === product);
}
