// 云端装机（provision）—— 让"装教室机"从一条 PowerShell 命令变成浏览器里点一下
//
// 背景（2026-10-05）：原来的装机路径是 `powershell -File install-classroom-agent.ps1 -Uid ...`，
// 意味着每台教室机前都要敲一行带令牌的命令。用户明确要求"不碰命令行，只要前端界面操作"，
// 所以把这套收进面板：面板填 uid → 云端现生成一枚**这台机器专属**的 bat → 浏览器下载 →
// U 盘拷过去 → 教室机双击 → bat 自己拉脚本、拉安装包、装、配 uid/云端/令牌、挂自启。
//
// 三块东西：
//   ① assets/           静态托管（安装包 + 装机脚本），教室机从这里取件
//   ② buildAgentProvisionBat()  按 uid 现算出 bat 文本（全 ASCII，避开 cmd/bat 编码坑）
//   ③ resolveAsset()     把 /assets/xxx 映射到磁盘，挡住目录穿越
//
// ⚠️ bat 内容必须是纯 ASCII：cmd 读 bat 用的是系统 ANSI 代码页，中文会变乱码。
//    中文提示一律放在 panel 上显示，bat 里只用符号行（[OK]/[!] 这种）和少量英文。
// ⚠️ 令牌会写进 bat（装机的人本来就要拿到它，这是"一机一密"），
//    所以 bat 不能随手丢在公共盘上 —— buildAgentProvisionBat 里已带这条注释。

import { existsSync, readdirSync, statSync, appendFileSync, readFileSync } from 'node:fs';
import { join, normalize, extname, sep } from 'node:path';
import { ASSETS_DIR } from './config.js';

/** 装机包产物目录（用户把 .exe 丢进来即可，不强制命名）。 */
export const PKG_DIR = join(ASSETS_DIR, 'pkg');

/** 装机脚本目录（云端托管的 install-agent.ps1 在这里）。 */
export const PROVISION_DIR = join(ASSETS_DIR, 'provision');

/**
 * 云端对外 Base URL。
 * 生产是 https://control.245959623.xyz；本机联调想走 http 就用 env 覆盖。
 */
export function publicBase() {
  return String(process.env.CLOUD_PUBLIC_BASE || 'https://control.245959623.xyz').replace(/\/+$/, '');
}

/**
 * 设备连云端的 ws 入口。
 * ⚠️ 必须用 wss:// 而不是 https:// —— agent 是 WebSocket 客户端，给它 https 地址会握手失败，
 *    而且连接直接连不上（2026-10-05 第一版就写错了，这里按 http/https 分别换 ws/wss）。
 */
export function agentWsUrl() {
  const base = publicBase();
  const scheme = /^https:/i.test(base) ? 'wss' : 'ws';
  return `${scheme}://${base.replace(/^https?:\/\//i, '')}/ws/agent`;
}

/** 管理端（老师机）连云端的 ws 入口。同样是 wss，别写成 https。 */
export function viewerWsUrl() {
  const base = publicBase();
  const scheme = /^https:/i.test(base) ? 'wss' : 'ws';
  return `${scheme}://${base.replace(/^https?:\/\//i, '')}/ws/viewer`;
}

/** /assets 下的静态资源前缀。 */
export const ASSET_PREFIX = '/assets/';

/** 只允许这些扩展名出现在 /assets 下，避免把磁盘上任意文件（含 .env）拖到公网。 */
const ASSET_ALLOW_EXT = new Set(['.exe', '.msi', '.ps1', '.bat', '.zip', '.json', '.txt', '.sha256']);

/**
 * /assets/<path> → 磁盘绝对路径；越界/不在白名单 → null（调用方回 404）。
 */
export function resolveAsset(urlPath) {
  const rel = String(urlPath || '').slice(ASSET_PREFIX.length).replace(/^\/+/, '');
  if (!rel) return null;
  if (!ASSET_ALLOW_EXT.has(extname(rel).toLowerCase())) return null;
  // 归一化后必须仍在 assets 目录内（挡 ../ 与 URL 编码变体之外的常规穿越）
  const abs = normalize(join(ASSETS_DIR, rel));
  if (abs !== ASSETS_DIR && !abs.startsWith(ASSETS_DIR + sep)) return null;
  if (!existsSync(abs) || !statSync(abs).isFile()) return null;
  return abs;
}

/**
 * 在 packages 目录里按扩展名挑一个安装包。
 * ⚠️ 必须按文件名里的关键字挑，不能"取第一个"：教学机和教室机的包都躺在同一个 pkg/ 下，
 *    光按扩展名取会把 `stelarith-agent-msi-*.msi` 当成管理端的包发出去
 *    —— 老师机装完 MSI 起的是被控端，装了等于没装（2026-10-05 实测撞到）。
 *    所以 prefer 命中就优先；一个都不命中时**宁可返回 null**（面板明说"没放包"），也不瞎猜。
 */
function pickPackage(ext, prefer) {
  if (!existsSync(PKG_DIR)) return null;
  const all = readdirSync(PKG_DIR)
    .filter((f) => extname(f).toLowerCase() === ext)
    .filter((f) => statSync(join(PKG_DIR, f)).isFile())
    .sort();
  if (!all.length) return null;
  const eagerly = all.find((f) => f.toLowerCase().includes(prefer.toLowerCase()));
  if (eagerly) return eagerly;
  // 退一步：同名的另一条产品线（如 agent 只有 setup.exe 没有别的 exe）就用唯一的那个
  return all.length === 1 ? all[0] : null;
}

/** 教室机安装包（.exe，取 stelarith-agent-setup*.exe）。 */
export function currentAgentPackage() {
  return pickPackage('.exe', 'agent-setup') || pickPackage('.exe', 'setup');
}

/** 管理端安装包（.msi，取 stelarith-viewer-msi*.msi）。 */
export function currentViewerPackage() {
  return pickPackage('.msi', 'viewer');
}

/* ---------- 装机台账（2026-10-05）----------
 * 以前装一台机器要在纸上/备忘录里记一行「uid=… 班级=… 日期=…」，装几十台就记不住、对不上。
 * 现在每生成一枚装机包就往这个 jsonl 追加一条，面板直接列出来 —— 台账这件事从人活变机器活。
 * 用 jsonl（一行一条）而不是 json：装机是高频追加， rewrite 整个文件的写法会在并发下互相覆盖。
 */
const PROVISION_LOG = join(ASSETS_DIR, 'provision-log.jsonl');

/** 记一条装机记录。写失败不能影响装机包本身 —— 台账丢了还能从事件流水补，所以这里宁可静默失败。 */
export function recordProvision(type, uid, pkg) {
  try {
    const line = JSON.stringify({ at: new Date().toISOString(), type, uid, package: pkg || '' }) + '\n';
    appendFileSync(PROVISION_LOG, line, 'utf8');
    return true;
  } catch {
    return false;
  }
}

/** 最近 n 条装机记录（新的在前）。文件不存在/坏了就返回 []，别让面板挂掉。 */
export function recentProvision(n = 20) {
  try {
    const raw = readFileSync(PROVISION_LOG, 'utf8').trim();
    if (!raw) return [];
    return raw.split(/\r?\n/)
      .filter(Boolean)
      .slice(-n)
      .map((l) => { try { return JSON.parse(l); } catch { return null; } })
      .filter(Boolean)
      .reverse();
  } catch {
    return [];
  }
}

/** 装机脚本在 assets 里的相对路径（bat 需要它来下载自己）。 */
export const PROVISION_SCRIPT = 'provision/install-agent.ps1';
/** 管理端那支（老师机，装 MSI + 写用户环境变量 + 挂自启）。 */
export const PROVISION_VIEWER_SCRIPT = 'provision/install-viewer.ps1';

/**
 * 生成"管理端（老师机）装机 bat"。
 * 与教室机那支的区别：管理端不占设备 uid（它是看的人），配置靠用户级环境变量，见 install-viewer.ps1 头注。
 *
 * @param {string} label 备注名（老师/科室，只影响 bat 文件名与提示，不参与装机）
 * @param {string} token 云端 CLOUD_VIEWER_TOKEN
 */
export function buildViewerProvisionBat(label, token) {
  const pkg = currentViewerPackage();
  if (!pkg) throw new Error('assets/pkg 目录下还没有 .msi 安装包，先把 stelarith-viewer-msi-0.5.0.msi 放进去');

  const base = publicBase();
  const scriptUrl = `${base}${ASSET_PREFIX}${PROVISION_VIEWER_SCRIPT}`;
  const installerUrl = `${base}${ASSET_PREFIX}pkg/${pkg}`;
  const safe = String(label || 'teacher').replace(/[^\w.-]+/g, '_').replace(/^_+|_+$/g, '') || 'teacher';

  return [
    '@echo off',
    'title Stelarith Viewer - Provisioning',
    'cd /d "%~dp0"',
    'powershell -NoProfile -ExecutionPolicy Bypass -Command "$d = $PWD.Path; $ps = Join-Path $d \'install-viewer.ps1\'; Write-Host \'[*] Downloading provisioning script...\'; Invoke-WebRequest -Uri \'' + scriptUrl + '\' -OutFile $ps -UseBasicParsing -TimeoutSec 120; Write-Host \'[*] Running installer. If a UAC box appears, choose Yes.\'; & $ps -Cloud \'' + viewerWsUrl() + '\' -Token \'' + token + '\' -InstallerUrl \'' + installerUrl + '\'; Write-Host \'\'; Read-Host \'Press Enter to exit\'"',
    '',
  ].join('\r\n');
}

/** 面板/路由统一入口：type=agent（教室机）| viewer（管理端），name 是那台机器的标识。 */
export function buildProvisionBat(type, name, token) {
  if (type === 'viewer') return buildViewerProvisionBat(name, token);
  return buildAgentProvisionBat(name, token);
}

/**
 * 生成"教室机装机 bat"。
 *
 * @param {string} uid  这台机器的唯一代号（命名规范 class_<校简称>_<届>_<班号>）
 * @param {string} token 云端 CLOUD_WS_TOKEN（会写进 bat，见文件头 ⚠️）
 */
export function buildAgentProvisionBat(uid, token) {
  const pkg = currentAgentPackage();
  if (!pkg) throw new Error('assets/pkg 目录下还没有 .exe 安装包，先把 stelarith-agent-setup.exe 放进去');

  // ⚠️ 别在这里再加一个 '/' —— ASSET_PREFIX 自带前导斜杠，多一个就变成 //assets/... ，
  //    Node 不会帮你折叠双斜杠，resolveAsset 拿到的相对路径会少一层，直接 404。
  const base = publicBase();
  const scriptUrl = `${base}${ASSET_PREFIX}${PROVISION_SCRIPT}`;
  const installerUrl = `${base}${ASSET_PREFIX}pkg/${pkg}`;
  // 离线兜底：若 U 盘里已放了同名 exe，脚本会优先用它，不去下载
  const localInstaller = 'stelarith-agent-setup.exe';

  return [
    '@echo off',
    'title Stelarith Classroom Agent - Provisioning',
    'cd /d "%~dp0"',
    'powershell -NoProfile -ExecutionPolicy Bypass -Command "$d = $PWD.Path; $ps = Join-Path $d \'install-agent.ps1\'; Write-Host \'[*] Downloading provisioning script...\'; Invoke-WebRequest -Uri \'' + scriptUrl + '\' -OutFile $ps -UseBasicParsing -TimeoutSec 120; Write-Host \'[*] Running installer. If a UAC box appears, choose Yes.\'; & $ps -Uid \'' + uid + '\' -Cloud \'' + agentWsUrl() + '\' -Token \'' + token + '\' -InstallerUrl \'' + installerUrl + '\' -Installer (Join-Path $d \'' + localInstaller + '\'); Write-Host \'\'; Read-Host \'Press Enter to exit\'"',
    '',
  ].join('\r\n');
}
