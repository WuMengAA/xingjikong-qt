// 重认证门（Re-auth Gate）—— 管理端定期要求验证密码（2026-10-24）
//
// ── 为什么有这个文件 ──────────────────────────────────────────────────
// 用户诉求（2026-10-24）：「可以改，但是你要保证可以一直连接，管理端定期要求验证密码」。
// 拍板方案：「静默续期 + 定期弹窗」。
//
// 一个架构事实决定落点：**云端不存密码**（权限/密码真源在站点）。
// 所以「验证密码」在云端只能拆成两半：
//   · 云端记住「这个人最后一次通过验证的时刻」—— 票据在登录那一刻已被站点验证过；
//   · 超过间隔后，**敏感操作**（下发指令/开终端/改班级/加人/远控）拒绝执行，
//     但**连接不掐**（这是"一直连接"红线）—— 回 REAUTH_REQUIRED 让客户端弹密码框，
//     重新登录拿新票据、重新 auth，云端刷新时刻后放行。
//
// 静默续期从哪来：票据本身有 TTL，viewer 每次**重新 auth 成功**都刷新时刻；
// 尚未到期的窗口期内一切照旧（静默）；只有超了间隔撞上敏感操作才弹窗。
//
// 静态令牌（mode='token'，uid=null）是**系统身份不是人**，永不要求重认证——
// 否则离线机房/老机器每次都要人输密码，直接断服务（"保证一直连接"）。

const verifiedAt = new Map();   // uid -> ts

/** 记录一次通过验证（viewer auth 成功时调）。uid 为 null（静态令牌）不记录。 */
export function noteVerified(uid, ts = Date.now()) {
  const u = String(uid ?? '').trim();
  if (!u) return { ok: false, reason: 'uid 为空（系统身份无需记录）' };
  verifiedAt.set(u, ts);
  // 顺手限表长：uid 数量有上界（学校账号数），超过 1 万清最旧一半（防灌 uid 表）。
  if (verifiedAt.size > 10000) {
    const drop = Math.floor(verifiedAt.size / 2);
    let i = 0;
    for (const k of verifiedAt.keys()) {
      verifiedAt.delete(k);
      if (++i >= drop) break;
    }
  }
  return { ok: true, verifiedAt: ts };
}

/**
 * 人的验证还新不新。**没记录过 = 不新鲜**（保守侧：没经过验证就默认拦，直到 auth 成功）。
 * @param {string|null} uid 管理端账号；null（静态令牌）恒新鲜。
 * @param {number} maxAgeMs 判定窗口（测试可注入小值）
 */
export function isFresh(uid, maxAgeMs) {
  if (uid == null || String(uid).trim() === '') return true;   // 系统身份
  const last = verifiedAt.get(String(uid).trim());
  if (!last) return false;
  return Date.now() - last < maxAgeMs;
}

/** 诊断：距上次验证还剩多新（管理端可显示"会话新鲜至 X"）。 */
export function freshnessOf(uid) {
  const u = String(uid ?? '').trim();
  if (!u) return null;
  return verifiedAt.get(u) ?? null;
}

/** 单测用：清空。 */
export function resetReauth() { verifiedAt.clear(); }