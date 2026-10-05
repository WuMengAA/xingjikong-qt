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

import { existsSync, readdirSync, statSync } from 'node:fs';
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
 * 当前可用的教室机安装包文件名（扫 packages 目录里第一个 .exe）。
 * 一个都没有就返回 null —— 面板会明说"还没放安装包"，而不是生成一枚下不到东西的.bat。
 */
export function currentAgentPackage() {
  if (!existsSync(PKG_DIR)) return null;
  const hit = readdirSync(PKG_DIR)
    .filter((f) => extname(f).toLowerCase() === '.exe')
    .sort()
    .find((f) => statSync(join(PKG_DIR, f)).isFile());
  return hit || null;
}

/** 装机脚本在 assets 里的相对路径（bat 需要它来下载自己）。 */
export const PROVISION_SCRIPT = 'provision/install-agent.ps1';

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
