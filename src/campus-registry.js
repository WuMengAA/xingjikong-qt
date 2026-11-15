// 校区注册与选择（2026-11-16 · 用户 m33515）
// 咸林中学是真实租户/校区；班级选择校区服务器后，装机包只连本校区地址。
// 本文件只做纯函数与只读清单，不碰生产 DB / 不重启服务 / 不写 OTA。
import { resolvePublicBase, wsUrlOf, isVendorBase } from './instance.js';

export const XIANLIN_CAMPUS = Object.freeze({
  name: '渭南市华州区咸林中学校区',
  shortName: '咸林中学',
  region: '陕西省渭南市华州区',
  tenantName: '渭南市华州区咸林中学',
  serverLabel: '咸林中学校区服务器',
  publicBase: 'https://control.245959623.xyz',
  classroomCount: 12,
  classNames: ['1班','2班','3班','4班','5班','6班','7班','8班','9班','10班','11班','12班'],
});

export const CAMPUS_SCHEMA = 1;

/** 只接受协议://主机[:端口]，带路径/查询/空值一律拒绝（fail-closed）。 */
export function normalizeServerBase(raw) {
  const base = String(raw || '').trim().replace(/\/+$/, '');
  return /^https?:\/\/[^\s/?#]+$/i.test(base) ? base : '';
}

/** 校区服务器清单：当前只登记用户点名的咸林中学，供班级选择页下拉。 */
export function campusServers() {
  const b = normalizeServerBase(XIANLIN_CAMPUS.publicBase) || resolvePublicBase({ CLOUD_PUBLIC_BASE: XIANLIN_CAMPUS.publicBase });
  return [{
    id: 'xianlin',
    name: XIANLIN_CAMPUS.name,
    shortName: XIANLIN_CAMPUS.shortName,
    region: XIANLIN_CAMPUS.region,
    tenantName: XIANLIN_CAMPUS.tenantName,
    publicBase: b,
    agentWsUrl: wsUrlOf(b, '/ws/agent'),
    viewerWsUrl: wsUrlOf(b, '/ws/viewer'),
    vendorBase: isVendorBase(b),
    classroomCount: XIANLIN_CAMPUS.classroomCount,
  }];
}

export function findCampusServer(id) {
  return campusServers().find((s) => s.id === String(id || '').trim()) || null;
}

/** 班级选择校区：只回服务端钉死的字段，不信任请求体里的 URL / 租户。 */
export function chooseCampusServer({ campusId = 'xianlin', classCode = '', className = '' } = {}) {
  const server = findCampusServer(campusId);
  if (!server) return { ok: false, error: '校区服务器不存在' };
  if (!/^[A-Za-z0-9_-]{6,16}$/.test(String(classCode || '').trim())) {
    return { ok: false, error: '班级码不合法（需 6-16 位字母数字或 -_）' };
  }
  return {
    ok: true,
    campus: server,
    classCode: String(classCode).trim(),
    className: String(className || '').trim(),
    envelope: {
      schema: CAMPUS_SCHEMA,
      publicBase: server.publicBase,
      agentWsUrl: server.agentWsUrl,
      viewerWsUrl: server.viewerWsUrl,
      tenantName: server.tenantName,
    },
  };
}

/** 12 个班的注册计划：给初始化脚本用，不自己写库。 */
export function xianlinClassPlan() {
  const s = findCampusServer('xianlin');
  return XIANLIN_CAMPUS.classNames.map((name, i) => ({
    index: i + 1,
    name,
    label: `${XIANLIN_CAMPUS.shortName} ${name}`,
    groupKey: String(i + 1),
    campusId: s.id,
    tenantName: s.tenantName,
    publicBase: s.publicBase,
  }));
}
