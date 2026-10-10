// 生成 WiX v4 的 .wxs：把 deploy/ 目录枚举成 MSI 组件树。
// 用法：node scripts/gen-wxs.mjs > msi/stelarith-agent.wxs
// 构建：wix build msi/stelarith-agent.wxs -o msi/stelarith-agent-msi.msi -arch x64
//       ↑ -arch x64 必须带！不带的整包是 32 位，ProgramFiles64Folder 会被重定向到 (x86)。
// （deploy/ 里几十个 DLL + 嵌套子目录，手写 wxs 不可维护）

import { readdirSync, statSync, readFileSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DEPLOY = join(__dirname, '..', 'deploy');

function walk(dir) {
  const out = [];
  for (const name of readdirSync(dir)) {
    const full = join(dir, name);
    const rel = relative(DEPLOY, full).replace(/\\/g, '/');
    if (statSync(full).isDirectory()) {
      out.push({ rel: rel + '/', dir: true });
      out.push(...walk(full));
    } else {
      out.push({ rel, dir: false });
    }
  }
  return out;
}

// 版本号单一真源：从 build\version.nsh 的 `!define VER "x.y.z"` 读（由 CMake configure
// 生成，唯一真源＝CMakeLists.txt 的 set(AGENT_VERSION ...)）。MSI 包与 Inno 全量安装包
// 共用同一份，杜绝各写版本号而分叉（历史上 exe 报 0.4.0-v1、安装器写 0.5.0）。
const VER_NSHT = join(__dirname, '..', 'build', 'version.nsh');
let APP_VER = '0.0.0';
try {
  const m = readFileSync(VER_NSHT, 'utf8').match(/!define\s+VER\s+"([^"]+)"/);
  if (m) APP_VER = m[1];
  else console.error('[gen-wxs] WARN 未在 build/version.nsh 找到 !define VER，回落到 0.0.0');
} catch (e) {
  console.error(`[gen-wxs] WARN 读不到 build/version.nsh（${e.message}），回落到 0.0.0`);
}

// 目录树：rel 路径 -> 目录 id（INSTALLFOLDER 为根）
const ids = new Map([['', 'INSTALLFOLDER']]);
let n = 0;
const nextId = () => `D${++n}`;

function ensureDirId(rel) {
  if (ids.has(rel)) return ids.get(rel);
  const parts = rel.split('/');
  const parent = parts.slice(0, -1).join('/');
  const parentId = ensureDirId(parent);
  const id = nextId();
  ids.set(rel, id);
  return id;
}

// ⚠️ 根级 deploy/agent.env **不进 MSI**：那是本机（TEST1）的真实配置，含明文令牌 dev-cloud-token
//    与测试回路地址 ws://127.0.0.1:8788。打进去的后果有两个，都不该发生：
//      1) 任何机器装上就带着 TEST1 的配置（连开发回路、用公开的开发令牌）；
//      2) "装完没 agent.env → 首次运行弹配置向导（OOBE）"这条设计直接失效——
//         有配置文件时程序判定为"已配置"，老师根本不会看到填写界面。
//    装后默认**没有** agent.env，由老师从配置向导里填；agent.env.example 是纯占位模板
//    （<token-matching-cloud-CLOUD_WS_TOKEN> 这种），照常进包供参照。
const files = walk(DEPLOY).filter((f) => !f.dir && f.rel !== "agent.env");
const dirs = [...new Set(files.map((f) => f.rel.split('/').slice(0, -1).join('/')))].filter(Boolean);

// 输出 Directory 树
function dirXml(rel) {
  const childDirs = dirs.filter((d) => {
    const parts = d.split('/');
    return parts.length === (rel ? rel.split('/').length + 1 : 1);
  });
  const name = rel.split('/').pop();
  let s = `    <Directory Id="${ensureDirId(rel)}" Name="${name}">\n`;
  for (const c of childDirs) s += dirXml(c);
  s += `    </Directory>\n`;
  return s;
}

let wxs = `<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">
  <Package
      Name="星集控被控端"
      Manufacturer="Stelarith"
      Version="${APP_VER}"
      UpgradeCode="2F6C4F0E-6C21-4E24-9F79-8E5ED49A5C31"
      Scope="perMachine"
      Language="2052">
    <MajorUpgrade DowngradeErrorMessage="A newer version is already installed." />
    <MediaTemplate EmbedCab="yes" />

    <!-- 用 ProgramFiles64Folder（C:\Program Files）：被控端是 64 位 exe，且和 NSIS 版装同一处；
         32 位包若用 ProgramFiles6432Folder 会装进 C:\Program Files (x86)，两边就分叉了。 -->
    <StandardDirectory Id="ProgramFiles64Folder">
      <Directory Id="INSTALLFOLDER" Name="Stelarith">
`;

for (const d of dirs.filter((d) => d.split('/').length === 1)) wxs += dirXml(d);

wxs += `      </Directory>
    </StandardDirectory>

    <Feature Id="Main" Title="星集控被控端" Level="1">
`;

let cid = 0;
for (const f of files) {
  const dirRel = f.rel.split('/').slice(0, -1).join('/');
  const dirId = dirRel === '' ? 'INSTALLFOLDER' : ids.get(dirRel);
  const compId = `C${++cid}`;
  // 架构不由组件控制：WiX v4+ 已移除 Component 的 Win64 属性（写了反而 WIX0004）。
  // 64 位由构建参数 -arch x64 决定；配 ProgramFiles64Folder 就会装进 C:\Program Files。
  // 历史教训：之前没传 -arch x64，整包是 32 位，ProgramFiles64Folder 被重定向成 (x86) → 1603。
  wxs += `      <Component Id="${compId}" Directory="${dirId}">\n`;
  wxs += `        <File Source="deploy/${f.rel}" KeyPath="yes" />\n`;
  wxs += `      </Component>\n`;
}

wxs += `    </Feature>

    <!-- 装完：跑后置脚本（写默认 agent.env + 建登录自启任务；脚本在 deploy/msi-post-install.cmd）-->
    <!-- 注意：ExeCommand 里不要写内联 PowerShell（[ 会被当属性引用、反斜杠-v 会被当转义 → WIX0104）；
          执行逻辑都放 .cmd 脚本里，安装器只调它。
          ExeCommand 的 CustomAction 必须带 Directory/BinaryRef/FileRef/Property 之一（WIX0037/0044）。
          坑（2026-10-03 实测）：这里曾用 Directory="SystemFolder" → 在 x64 包里 [SystemFolder]
          被解析成 C:\Windows\SysWOW64\（32 位 cmd.exe，deferred+未标 64 位的 action 按 32 位引擎跑），
          32 位 cmd 在 SYSTEM 上下文执行 .cmd 返回 1 → 1722 → 整包回滚 1603。
          正解：Directory="System64Folder"（System32，64 位 cmd.exe），工作目录改 INSTALLFOLDER
          更贴脚本位置，且不依赖 System*Folder 解析。 -->
    <!-- 坑2（2026-10-03 实测，最致命）：原来 ExeCommand 末尾传了 "[INSTALLFOLDER]" 作为参数，
          WiX 渲染成 "C:\Program Files\Stelarith\" —— 尾反斜杠+引号在 Windows C 运行时命令行
          解析器里是"转义引号"（\" → 字面引号），导致引号不平衡 → cmd.exe /c 把整条命令行解析错乱
          → 脚本压根没跑起来（ProgramData 日志一个字节没写、agent.env 没生成、计划任务没建）。
          Return=ignore 实验铁证：安装成功(0)但脚本产物全无。
          正解：ExeCommand 不传任何参数，脚本用 %~dp0 自推导安装目录。 -->
    <CustomAction Id="PostInstall" Directory="INSTALLFOLDER" Execute="deferred" Impersonate="no" Return="check"
      ExeCommand='"[System64Folder]cmd.exe" /c "[INSTALLFOLDER]msi-post-install.cmd"' />

    <InstallExecuteSequence>
      <Custom Action="PostInstall" After="InstallFiles" />
    </InstallExecuteSequence>
  </Package>
</Wix>
`;

process.stdout.write(wxs);