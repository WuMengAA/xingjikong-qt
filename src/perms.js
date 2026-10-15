// 云端权限消费层（2026-10-15 · 契约《星集控-多校多身份-现状勘误与scope契约草案》§4.3）
//
// 位置：云端是**唯一权限入口**（指令下发 / 设备列表 / 订阅），但**不是权限真源**。
// 真源在站点（permissions.ts + 用户档案），由站点签进 viewer 票的 claims；
// 云端只做一件事：**从票里读 scopes，按 scope 过滤一切「返回设备 / 下发指令」路径**。
//
// 硬规矩（契约 §4.5，必须写进代码注释）：
//   ① 权限真源只有站点；云端只消费、不创造。
//   ② schoolId 只能从票来，绝不接受客户端参数指定（这里根本不读参数）。
//   ③ 云端任何「返回设备 / 下发指令」的路径必须过 can()；WS 广播必须过 scope 过滤。
//   ④ 前端隐藏 ≠ 权限；后端校验是唯一安全边界。
//
// 向后兼容（契约 §4.4，取推荐项 A）：
//   · 旧票（第 3 段是纯 uid、无 claims）⇒ scopes=null ⇒ **全权**（生产现状就是全权，
//     收紧会立刻断现有老师机；等站点全面签发 claims 票后再收）。
//   · 长期静态令牌 CLOUD_VIEWER_TOKEN ⇒ **全权**（老机器/离线机房不能断），但会记审计。
//   · claims 票显式 scopes=[] ⇒ 无任何数据权限（与「无 claims」严格区分）。
//
// scope 语义（契约 §4.2）：type ∈ {class, grade, school}，id 是班级码形态
// （class_xlzx_2028_08）；授权父节点覆盖子节点（school 覆盖所有 class_<校>_*，
// grade 覆盖 class_<校>_<届>_*）。目标设备先归约成 classCode 再比。

/** 从鉴权结果提取「权限身份」。auth 来自 authorizeViewer / verifyViewerTicket 的返回。 */
export function identityFromAuth(auth) {
  if (!auth || !auth.ok) return null;
  if (auth.mode === 'token') {
    // 长期静态令牌：全权（契约 §4.4）。uid=null（匿名），审计只靠连接/时间。
    return { uid: null, roles: [], scopes: null };
  }
  const claims = auth.claims ?? null;
  return {
    uid: auth.uid ?? claims?.uid ?? null,
    schoolId: claims?.schoolId ?? null,
    roles: Array.isArray(claims?.roles) ? claims.roles : [],
    scopes: Array.isArray(claims?.scopes) ? claims.scopes : null,
  };
}

/** 平台级超管（schoolId=null 且 roles 含 platform_admin）：跳过 scope 过滤。 */
function isPlatform(ident) {
  return ident.schoolId == null && Array.isArray(ident.roles) && ident.roles.includes('platform_admin');
}

/**
 * canAccess：这个连接能不能碰「classCode 归属的设备」。
 * @param {object|null} ident identityFromAuth 的结果（null = 未鉴权，一律拒）
 * @param {string|null} classCode 目标设备的班级码（registry 里没有 = null）
 * @returns {boolean}
 */
export function canAccess(ident, classCode) {
  if (!ident) return false;
  if (ident.scopes === null) return true;              // 全权：静态令牌 / 旧票（契约 §4.4-A）
  if (isPlatform(ident)) return true;                  // 平台级超管
  if (!classCode) return false;                        // 目标设备没有班级归属 → 谁都不该碰
  for (const s of ident.scopes) {
    if (!s || typeof s.type !== 'string' || typeof s.id !== 'string') continue;
    const sid = String(s.id);
    if (s.type === 'class' && sid === classCode) return true;
    // grade / school：前缀命中（class_xlzx_2028_08 前缀 class_xlzx_2028 = 届，class_xlzx = 校）
    if ((s.type === 'grade' || s.type === 'school') && classCode.startsWith(sid + '_')) return true;
  }
  return false;
}

/**
 * 列表过滤：全权身份原样返回；受限身份只留 scopes 内的设备。
 * @param {object|null} ident 权限身份
 * @param {Array} devices 设备数组
 * @param {(d:any)=>string|null} [getClass] 取班级码的回调（缺省读 d.classCode）
 */
export function filterDevices(ident, devices, getClass) {
  if (!ident || ident.scopes === null || isPlatform(ident)) return devices;
  const gc = getClass || ((d) => (d && typeof d.classCode === 'string' ? d.classCode : null));
  return devices.filter((d) => canAccess(ident, gc(d)));
}
