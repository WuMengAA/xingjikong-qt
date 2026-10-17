// 星集控 · 班级点歌凭据（VoiceHub API Key）托管（2026-10-18）
// ─────────────────────────────────────────────────────────────────────────────
// 需求（用户 2026-10-18 拍板）：每台被控端绑定到**班级**，点歌跟随班级账号同步；
// VoiceHub 的 API Key **按班级各持一个**。
//
// 为什么需要「托管」而不是每台机器各存一份：
//   每台教室机各存一个 Key ⇒ 60 份明文散落在 60 台机器上，任何一台被翻就泄露；
//   且 VoiceHub 侧 Key 只存 hash（见 voicehub-src/apiKeyUtils），明文本就不该落多份。
//   集中托管后：只有云端持有一处，按班级下发时**只下发"能不能点"**，不下发 Key 本身。
//
// ⚠️ 安全红线（违反即事故）：
//   1. Key 明文**绝不进日志、绝不进 /api/devices 之类的读接口、绝不推给被控端**。
//      被控端只需要知道「本班能不能点歌」，真正的写入由云端代发。
//   2. 存盘只存**加密后**的密文（AES-256-GCM）。密钥来自环境变量
//      STE_SONGKEY_SECRET；没配就**拒绝启用该功能**（fail-closed，不是回落明文）。
//   3. 支持"最后 4 位"用于人工核对：库里额外存一个 tail，够确认是哪把 Key，不足以复原。
//
// 为什么由云端代发（而不是把 Key 给被控端）：
//   VoiceHub 的 /api/open/songs/request 需要 x-api-key；若下发给 60 台机器，
//   等于把写权限散到端上。云端代发则：端上只能"请求点歌"，不能"直接点歌"，
//   且所有点歌请求都会经过云端的权限与频控。
import { createCipheriv, createDecipheriv, randomBytes, createHash } from 'node:crypto';

const ALGO = 'aes-256-gcm';
// 密文格式（base64）：[12B iv][16B tag][nB ciphertext]
const IV_LEN = 12;
const TAG_LEN = 16;

/** 从环境变量取密钥；没配返回 null（调用方据此 fail-closed 禁用功能）。 */
function deriveKey(secret) {
  const s = secret ?? process.env.STE_SONGKEY_SECRET ?? '';
  if (!s || String(s).trim().length < 16) return null;
  // 用 HKDF 风格的定长派生：sha256(secret) 直接作为 32 字节密钥。
  // ⚠️ 这里不引入额外依赖（云端只有 node 内置模块），sha256 对"高熵 secret"足够；
  //    短口令会被长度校验挡在门外（<16 直接拒绝），避免字典攻击面。
  return createHash('sha256').update(String(s), 'utf8').digest();
}

/**
 * 班级点歌凭据管理。
 * 存储后端：注入式（本文件不直连 SQLite，避免与 store.js 的 schema 耦合打架）。
 *   read()  -> Map<className, {enc, tail, updatedAt}>
 *   write(Map) -> void
 */
export function createSongKeyStore(opts = {}) {
  const secret = opts.secret ?? process.env.STE_SONGKEY_SECRET;
  const key = deriveKey(secret);
  const enabled = !!key;
  const read = typeof opts.read === 'function' ? opts.read : () => new Map();
  const write = typeof opts.write === 'function' ? opts.write : () => {};
  const log = typeof opts.log === 'function' ? opts.log : () => {};

  if (!enabled) {
    // fail-closed：没密钥就不启用。绝不"回落明文存盘"。
    log('[songkey] 未配置 STE_SONGKEY_SECRET（需 ≥16 字符）⇒ 班级点歌凭据功能已禁用');
  }

  function encrypt(plain) {
    const iv = randomBytes(IV_LEN);
    const c = createCipheriv(ALGO, key, iv);
    const ct = Buffer.concat([c.update(String(plain), 'utf8'), c.final()]);
    const tag = c.getAuthTag();
    return Buffer.concat([iv, tag, ct]).toString('base64');
  }

  function decrypt(enc) {
    const raw = Buffer.from(String(enc), 'base64');
    if (raw.length <= IV_LEN + TAG_LEN) throw new Error('密文长度异常');
    const iv = raw.subarray(0, IV_LEN);
    const tag = raw.subarray(IV_LEN, IV_LEN + TAG_LEN);
    const ct = raw.subarray(IV_LEN + TAG_LEN);
    const d = createDecipheriv(ALGO, key, iv);
    d.setAuthTag(tag);
    return Buffer.concat([d.update(ct), d.final()]).toString('utf8');
  }

  // 内存缓存：避免每次点歌都解密 + 读盘。写操作后失效。
  let cache = null;
  function load() {
    if (cache) return cache;
    cache = read() || new Map();
    return cache;
  }

  return {
    get enabled() { return enabled; },

    /** 为某个班级设置 VoiceHub Key。返回 { ok, tail } —— **绝不返回明文**。 */
    setKey(className, plainKey) {
      if (!enabled) return { ok: false, error: 'disabled', message: '未配置 STE_SONGKEY_SECRET，功能未启用' };
      const cls = String(className || '').trim();
      const k = String(plainKey || '').trim();
      if (!cls) return { ok: false, error: 'bad_class', message: '班级名不能为空' };
      // VoiceHub 的 Key 形如 vhub_xxxxxxxx…（见 apiKeyUtils.generateApiKey）。
      // 这里只做形态提示，不做强校验（上游格式可能演进），但必须非空且不太短。
      if (k.length < 16) return { ok: false, error: 'bad_key', message: 'Key 太短，疑似粘贴不完整' };

      const m = load();
      m.set(cls, {
        enc: encrypt(k),
        tail: k.slice(-4),                 // 仅供人工核对"是哪把 Key"
        updatedAt: Date.now(),
      });
      write(m);
      return { ok: true, tail: k.slice(-4) };
    },

    /** 取某班级的明文 Key —— **只允许云端内部调用（代发点歌）**，绝不经 HTTP/WS 下发。 */
    getKey(className) {
      if (!enabled) return null;
      const rec = load().get(String(className || '').trim());
      if (!rec) return null;
      try { return decrypt(rec.enc); }
      catch (e) { log(`[songkey] 解密失败（secret 变更？）class=${className}: ${e.message}`); return null; }
    },

    /** 清单：给管理界面显示"哪些班配了 Key"——只有 tail，没有明文。 */
    list() {
      const out = [];
      for (const [cls, rec] of load()) {
        out.push({ className: cls, tail: rec.tail, updatedAt: rec.updatedAt });
      }
      return out.sort((a, b) => a.className.localeCompare(b.className));
    },

    has(className) {
      if (!enabled) return false;
      return load().has(String(className || '').trim());
    },

    remove(className) {
      if (!enabled) return false;
      const m = load();
      const ok = m.delete(String(className || '').trim());
      if (ok) write(m);
      return ok;
    },
  };
}
