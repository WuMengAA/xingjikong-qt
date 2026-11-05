// 祝福弹幕敏感词表（2026-11-05 · 只读下发，被控端启动/定期拉取）。
//
// 边界（与 interact.js 的分工，别造第二套真源）：
//   · 「学生投稿 → 云端落待审 → 管理端审批 → 可信广播」的审核闭环早已落地在
//     interact.js:submitBlessing 与 index.js 的 /api/interact/*、broadcastBlessingDanmaku；
//     本模块**不重复造审核端点**，只提供一份可热更新的补充词表。
//   · 被控端始终保留本机三词兜底（danmaku_wordlist.h 的 localWords），云端词表
//     **只增不减**：本文件坏/空/不可达都削弱不了兜底，最坏只是没新增词。
//   · 端点是公开只读（放在 isManageApi 之前）：被控端只有设备令牌、发 HTTP 不带
//     Authorization，挂到 /api/interact/* 下会被管理面鉴权 401 拒掉。
//   · 词表不含任何凭据；sanitize 是防御性收敛（防手抖把整个字典塞进来）。
//
// 真源：同目录 danmaku-words.json。管理员改那份即可，改完被控端 30 分钟内自动生效。

import { readFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
export const DANMAKU_WORDS_PATH = '/api/danmaku/words';   // 与 index.js 薄壳共用，路径只此一处
export const MAX_WORDS = 200;
export const MAX_WORD_CHARS = 64;

/** 收敛任意输入为干净的字符串数组：去非串/去空白/截断/去重/限量。 */
export function sanitizeWords(input) {
  const arr = Array.isArray(input) ? input : [];
  const out = [];
  const seen = new Set();
  for (const w of arr) {
    if (typeof w !== 'string') continue;
    const s = w.trim().slice(0, MAX_WORD_CHARS);
    if (!s || seen.has(s)) continue;
    seen.add(s);
    out.push(s);
    if (out.length >= MAX_WORDS) break;
  }
  return out;
}

/** 读真源；文件缺失/损坏一律回落空表（= 只用本机兜底），绝不抛异常打断请求。 */
export function loadDanmakuWords() {
  const f = join(HERE, 'danmaku-words.json');
  if (!existsSync(f)) return { ok: true, words: [], updatedAt: null };
  try {
    const j = JSON.parse(readFileSync(f, 'utf8'));
    return {
      ok: true,
      words: sanitizeWords(j && j.words),
      updatedAt: (j && typeof j.updatedAt === 'string') ? j.updatedAt : null,
    };
  } catch (e) {
    return { ok: true, words: [], updatedAt: null };
  }
}

/** index.js 薄壳用：一次性给出可直接交给 send(200, ...) 的响应体。 */
export function danmakuWordsPayload() {
  const w = loadDanmakuWords();
  return { ok: true, words: w.words, updatedAt: w.updatedAt, cacheSeconds: 30 };
}
