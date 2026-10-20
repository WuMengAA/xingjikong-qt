// 设备绑定：**二维码矩阵生成** + **一次性绑定票据表**（2026-10-20）
//
// 为什么自己写 QR 编码器（而不是 npm i qrcode）：
//   ① 本项目依赖面刻意保持「只有 ws」——云端是常年无人值守跑在机房里的，多一个包就多一份
//      供应链与升级风险；QR 是**冻结标准**（ISO/IEC 18004），不会变版本，抄一次终身受用。
//   ② 只需要「byte 模式 + ECL M + 版本 1–10」这一小片能力：内容永远是本站的绑定 URL
//      （≤ 100 字节），用不到汉字模式/ECL H/微型码。
//   ③ ⚠️ 但"自己写"必须**可验证**：scripts/_verify_qr.mjs 用业界最常用的 node-qrcode
//      逐版本逐掩码做像素级比对（不是"看起来像"）。生成扫不出来的码比不显示更糟。
//
// 票据模型（一条码两种来路，设备侧看到的都是「6 位绑定码」）：
//   · kind='device'：设备自己申请（GET /api/device/bind-qr?uid=…），**码里没有班级**，
//                    手机扫码后在落地页选班级 ⇒ 这是「没有管理员在旁边」的自助路。
//   · kind='class' ：管理员预先按班生成（POST /api/admin/bindcode），**码里带班级**，
//                    老师在设备上直接输码即绑定，或手机扫码点"确认"。
//
// 纪律：
//   · 码表只在内存（TTL 5 分钟 / 管理员码 24 小时）。云端重启丢票 = 让老师再点一次「换一张」，
//     比把一次性凭据写进磁盘更安全；
//   · 码只用无歧义字符（去掉 I/O/0/1），老师照着念不会念错；
//   · 任何失败都带人话 error，不返回 ok:true 的假成功。

/* ───────────────────────── ① QR 编码器（byte 模式 / ECL M） ───────────────────────── */

// GF(256)，本原多项式 0x11D（QR 规范固定值）
const GF_EXP = new Uint8Array(512);
const GF_LOG = new Uint8Array(256);
{
  let x = 1;
  for (let i = 0; i < 255; i++) {
    GF_EXP[i] = x;
    GF_LOG[x] = i;
    x <<= 1;
    if (x & 0x100) x ^= 0x11d;
  }
  for (let i = 255; i < 512; i++) GF_EXP[i] = GF_EXP[i - 255];
}

function gfMul(a, b) {
  if (a === 0 || b === 0) return 0;
  return GF_EXP[GF_LOG[a] + GF_LOG[b]];
}

const GEN_CACHE = new Map();
/** 生成多项式 g(x) = ∏(x - α^i)，系数按 x 的高次到低次排列，首项恒 1。 */
function rsGenerator(degree) {
  const hit = GEN_CACHE.get(degree);
  if (hit) return hit;
  let poly = [1];
  for (let i = 0; i < degree; i++) {
    const next = new Array(poly.length + 1).fill(0);
    for (let j = 0; j < poly.length; j++) {
      next[j] ^= poly[j];
      next[j + 1] ^= gfMul(poly[j], GF_EXP[i]);
    }
    poly = next;
  }
  GEN_CACHE.set(degree, poly);
  return poly;
}

/** 对一个数据块求 ecLen 个纠错码字。 */
function rsEncode(data, ecLen) {
  const gen = rsGenerator(ecLen);
  const buf = new Array(data.length + ecLen).fill(0);
  for (let i = 0; i < data.length; i++) buf[i] = data[i];
  for (let i = 0; i < data.length; i++) {
    const factor = buf[i];
    if (factor === 0) continue;
    for (let j = 0; j < gen.length; j++) buf[i + j] ^= gfMul(gen[j], factor);
  }
  return buf.slice(data.length);
}

// ECL M 的分块表：[{ ec: 每块纠错码字数, blocks: [[块数, 每块数据码字数], …] }]
const ECC_M = {
  1: { ec: 10, blocks: [[1, 16]] },
  2: { ec: 16, blocks: [[1, 28]] },
  3: { ec: 26, blocks: [[1, 44]] },
  4: { ec: 18, blocks: [[2, 32]] },
  5: { ec: 24, blocks: [[2, 43]] },
  6: { ec: 16, blocks: [[4, 27]] },
  7: { ec: 18, blocks: [[4, 31]] },
  8: { ec: 22, blocks: [[2, 38], [2, 39]] },
  9: { ec: 22, blocks: [[3, 36], [2, 37]] },
  10: { ec: 26, blocks: [[4, 43], [1, 44]] },
};

const ALIGN_POS = {
  1: [], 2: [6, 18], 3: [6, 22], 4: [6, 26], 5: [6, 30],
  6: [6, 34], 7: [6, 22, 38], 8: [6, 24, 42], 9: [6, 26, 46], 10: [6, 28, 50],
};

const MAX_VERSION = 10;

function dataCodewordsOf(version) {
  const spec = ECC_M[version];
  if (!spec) return 0;
  return spec.blocks.reduce((n, [cnt, len]) => n + cnt * len, 0);
}

/** 选能装下 len 个字节的最小版本（ECL M）。装不下返回 0（调用方据此报错，绝不静默截断）。 */
function pickVersion(len) {
  for (let v = 1; v <= MAX_VERSION; v++) {
    const capBytes = dataCodewordsOf(v) - Math.floor((4 + (v < 10 ? 8 : 16)) / 8) - (v < 10 ? 0 : 0);
    // 精确判据：位容量 = dataCodewords*8；头部 = 4 + 计数位
    const bits = 4 + (v < 10 ? 8 : 16) + len * 8;
    if (bits <= dataCodewordsOf(v) * 8 && capBytes > 0) return v;
  }
  return 0;
}

/** 字节流 → 数据码字（含终止符与填充）。 */
function encodeData(bytes, version) {
  const capacity = dataCodewordsOf(version);
  const bits = [];
  const push = (value, len) => { for (let i = len - 1; i >= 0; i--) bits.push((value >> i) & 1); };
  push(0b0100, 4);                                    // byte 模式
  push(bytes.length, version < 10 ? 8 : 16);          // 字符计数
  for (const b of bytes) push(b, 8);
  // 终止符：最多 4 个 0
  const remain = capacity * 8 - bits.length;
  push(0, Math.min(4, Math.max(0, remain)));
  // 补齐到字节边界
  while (bits.length % 8 !== 0) bits.push(0);
  const codewords = [];
  for (let i = 0; i < bits.length; i += 8) {
    let v = 0;
    for (let j = 0; j < 8; j++) v = (v << 1) | bits[i + j];
    codewords.push(v);
  }
  // 填充码字 0xEC / 0x11 交替
  const PAD = [0xec, 0x11];
  let p = 0;
  while (codewords.length < capacity) codewords.push(PAD[p++ % 2]);
  return codewords;
}

/** 分块 + 纠错 + 交织 → 最终比特流（按规范顺序）。 */
function buildBitStream(bytes, version) {
  const spec = ECC_M[version];
  const data = encodeData(bytes, version);
  const blocks = [];
  let off = 0;
  for (const [cnt, len] of spec.blocks) {
    for (let i = 0; i < cnt; i++) {
      const chunk = data.slice(off, off + len);
      off += len;
      blocks.push({ data: chunk, ec: rsEncode(chunk, spec.ec) });
    }
  }
  const out = [];
  const maxData = Math.max(...blocks.map((b) => b.data.length));
  for (let i = 0; i < maxData; i++) {
    for (const b of blocks) if (i < b.data.length) out.push(b.data[i]);
  }
  for (let i = 0; i < spec.ec; i++) {
    for (const b of blocks) if (i < b.ec.length) out.push(b.ec[i]);
  }
  const bits = [];
  for (const cw of out) for (let i = 7; i >= 0; i--) bits.push((cw >> i) & 1);
  return bits;
}

const MASKS = [
  (i, j) => (i + j) % 2 === 0,
  (i) => i % 2 === 0,
  (i, j) => j % 3 === 0,
  (i, j) => (i + j) % 3 === 0,
  (i, j) => (Math.floor(i / 2) + Math.floor(j / 3)) % 2 === 0,
  (i, j) => ((i * j) % 2) + ((i * j) % 3) === 0,
  (i, j) => (((i * j) % 2) + ((i * j) % 3)) % 2 === 0,
  (i, j) => (((i * j) % 3) + ((i + j) % 2)) % 2 === 0,
];

/** 格式信息：5 位数据(ECL 2 位 + 掩码 3 位) + BCH(15,5) 校验，再异或 0x5412。ECL M = 0b00。 */
function formatBits(mask) {
  const data = (0b00 << 3) | mask;
  let d = data << 10;
  for (let i = 14; i >= 10; i--) {
    if ((d >> i) & 1) d ^= 0x537 << (i - 10);
  }
  return ((data << 10) | d) ^ 0x5412;
}

/** 版本信息（v≥7）：6 位版本号 + BCH(18,6)，生成多项式 0x1F25。 */
function versionBits(version) {
  let d = version << 12;
  for (let i = 17; i >= 12; i--) {
    if ((d >> i) & 1) d ^= 0x1f25 << (i - 12);
  }
  return (version << 12) | d;
}

/** 构建矩阵（rows[y][x] === 1 表示深色）。mask 显式给出，便于测试逐掩码比对。 */
function buildMatrix(bytes, version, mask) {
  const size = 17 + 4 * version;
  const mod = Array.from({ length: size }, () => new Array(size).fill(null));

  const setFinder = (row, col) => {
    for (let r = -1; r <= 7; r++) {
      if (row + r < 0 || row + r >= size) continue;
      for (let c = -1; c <= 7; c++) {
        if (col + c < 0 || col + c >= size) continue;
        const dark = (r >= 0 && r <= 6 && (c === 0 || c === 6)) ||
          (c >= 0 && c <= 6 && (r === 0 || r === 6)) ||
          (r >= 2 && r <= 4 && c >= 2 && c <= 4);
        mod[row + r][col + c] = dark ? 1 : 0;
      }
    }
  };
  setFinder(0, 0);
  setFinder(size - 7, 0);
  setFinder(0, size - 7);

  // ⚠️ 校正图案必须在**时序图案之前**画：v7+ 的校正图案可能正落在第 6 行/第 6 列上
  //    （如 v7 的 (6,22) 与 (22,6)）。先画时序就会把中心点标成"已占用"，
  //    这两个校正图案被误判为"与定位图案重叠"跳过 ⇒ 40 个格子被错当数据区，
  //    多出的比特流长度对不上 ⇒ 整张码偏移（2026-10-20 与 node-qrcode 对拍时抓到）。
  const pos = ALIGN_POS[version] || [];
  for (const row of pos) {
    for (const col of pos) {
      if (mod[row][col] !== null) continue;          // 与定位图案重叠的跳过
      for (let r = -2; r <= 2; r++) {
        for (let c = -2; c <= 2; c++) {
          const dark = r === -2 || r === 2 || c === -2 || c === 2 || (r === 0 && c === 0);
          mod[row + r][col + c] = dark ? 1 : 0;
        }
      }
    }
  }

  // 时序图案（会跳过已被校正图案占用的格子，两者在规范里是相容的）
  for (let r = 8; r < size - 8; r++) if (mod[r][6] === null) mod[r][6] = r % 2 === 0 ? 1 : 0;
  for (let c = 8; c < size - 8; c++) if (mod[6][c] === null) mod[6][c] = c % 2 === 0 ? 1 : 0;

  // 格式信息占位（先按 mask 写入，掩码区同时标为已占用）
  const fmt = formatBits(mask);
  const bit = (v, i) => (v >> i) & 1;
  for (let i = 0; i < 15; i++) {
    if (i < 6) mod[i][8] = bit(fmt, i);
    else if (i < 8) mod[i + 1][8] = bit(fmt, i);
    else mod[size - 15 + i][8] = bit(fmt, i);
  }
  for (let i = 0; i < 15; i++) {
    if (i < 8) mod[8][size - i - 1] = bit(fmt, i);
    else if (i < 9) mod[8][15 - i - 1 + 1] = bit(fmt, i);
    else mod[8][15 - i - 1] = bit(fmt, i);
  }
  mod[size - 8][8] = 1;                              // 固定深色模块

  // 版本信息（v≥7）
  if (version >= 7) {
    const vb = versionBits(version);
    for (let i = 0; i < 18; i++) {
      const m = bit(vb, i);
      mod[Math.floor(i / 3)][(i % 3) + size - 8 - 3] = m;
      mod[(i % 3) + size - 8 - 3][Math.floor(i / 3)] = m;
    }
  }

  // 数据区（右下角蛇形填入），按掩码取反
  const bits = buildBitStream(bytes, version);
  let inc = -1;
  let row = size - 1;
  let bitIndex = 7;
  let byteIndex = 0;
  const maskFn = MASKS[mask];
  for (let col = size - 1; col > 0; col -= 2) {
    if (col === 6) col--;                            // 跳过竖直时序列
    for (;;) {
      for (let c = 0; c < 2; c++) {
        const x = col - c;
        if (mod[row][x] !== null) continue;
        let dark = false;
        if (byteIndex < bits.length) dark = bits[byteIndex * 8 + (7 - bitIndex)] === 1;
        if (maskFn(row, x)) dark = !dark;
        mod[row][x] = dark ? 1 : 0;
        bitIndex--;
        if (bitIndex < 0) { byteIndex++; bitIndex = 7; }
      }
      row += inc;
      if (row < 0 || row >= size) { row -= inc; inc = -inc; break; }
    }
  }
  return mod;
}

/** 掩码惩罚分（ISO 18004 §8.8.2 四条规则）。分越低越"好扫"。 */
function penalty(mod) {
  const size = mod.length;
  let score = 0;

  // 规则1：同色连续 ≥5
  const runScore = (line) => {
    let s = 0;
    let run = 1;
    for (let i = 1; i < line.length; i++) {
      if (line[i] === line[i - 1]) run++;
      else { if (run >= 5) s += 3 + (run - 5); run = 1; }
    }
    if (run >= 5) s += 3 + (run - 5);
    return s;
  };
  for (let i = 0; i < size; i++) {
    score += runScore(mod[i]);
    score += runScore(mod.map((r) => r[i]));
  }

  // 规则2：2×2 同色块
  for (let r = 0; r < size - 1; r++) {
    for (let c = 0; c < size - 1; c++) {
      const v = mod[r][c];
      if (v === mod[r][c + 1] && v === mod[r + 1][c] && v === mod[r + 1][c + 1]) score += 3;
    }
  }

  // 规则3：1:1:3:1:1 且一侧有 4 个浅色（含边界外延视作浅色）
  const P1 = [1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0];
  const P2 = [0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1];
  const matchAt = (line, from, pat) => {
    for (let i = 0; i < pat.length; i++) if (line[from + i] !== pat[i]) return false;
    return true;
  };
  for (let i = 0; i < size; i++) {
    const rowLine = mod[i];
    const colLine = mod.map((r) => r[i]);
    for (const line of [rowLine, colLine]) {
      for (let k = 0; k + 11 <= size; k++) {
        if (matchAt(line, k, P1)) score += 40;
        if (matchAt(line, k, P2)) score += 40;
      }
    }
  }

  // 规则4：深色占比偏离 50%
  let dark = 0;
  for (const row of mod) for (const v of row) if (v === 1) dark++;
  const ratio = (dark * 100) / (size * size);
  score += Math.floor(Math.abs(ratio - 50) / 5) * 10;
  return score;
}

/**
 * 生成二维码矩阵。
 * @param {string} text 内容（UTF-8）
 * @param {object} [opts] { version, mask } —— 不给就自动选最小版本 + 最优掩码
 * @returns {{ version:number, size:number, rows:string[] }} rows 是 0/1 字符串数组
 */
export function makeQrMatrix(text, opts = {}) {
  const bytes = Array.from(Buffer.from(String(text), 'utf8'));
  const needed = pickVersion(bytes.length);
  if (!needed) {
    throw new Error(`内容过长（${bytes.length} 字节），超出本编码器支持范围（ECL M / 版本 1–${MAX_VERSION}）`);
  }
  // ⚠️ 指定版本小于所需版本时必须**报错**：静默溢出会产出"图正常、扫不出"的码，
  //    这是本项目反复强调的"假成功"。测试里逐版本对拍就是靠这条守住。
  const version = opts.version || needed;
  if (!ECC_M[version]) throw new Error(`不支持的版本 ${version}（仅 1–${MAX_VERSION}）`);
  if (version < needed) {
    throw new Error(`版本 ${version} 装不下该内容（需至少 v${needed}）`);
  }
  let bestMask = 0;
  let bestMod = null;
  let bestScore = Infinity;
  const masks = Number.isInteger(opts.mask) ? [opts.mask] : [0, 1, 2, 3, 4, 5, 6, 7];
  for (const m of masks) {
    const mod = buildMatrix(bytes, version, m);
    if (!Number.isInteger(opts.mask)) {
      const s = penalty(mod);
      if (s < bestScore) { bestScore = s; bestMask = m; bestMod = mod; }
    } else {
      bestMask = m; bestMod = mod;
    }
  }
  return {
    version,
    mask: bestMask,
    size: bestMod.length,
    rows: bestMod.map((r) => r.join('')),
  };
}

/* ───────────────────────── ② 绑定票据（一次性绑定码） ───────────────────────── */

// 无歧义字符集：去掉 I / O / 0 / 1（老师照着念、照着抄不会错）
const CODE_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
const CODE_LEN = 6;
const DEVICE_TTL_MS = 5 * 60 * 1000;        // 设备自助码：5 分钟（QML 轮询窗口）
const CLASS_TTL_MS = 24 * 60 * 60 * 1000;   // 管理员班级码：24 小时（够贴一节课）
const MAX_TICKETS = 5000;                   // 上限护栏：码表由匿名请求驱动，不设上限会被刷爆

/** code -> { code, kind, uid, classCode, className, state, createdAt, expireAt, boundAt } */
const tickets = new Map();

function randomCode() {
  // 用 CSPRNG：绑定码是**凭据**，可预测就等于把班级送给路人
  const arr = new Uint8Array(CODE_LEN);
  globalThis.crypto.getRandomValues(arr);
  let out = '';
  for (let i = 0; i < CODE_LEN; i++) out += CODE_ALPHABET[arr[i] % CODE_ALPHABET.length];
  return out;
}

function sweep() {
  const now = Date.now();
  for (const [code, t] of tickets) {
    if (t.expireAt <= now) tickets.delete(code);
  }
  if (tickets.size > MAX_TICKETS) {
    // 淘汰最早过期的（与 registry 的 knownDevices 同款思路：留活跃的）
    const sorted = [...tickets.entries()].sort((a, b) => a[1].expireAt - b[1].expireAt);
    for (let i = 0; i < tickets.size - MAX_TICKETS; i++) tickets.delete(sorted[i][0]);
  }
}
const sweeper = setInterval(sweep, 60 * 1000);
if (sweeper.unref) sweeper.unref();

function normalizeCode(input) {
  return String(input || '').trim().toUpperCase().replace(/[^A-Z0-9]/g, '');
}

/**
 * 设备自助申请绑定码（码里不含班级，扫码后在落地页选）。
 * 同一 uid 的旧待确认票据会被作废 —— 点「换一张」之后旧码必须失效。
 */
export function issueDeviceBind(uid, { ttlMs = DEVICE_TTL_MS } = {}) {
  const u = String(uid || '').trim();
  if (!u) return { ok: false, error: '缺少设备标识' };
  sweep();
  for (const [code, t] of tickets) {
    if (t.kind === 'device' && t.uid === u && t.state === 'pending') tickets.delete(code);
  }
  let code = randomCode();
  let guard = 0;
  while (tickets.has(code) && guard++ < 50) code = randomCode();
  const now = Date.now();
  const ticket = {
    code, kind: 'device', uid: u, classCode: '', className: '',
    state: 'pending', createdAt: now, expireAt: now + ttlMs, boundAt: 0,
  };
  tickets.set(code, ticket);
  return { ok: true, ticket };
}

/** 管理员按班生成绑定码（码里带班级：老师输码即绑、或扫码点确认）。 */
export function issueClassBind(classCode, className, { ttlMs = CLASS_TTL_MS } = {}) {
  const cc = String(classCode || '').trim();
  if (!cc) return { ok: false, error: '缺少班级码' };
  sweep();
  let code = randomCode();
  let guard = 0;
  while (tickets.has(code) && guard++ < 50) code = randomCode();
  const now = Date.now();
  const ticket = {
    code, kind: 'class', uid: '', classCode: cc, className: String(className || '').trim(),
    state: 'pending', createdAt: now, expireAt: now + ttlMs, boundAt: 0,
  };
  tickets.set(code, ticket);
  return { ok: true, ticket };
}

/** 查票（不消费）。过期/不存在都返回 null，调用方给"码无效或已过期"。 */
export function getTicket(code) {
  const c = normalizeCode(code);
  if (!c) return null;
  const t = tickets.get(c);
  if (!t) return null;
  if (t.expireAt <= Date.now()) { tickets.delete(c); return null; }
  return { ...t };
}

/** 标记已绑定（幂等：重复 confirm 返回第一次的班级）。 */
export function markBound(code, { classCode, className, uid } = {}) {
  const c = normalizeCode(code);
  const t = tickets.get(c);
  if (!t) return { ok: false, error: '绑定码无效或已过期' };
  if (t.state === 'bound') {
    return { ok: true, already: true, ticket: { ...t } };
  }
  t.state = 'bound';
  t.boundAt = Date.now();
  if (classCode) t.classCode = String(classCode);
  if (className) t.className = String(className);
  if (uid && !t.uid) t.uid = String(uid);
  return { ok: true, ticket: { ...t } };
}

/** 设备侧轮询：只认「这张票 + 这个 uid」，别的设备的票一律视为无效。 */
export function pollDeviceBind(code, uid) {
  const t = getTicket(code);
  const u = String(uid || '').trim();
  if (!t) return { ok: false, state: 'invalid', error: '绑定码无效或已过期' };
  if (t.uid && u && t.uid !== u) return { ok: false, state: 'invalid', error: '绑定码不属于这台设备' };
  if (t.state === 'bound') {
    return { ok: true, state: 'bound', classCode: t.classCode, className: t.className };
  }
  return { ok: true, state: 'pending', classCode: '', className: '', expireAt: t.expireAt };
}

/** 运维视角：当前挂着多少待确认票（诊断用，不含班级以外的东西）。 */
export function ticketStats() {
  sweep();
  let pending = 0;
  let bound = 0;
  for (const t of tickets.values()) (t.state === 'bound' ? bound++ : pending++);
  return { total: tickets.size, pending, bound };
}

export const BIND_QR_LIMITS = { MAX_VERSION, DEVICE_TTL_MS, CLASS_TTL_MS, CODE_LEN };

// 内部件出口：只给 tests/ 下的校验脚本用（不是对外 API）。QR 编码器这种"错了也能出图"
// 的组件必须能逐层对拍 —— 矩阵一致只是最后一道，码字层对不上时得能一眼看出是哪一层。
export const __internals = { encodeData, buildBitStream, buildMatrix, formatBits, versionBits, penalty, pickVersion, dataCodewordsOf };
