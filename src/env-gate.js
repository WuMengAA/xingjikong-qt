// 环境白名单（Environment Whitelist）—— 谁是测试机，云端说了算（2026-10-24）
//
// ── 背景 ────────────────────────────────────────────────────────────────
// 用户诉求（2026-10-24）：「内网只存在于本地调试中，非 test 相关设备就为生产设备
// （学校设备）」。拍板选用**云端白名单**：
//   · 测试设备 = 云端维护的一份 uid 清单；
//   · 不在清单里 = 生产设备，一律按学校设备对待，不享受任何调试特权。
//
// 信任模型（与 knownDevices 班级归属同一纪律）：**设备自报的 env 一律忽略**。
// 否则任何一台学校机器在 register 里塞 env:"test" 就能伪装成测试机，
// 而测试特权的典型形态（内网直连、宽松上传、诊断开关）全是白名单给的。
// 清单在云端、改权限也在云端 —— 伪装者改不到清单。
//
// 存储：JSON 文件（写入即持久）。低频变更（管理员加/删测试机），
// 不需要 sqlite 的重型能力；与 repairs.json / club-members.json 同一族。
// 写文件失败必须回 false（内存里有的文件里没有 = 重启即丢，是假状态）。

import fs from 'node:fs';
import { TEST_WHITELIST_FILE } from './config.js';

let entries = new Map();     // uid -> true（Set 的 JSON 数组化麻烦，Map 更直接）
let file = TEST_WHITELIST_FILE;
let loaded = false;

/** 打开白名单（幂等）。重启后从文件恢复 —— 测试机身份不能因重启被抹成生产机。 */
export function initEnvWhitelist(filePath = TEST_WHITELIST_FILE) {
  if (loaded) return { ok: true, testUids: [...entries.keys()] };
  file = filePath;
  try {
    if (fs.existsSync(file)) {
      const parsed = JSON.parse(fs.readFileSync(file, 'utf8'));
      const arr = Array.isArray(parsed) ? parsed
        : Array.isArray(parsed?.testUids) ? parsed.testUids : [];
      entries = new Map(arr.filter((u) => typeof u === 'string' && u.trim()).map((u) => [u.trim(), true]));
    }
    loaded = true;
    return { ok: true, testUids: [...entries.keys()] };
  } catch (e) {
    // 文件损坏不能静默吞：回失败，让启动日志明说（此时白名单为空 = 全员生产，安全侧收缩）
    loaded = true;
    return { ok: false, error: `白名单文件无法解析：${e?.message || e}` };
  }
}

/** 单测用：重置实例。 */
export function closeEnvWhitelist() {
  entries = new Map();
  loaded = false;
}

function persist() {
  try {
    const tmp = `${file}.tmp`;
    fs.writeFileSync(tmp, JSON.stringify([...entries.keys()].sort(), null, 2), 'utf8');
    fs.renameSync(tmp, file);
    return true;
  } catch (e) {
    return false;
  }
}

/** 这台设备是不是测试机。不是白名单成员 = 生产设备（学校设备）。 */
export function isTestUid(uid) {
  const u = String(uid ?? '').trim();
  if (!u) return false;
  return entries.has(u);
}

/** 加/移出测试白名单。 @returns {{ok:true, uid, test:boolean} | {ok:false, error:string}} */
export function setTestUid(uid, on) {
  const u = String(uid ?? '').trim();
  if (!u) return { ok: false, error: '缺 uid' };
  const want = !!on;
  if (entries.has(u) === want) return { ok: true, uid: u, test: want, unchanged: true };
  if (want) entries.set(u, true); else entries.delete(u);
  if (!persist()) {
    // 写盘失败 → 回滚内存（不许内存说"加了"，重启又没了）
    if (want) entries.delete(u); else entries.set(u, true);
    return { ok: false, error: '白名单写盘失败' };
  }
  return { ok: true, uid: u, test: want };
}

/** 全量清单（管理面板用）。 */
export function listTestUids() {
  return [...entries.keys()].sort();
}