// 管理端（老师机 viewer）专用的 WiX v4 生成器。
// 用法：node scripts/gen-wxs-viewer.mjs > msi/viewer-qt.wxs
// 构建：wix build msi/viewer-qt.wxs -o msi/stelarith-viewer-msi.msi -arch x64
//       ↑ -arch x64 必须带，否则整包 32 位，ProgramFiles64Folder 会被重定向到 (x86)。
//
// 与被控端 gen-wxs.mjs 的三处关键差异（别照抄被控端）：
// 1. UpgradeCode 必须不同 —— 相同的话两个 MSI 会互相认作"同产品升级"，装一个把另一个顶掉。
// 2. 安装目录 Name="Stelarith Viewer" —— 与被控端的 "Stelarith" 分开，避免两个包往同一目录
//    装文件、卸载时互相删对方的文件。
// 3. 不需要 PostInstall CustomAction —— 管理端是老师手动双击打开的，不做开机自启，
//    配置 viewer.env 已作为普通文件打进包里，不需要脚本生成。
// （deploy/ 下 1289 个文件含 qml 插件树，手写 wxs 不可维护，必须生成）

import { readdirSync, statSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DEPLOY = join(__dirname, '..', 'deploy');

// 版本号单一真源（2026-10-05）：从 src/main.cpp 的 kViewerVersion 读，
// 保证 MSI 版本与应用内版本一致（不必再手改这里）。
// ⚠️ 2026-10-08 修：kViewerVersion 是 semver（如 "0.6.23-rc.2"），WiX 的 Package Version
//    只认纯数字 a.b.c.d，直接塞 "-rc.2" 会让 `wix build` 报非法版本。这里收敛成 4 段数字
//    （"0.6.23-rc.2" → "0.6.23.2"），原始串另存 VERSION_RAW 给 NSIS 展示用。
let APP_VER = '0.0.0';
try {
  const m = readFileSync(join(__dirname, '..', 'src', 'main.cpp'), 'utf8')
    .match(/kViewerVersion\s*=\s*"([^"]+)"/);
  if (m) APP_VER = m[1];
  else console.error('[gen-wxs-viewer] WARN 未找到 kViewerVersion，回落到 0.0.0');
} catch (e) {
  console.error(`[gen-wxs-viewer] WARN 读不到 src/main.cpp（${e.message}），回落到 0.0.0`);
}
function wxVer(v) {
  const s = v.replace(/[^0-9.]/g, '.').replace(/\.+/g, '.').replace(/^\.|\.$/g, '');
  const parts = s.split('.').filter(Boolean);
  while (parts.length < 4) parts.push('0');
  return parts.slice(0, 4).join('.');
}
const WX_VER = wxVer(APP_VER);

// 同一份版本号也喂给 NSIS 安装器（scripts/make-installer.nsi 通过 build/version.nsh 读取），
// 保证 exe 安装包 / msi 安装包 / 应用内版本三处同源。
mkdirSync(join(__dirname, '..', 'build'), { recursive: true });
writeFileSync(
  join(__dirname, '..', 'build', 'version.nsh'),
  `!define VERSION "${WX_VER}"\n!define VERSION_RAW "${APP_VER}"\n`
);
console.error(`[gen-wxs-viewer] 版本 ${APP_VER} → WiX ${WX_VER}（已写 build/version.nsh）`);

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

const ids = new Map([['', 'INSTALLFOLDER']]);
let n = 0;
const nextId = () => `D${++n}`;

function ensureDirId(rel) {
  if (ids.has(rel)) return ids.get(rel);
  const parts = rel.split('/');
  const parentId = ensureDirId(parts.slice(0, -1).join('/'));
  const id = nextId();
  ids.set(rel, id);
  return id;
}

const files = walk(DEPLOY).filter((f) => !f.dir);
// 必须列出**每一层祖先目录**，不能只收"直接装了文件"的目录：
// 中间目录（如 a/b —— 它自己没文件，但 a/b/c 有）若不在 dirs 里，dirXml 就不会输出它，
// 它的子目录 id 也就永远不会被创建 → 组件引用时拿到 undefined →
// wix build 报 WIX0094: The identifier 'Directory:undefined' could not be found。
// （被控端 gen-wxs.mjs 同样只收了最深层，只是目录浅侥幸没触发）
const dirSet = new Set();
for (const f of files) {
  const parts = f.rel.split('/').slice(0, -1);
  for (let i = 1; i <= parts.length; i++) dirSet.add(parts.slice(0, i).join('/'));
}
const dirs = [...dirSet].filter(Boolean);

function dirXml(rel) {
  // ⚠️ 2026-10-04 修的坑（被控端 gen-wxs.mjs 有同样缺陷，只是目录浅没触发）：
  // 原来只按"层级 +1"挑子目录，**没比对父路径前缀** —— 于是 `qml/QtQuick` 和 `Stelarith/qml`
  // 层级都是 2，会被同时当成 `qml` 的子目录，整棵子树被重复输出多次，
  // 最终 wix build 报 WIX0091 Duplicate Directory。管理端 qml 目录下有 1234 个文件、
  // 多层同名目录，一跑就炸。必须同时判断前缀。
  const childDirs = dirs.filter((d) => {
    const parts = d.split('/');
    if (parts.length !== (rel ? rel.split('/').length + 1 : 1)) return false;
    return rel === '' || d.startsWith(rel + '/');
  });
  const name = rel.split('/').pop();
  let s = `    <Directory Id="${ensureDirId(rel)}" Name="${name}">\n`;
  for (const c of childDirs) s += dirXml(c);
  s += `    </Directory>\n`;
  return s;
}

let wxs = `<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">
  <Package
      Name="星集控管理端"
      Manufacturer="Stelarith"
      Version="${WX_VER}"
      UpgradeCode="7A3E9D21-5B84-4F6C-8D10-2E9C4A7B1F53"
      Scope="perMachine"
      Language="2052">
    <MajorUpgrade DowngradeErrorMessage="已经装了更新的版本。" />
    <MediaTemplate EmbedCab="yes" />

    <StandardDirectory Id="ProgramFiles64Folder">
      <Directory Id="INSTALLFOLDER" Name="Stelarith Viewer">
`;

for (const d of dirs.filter((d) => d.split('/').length === 1)) wxs += dirXml(d);

wxs += `      </Directory>
    </StandardDirectory>

    <Feature Id="Main" Title="星集控管理端" Level="1">
`;

let cid = 0;
for (const f of files) {
  const dirRel = f.rel.split('/').slice(0, -1).join('/');
  const dirId = dirRel === '' ? 'INSTALLFOLDER' : ids.get(dirRel);
  const compId = `C${++cid}`;
  wxs += `      <Component Id="${compId}" Directory="${dirId}">\n`;
  wxs += `        <File Source="deploy/${f.rel}" KeyPath="yes" />\n`;
  wxs += `      </Component>\n`;
}

wxs += `    </Feature>
  </Package>
</Wix>
`;

process.stdout.write(wxs);
