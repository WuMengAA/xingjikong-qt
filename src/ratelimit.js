// 进程内滑动窗口限流（2026-10-27 · OOBE 绑定码洪水加固）
//
// 用途：/api/device/bind-qr（匿名签发绑定码）与 /api/bind/info（凭码查班级清单）
// 都是「匿名可连续调用」的端点，给它们加低频保护：
//   · bind-qr 60s / IP / 6 次 —— OOBE 正常换码重试 1-3 次/分钟，留足裕量；
//   · bind/info 60s / IP / 60 次 —— 手机选班页拉一次清单，正常 <5 次/分钟。
// key 用源 IP：攻击者可任意编 uid，但 IP 是硬锚（同 NAT 聚合用户被限严一点，
// 可接受——宁可误伤聚合也不给洪水开口）。
//
// 实现：内存 Map<key, number[]>（时间戳数组），惰性剪枝 + bucket 上限整体清场，
// 无外部依赖（依赖面刻意只有 ws，见 bind-qr.js 头注释同款纪律）。
// 单进程模型：重启即清零（限流是防滥用不是计费，丢窗口可接受）。

const buckets = new Map();
const MAX_BUCKETS = 5000;

/**
 * 放行一次并记时间戳。返回 true=放行，false=超限（调用方回 429）。
 * @param {string|null|undefined} key 限流键（null/空串恒放行，便于无条件跳过）
 * @param {number} limit 窗口内最大放行次数
 * @param {number} windowMs 窗口毫秒（默认 60s）
 */
export function hitRate(key, limit, windowMs = 60_000) {
  if (!key) return true;
  const now = Date.now();
  let arr = buckets.get(key);
  if (!arr) {
    if (buckets.size >= MAX_BUCKETS) buckets.clear(); // 拉闸式清场，防内存无限膨胀
    arr = [];
    buckets.set(key, arr);
  }
  const cutoff = now - windowMs;
  while (arr.length && arr[0] <= cutoff) arr.shift(); // 惰性剪枝（arr 长度 ≤ limit，代价可控）
  if (arr.length >= limit) return false;
  arr.push(now);
  return true;
}

/** 取出源 IP（细粒度用 remoteAddress——本服务在隧道/反代后时聚合粒度会变粗，可接受）。 */
export function clientIp(req) {
  try {
    const sock = req.socket || {};
    return String(sock.remoteAddress || '0.0.0.0');
  } catch {
    return '0.0.0.0';
  }
}