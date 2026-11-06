// 设备侧课表只读入口（2026-11-06 · 用户 m08463 第 3 项「课表开始下放给各个班级」）
// ───────────────────────────────────────────────────────────────────────────
// 为什么必须新开一个设备可读的端点，而不是复用既有 `/api/class/timetable`：
//
//   `index.js:1249` 的 isViewerApi 里有 `u.pathname.startsWith('/api/class/')`，
//   整个班级域**只认管理端令牌或站点票据**。被控端手上只有 `STE_QT_WS_TOKEN`
//   （设备令牌 / agent ticket），拿它打 `/api/class/timetable` 恒 401。
//   所以「教室机自己去云端拉本班课表」这条路一直是断的 —— 更准确地说，是从没建过：
//   现役下放走的是云端主动 `sendToClassDevices(code,'timetable-apply')` 推送
//   （class-timetable-push.js），只在班级**在线**且被 `timetable-push` 触发时才发。
//
//   教室机第一次开机、或错过那次推送（断线/重装/换机），就再也拿不到课表，
//   只能等管理员再点一次下发。本条补的就是这个缺口：**设备主动拉一次**。
//
// 鉴权口径（与 `/api/recordings/upload` 完全一致，不另造一套）：
//   Authorization: Bearer <设备令牌>（静态 DEV_TOKEN 或 agent ticket）
//   · agent ticket 验通 ⇒ uid 必须等于票里签的 uid（不许自报）
//   · 只能读**自己所在班**的课表，不接受 uid 换班级
//
// 返回结构直接复用 `getTimetable()` 的语义（profile 是 classisland/v1 信封原文），
// 不在这里做任何结构翻译 —— 翻译归被控端 `TimetableApply::applyEnvelope`，
// 云端多解释一层就等于多一份会漂移的真源。

import { getTimetable } from './class-data.js';
import { getDeviceClassCode, getDeviceTenant, listClasses } from './registry.js';

/** 按班号取显示名（只在本租户里找，找不到回空串 —— 不编一个假名字出来）。 */
function classNameOf(code, tenantId) {
  try {
    const hit = (listClasses(tenantId) || []).find((c) => c.code === code);
    return hit ? String(hit.name || '') : '';
  } catch {
    return '';
  }
}

/** 空课表 / 未分班的统一回执（被控端据此决定「稍后再试」还是「本机没班」）。 */
function emptyReply(classCode, extra = {}) {
  return {
    ok: true,
    classCode: classCode || '',
    className: '',
    profile: null,
    updatedAt: 0,
    reason: classCode ? 'NO_TIMETABLE' : 'NO_CLASS',
    ...extra,
  };
}

/**
 * 处理 `GET /api/device/timetable`。
 *
 * @param {object} deps  { uid, classCodeOf, tenantOf, read }
 *   uid        已鉴权得到的设备 uid（调用方负责验令牌，本函数不碰 req）
 *   classCodeOf 取设备班级码（registry.getDeviceClassCode）
 *   tenantOf    取设备租户（registry.getDeviceTenant）
 *   read        读课表（class-data.getTimetable）
 * @returns {{status:number, body:object}}
 */
export function deviceTimetable({ uid, classCodeOf, tenantOf, read }) {
  const u = String(uid || '').trim();
  if (!u) return { status: 400, body: { ok: false, error: '缺少设备标识' } };

  const classCode = String(classCodeOf(u) || '').trim();
  if (!classCode) {
    // 未分班不是错误：设备刚装好、还没绑定班级是正常状态。
    // 回 200 + reason=NO_CLASS，让被控端区分「没班」与「网络坏了」。
    return { status: 200, body: emptyReply('', { uid: u }) };
  }

  let name = '';
  try { name = classNameOf(classCode, tenantOf(u)); } catch { name = ''; }

  let r;
  try {
    r = read(classCode, tenantOf(u));
  } catch (e) {
    return { status: 500, body: { ok: false, error: `读课表失败：${e && e.message ? e.message : e}` } };
  }
  if (!r || r.ok === false) {
    return { status: 400, body: { ok: false, error: (r && r.error) || '读课表失败' } };
  }
  if (!r.profile) {
    return { status: 200, body: emptyReply(classCode, { uid: u, className: name }) };
  }

  return {
    status: 200,
    body: {
      ok: true,
      uid: u,
      classCode,
      className: name,
      profile: r.profile,          // classisland/v1 信封原文，云端不解释
      updatedAt: Number(r.updatedAt) || 0,
      updatedBy: String(r.updatedBy || ''),
      reason: '',
    },
  };
}

/** 便捷默认依赖（index.js 只需传 { uid } 就能用，保持那边零余量可控）。 */
export function deviceTimetableDefault(deps) {
  return deviceTimetable({
    classCodeOf: getDeviceClassCode,
    tenantOf: getDeviceTenant,
    read: getTimetable,
    ...deps,
  });
}
