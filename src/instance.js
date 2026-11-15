// 实例自治契约（2026-11-15 · 用户点单「走向更多电教委员和学校」）
//
// 一句话：每个部署只代表自己。它管自己的设备、班级、令牌与数据，
// 不代理、不指挥、也不被别的云端指挥。本模块是这条契约的唯一代码落点。
//
// 背景：旧 provision.js 把官方生产域名 control.245959623.xyz 写成了
// CLOUD_PUBLIC_BASE 的默认值。别人拿这份代码单独部署，生成的装机 bat
// 会连回我方云端 —— 那不是「独立部署」，是我方对别人实例的一次隐式接管。
// 现在改为 fail-closed：不填自己的地址就不生成装机包，绝不猜、绝不回落到我方地址。
//
// 本模块只做纯函数（单测 / 部署自检脚本可直接调），不碰网络、不碰磁盘。

export const SOVEREIGNTY_CONTRACT = Object.freeze({
  version: 1,
  crossCloudControl: false,
  statement: '每个实例自治；本代码不提供跨实例控制通道，也不接受外部实例的控制指令。',
});

// 这些键一旦出现，说明有人想给本实例装一个「上级云端」。留作部署自检的否决名单。
export const FORBIDDEN_CROSS_CLOUD_KEYS = Object.freeze([
  'STE_UPSTREAM_CLOUD_URL',
  'CLOUD_REMOTE_CONTROL_URL',
  'CLOUD_CENTRAL_TOKEN',
  'STE_MASTER_SERVER',
]);

export function resolvePublicBase(env = process.env) {
  const raw = String(env.CLOUD_PUBLIC_BASE || '').trim().replace(/\/+$/, '');
  if (!raw) return '';
  return /^https?:\/\/[^\s/?#]+$/i.test(raw) ? raw : '';
}

export function wsUrlOf(base, path) {
  const b = String(base || '').replace(/\/+$/, '');
  if (!b) return '';
  const scheme = /^https:/i.test(b) ? 'wss' : 'ws';
  return scheme + '://' + b.replace(/^https?:\/\//i, '') + path;
}

export function provisionReadiness(env = process.env) {
  const publicBase = resolvePublicBase(env);
  const agentWsUrl = wsUrlOf(publicBase, '/ws/agent');
  const viewerWsUrl = wsUrlOf(publicBase, '/ws/viewer');
  return {
    ok: !!publicBase,
    publicBase,
    agentWsUrl,
    viewerWsUrl,
    reason: publicBase ? '' : '未配置 CLOUD_PUBLIC_BASE：装机包不会指向任何外部实例',
  };
}

export function forbiddenConfigured(env = process.env) {
  return FORBIDDEN_CROSS_CLOUD_KEYS.filter((k) => String(env[k] || '').trim() !== '');
}

export const VENDOR_BASE_RE = /245959623\.xyz$/i;

export function isVendorBase(base) {
  const b = String(base || '').replace(/\/+$/, '');
  if (!b) return false;
  try { return VENDOR_BASE_RE.test(new URL(b).hostname); }
  catch { return false; }
}

/** 严格独立部署判据：对外地址是否仍指向本项目官方域名。 */
export function foreignBaseConfigured(env = process.env) {
  return isVendorBase(resolvePublicBase(env));
}

export function instanceSummary(env = process.env) {
  return {
    ...SOVEREIGNTY_CONTRACT,
    ...provisionReadiness(env),
    vendorBase: foreignBaseConfigured(env),
    forbiddenConfigured: forbiddenConfigured(env),
  };
}
