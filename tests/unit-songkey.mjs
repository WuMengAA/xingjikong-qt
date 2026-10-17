// 星集控云端 · 班级点歌凭据（songkey）单元测试（2026-10-18）
// 跑法：node tests/unit-songkey.mjs
//
// 这些断言守的是**安全红线**，不是功能偏好：
//   1. 没配 STE_SONGKEY_SECRET ⇒ 功能禁用（fail-closed），**绝不回落明文存盘**
//   2. 存盘内容里**不能出现 Key 明文**（否则托管等于没托管）
//   3. 解密必须能还原（否则代发点歌会失败）
//   4. 换个 secret 必须解不出来（GCM 认证失败），且不崩
//   5. setKey 的返回值**不能带明文**（接口层泄漏面）
import assert from 'node:assert/strict';
import { createSongKeyStore } from '../src/class-songkey.js';

let pass = 0, fail = 0;
const ok = (m) => { pass++; console.log(`  PASS  ${m}`); };
const bad = (m, d) => { fail++; console.log(`  FAIL  ${m}`); if (d) console.log(`        ↳ ${d}`); };
const check = (c, m, d) => (c ? ok(m) : bad(m, d));

const SECRET = 'test-secret-0123456789abcdef';
const KEY = 'vhub_ABCDEFGHIJKLMNOPQRSTUVWXYZ123456';

function mkStore(secret, bag = { map: new Map() }) {
  return createSongKeyStore({
    secret,
    read: () => bag.map,
    write: (m) => { bag.map = m; },
    log: () => {},
  });
}

console.log('── 一、fail-closed：无密钥即禁用 ──');
{
  const s = mkStore('');
  check(s.enabled === false, '空 secret ⇒ enabled=false');
  const r = s.setKey('高三1班', KEY);
  check(r.ok === false && r.error === 'disabled', 'setKey 被拒绝（disabled）');
  check(s.getKey('高三1班') === null, 'getKey 返回 null');
  check(s.has('高三1班') === false, 'has 返回 false');
}
{
  const s = mkStore('short');   // <16 字符
  check(s.enabled === false, 'secret 太短（<16）⇒ 仍禁用');
}

console.log('── 二、加密存储：盘上不得出现明文 ──');
const bag = { map: new Map() };
{
  const s = mkStore(SECRET, bag);
  check(s.enabled === true, '合法 secret ⇒ enabled=true');
  const r = s.setKey('高三1班', KEY);
  check(r.ok === true, 'setKey 成功');
  check(r.tail === KEY.slice(-4), '返回 tail（末4位）用于核对');
  check(!JSON.stringify(r).includes(KEY), 'setKey 返回值**不含明文**');

  const stored = JSON.stringify([...bag.map.entries()]);
  check(!stored.includes(KEY), '存盘内容**不含 Key 明文**');
  check(!stored.includes('vhub_'), '存盘内容不含 vhub_ 前缀片段');
  check(stored.includes(KEY.slice(-4)), '存盘含 tail（末4位，仅供核对）');

  // 解密还原
  check(s.getKey('高三1班') === KEY, 'getKey 能正确解密还原');
}

console.log('── 三、清单接口只给 tail ──');
{
  const s = mkStore(SECRET, bag);
  s.setKey('高二2班', 'vhub_SECONDKEY999999999999999999');
  const list = s.list();
  check(list.length === 2, `list 返回 2 个班（实际 ${list.length}）`);
  check(!JSON.stringify(list).includes(KEY), 'list **不含明文**');
  check(list.every((x) => x.tail && x.tail.length === 4), 'list 每项只有 4 位 tail');
}

console.log('── 四、换 secret 必须解不出（GCM 认证失败） ──');
{
  const s2 = mkStore('another-secret-abcdef0123456789', bag);
  check(s2.getKey('高三1班') === null, '换 secret ⇒ 解密失败返回 null（不崩）');
}

console.log('── 五、输入校验 ──');
{
  const s = mkStore(SECRET, { map: new Map() });
  check(s.setKey('', KEY).error === 'bad_class', '空班级名被拒');
  check(s.setKey('高三1班', 'short').error === 'bad_key', '过短 Key 被拒');
  check(s.setKey('高三1班', KEY).ok === true, '正常 Key 通过');
  check(s.remove('高三1班') === true, 'remove 生效');
  check(s.has('高三1班') === false, 'remove 后 has=false');
}

console.log('── 六、加密是随机化的（同明文两次加密不同） ──');
{
  // ⚠️ 必须用**两个独立 bag**：同一个 bag 会被前一个 store 的缓存持有，
  //    再拿它读密文会读到别处的状态（上一版就写错了，测试自己报 TypeError）。
  const b1 = { map: new Map() };
  const b2 = { map: new Map() };
  mkStore(SECRET, b1).setKey('A班', KEY);
  mkStore(SECRET, b2).setKey('A班', KEY);
  const e1 = b1.map.get('A班').enc;
  const e2 = b2.map.get('A班').enc;
  check(!!e1 && !!e2, '两次都拿到了密文');
  check(e1 !== e2, '同一明文两次加密结果不同（IV 随机，防止密文可对比）');
}

console.log(`\n════ ${pass} 通过 / ${fail} 失败 ════`);
process.exit(fail ? 1 : 0);
