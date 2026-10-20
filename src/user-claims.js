// 云端 user-claims 存储（2026-10-20 · RBAC 权限隔离第二批）
//
// 为什么要有这个存储：
//   权限真源在站点（permissions.ts + 用户档案），由站点签进 viewer 票的 claims；
//   但站点不在本三仓（外部工程，改不动），当前站点签的票是 bare-uid（第 3 段纯 uid、
//   无 claims）⇒ verifyViewerTicket 拿不到 roles/scopes ⇒ identityFromAuth scopes=null
//   ⇒ 全权，权限永远流动不起来。
//
//   打通方案（契约 §4.4 推荐项 A 的延展，最干净、零回归）：
//   云端建 claims 存储，鉴权时按 uid 回填 —— bare-uid 票在鉴权瞬间按 uid 从本存储
//   取 roles/scopes 补进 identity（applyStoredClaims），权限就真正流动了：
//     · 存储为空 / 未配 => 行为 = 今天（全权，零回归）；
//     · 站点将来全面签发 claims 票 => 票自带 claims，本存储自动退役（回填不覆盖票内 claims）。
//
// 存储形态：内存 Map + 落盘 JSON（重启不丢）。默认文件 <云端根>/user-claims.json，
//   与 class-dir.db / instruction-queue.db 同一目录约定。
//
// 纪律：
//   · claims 结构与票内 claims 同构：{schoolId?, roles[], scopes[]}；
//   · set/delete 由调用方记审计（谁改的）；本模块只管存取，不写日志；
//   · 写盘走 tmp+rename 原子替换，损坏不崩服务（退化空存储，行为=今天）；
//   · scopes 必须显式数组：[] = 显式零权限（与「无 claims ⇒ 全权」严格区分，契约 §4.4）。

import { readFileSync, renameSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const DEFAULT_FILE = join(dirname(fileURLToPath(import.meta.url)), '..', 'user-claims.json');

// uid -> { schoolId?, roles[], scopes[], updatedAt }
const claims = new Map();
let file = DEFAULT_FILE;

/** 初始化（幂等）：加载已有存储，损坏时退化为空存储。调用方负责在启动时打日志。 */
export function initUserClaims(filePath = DEFAULT_FILE) {
  file = filePath;
  claims.clear();
  try {
    if (existsSync(file)) {
      const raw = JSON.parse(readFileSync(file, 'utf8'));
      const list = raw?.claims && typeof raw.claims === 'object' ? raw.claims : {};
      for (const [uid, c] of Object.entries(list)) {
        if (!uid || !c || typeof c !== 'object') continue;
        claims.set(uid, normalize(c));
      }
    }
  } catch (e) {
    // 损坏不崩服务：退化为空存储（行为=今天，全权）。原文件保留待人工处置。
    claims.clear();
  }
  return { loaded: claims.size, file };
}

/** 字段规范化 + 防存进脏数据（roles/scopes 必须是数组，schoolId 非数组）。 */
function normalize(c) {
  const out = {};
  if (c.schoolId && typeof c.schoolId === 'string') out.schoolId = c.schoolId;
  out.roles = Array.isArray(c.roles) ? c.roles.filter((r) => typeof r === 'string') : [];
  out.scopes = Array.isArray(c.scopes) ? c.scopes.filter((s) => s && typeof s === 'object') : [];
  if (c.updatedAt) out.updatedAt = c.updatedAt;
  return out;
}

function persist() {
  const snapshot = { v: 1, updatedAt: new Date().toISOString(), claims: Object.fromEntries(claims) };
  const tmp = `${file}.tmp`;
  try {
    // tmp+rename 原子替换：进程中途挂掉也不会留下半截文件
    writeFileSync(tmp, JSON.stringify(snapshot, null, 2), 'utf8');
    renameSync(tmp, file);
  } catch (e) {
    // 写盘失败不崩服务，但要让调用方知道（返回 false）
    return false;
  }
  return true;
}

/** 取一个人的 claims（无记录 = null，调用方按「全权」对待）。 */
export function getUserClaims(uid) {
  if (!uid) return null;
  return claims.get(String(uid)) ?? null;
}

/**
 * 写/覆盖一个人的 claims。by = 操作者（审计，落盘字段）。
 * @returns {{ok:boolean, saved?:string[], error?:string}}
 */
export function setUserClaims(uid, c, by = 'unknown') {
  const id = String(uid || '').trim();
  if (!id) return { ok: false, error: 'uid 为空' };
  if (!c || typeof c !== 'object') return { ok: false, error: 'claims 必须是对象' };
  const norm = normalize({ ...c, updatedAt: new Date().toISOString(), by });
  claims.set(id, norm);
  const persisted = persist();
  return { ok: true, saved: { ...norm }, persisted };
}

/** 删除一个人的记录（删掉 = 回到「无 claims ⇒ 全权」）。 */
export function deleteUserClaims(uid) {
  const id = String(uid || '').trim();
  const existed = id ? claims.delete(id) : false;
  if (existed) persist();
  return { ok: true, existed };
}

/** 全量清单（管理 API 用，按 uid 排序稳定输出）。 */
export function listUserClaims() {
  return [...claims.entries()]
    .map(([uid, c]) => ({ uid, schoolId: c.schoolId ?? null, roles: c.roles, scopes: c.scopes, updatedAt: c.updatedAt ?? null }))
    .sort((a, b) => (a.uid < b.uid ? -1 : a.uid > b.uid ? 1 : 0));
}

/**
 * 鉴权回填（RBAC 第二批核心）：票没带 claims（旧票 bare uid）但存储里登记了
 * 该 uid 的 claims ⇒ 回填进 auth.claims，identityFromAuth 就能读到 roles/scopes。
 * 存储没有 / 票已带 claims / 非票模式 ⇒ 原样返回（行为不变）。
 * 注意：直接改入参对象（auth 是鉴权函数新建的，无共享引用风险）。
 */
export function applyStoredClaims(auth) {
  if (!auth || auth.ok !== true || auth.mode !== 'ticket') return auth;
  if (!auth.uid || (auth.claims && typeof auth.claims === 'object')) return auth;
  const stored = getUserClaims(auth.uid);
  if (stored) auth.claims = stored;
  return auth;
}