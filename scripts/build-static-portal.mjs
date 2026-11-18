// 静态集控资源站构建脚本（Cloudflare Pages 交付物）
// 真源：static-portal/manifest.json（白名单 + 口径）；产物：build/static-portal/（gitignore）。
// 纪律：只拷 manifest 声明的可公开文件；单文件 >25 MiB 跳过（CF Pages 上限）；
//       时间戳取 manifest.generatedAt / SOURCE_DATE_EPOCH，保证重复构建 SHA 稳定。
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const MANIFEST = path.join(ROOT, 'static-portal', 'manifest.json');
const OUT = path.join(ROOT, process.env.STE_PORTAL_OUT || 'build/static-portal');
const MAX_BYTES = 25 * 1024 * 1024; // CF Pages 单资产上限（docs 实查 2026-11-18）
const STAMP = process.env.SOURCE_DATE_EPOCH
  ? new Date(Number(process.env.SOURCE_DATE_EPOCH) * 1000).toISOString()
  : (() => { const m = JSON.parse(fs.readFileSync(MANIFEST, 'utf8')); return new Date(m.generatedAt).toISOString(); })();

function sha(buf) { return crypto.createHash('sha256').update(buf).digest('hex'); }
function walk(dir, rel = '') {
  const out = [];
  if (!fs.existsSync(dir)) return out;
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    const r = rel ? rel + '/' + e.name : e.name;
    if (e.isDirectory()) out.push(...walk(p, r)); else out.push({ src: p, rel: r });
  }
  return out;
}

const manifest = JSON.parse(fs.readFileSync(MANIFEST, 'utf8'));
fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(OUT, { recursive: true });

// 1) 白名单 sources：只拷 public 非空者；>25MiB 一律跳过（Pages 单资产上限）
for (const s of manifest.sources) {
  if (!s.public) continue;
  const srcDir = path.join(ROOT, path.dirname(s.source.replace(/\\/g, '/')));
  const exts = ['.ps1', '.zip', '.json', '.exe'];
  for (const f of walk(srcDir)) {
    if (!exts.includes(path.extname(f.rel).toLowerCase())) continue;
    if (/secret|token|\.env/i.test(f.rel)) continue;
    const size = fs.statSync(f.src).size;
    if (size > MAX_BYTES) { console.log('SKIP >25MiB', f.rel, size); continue; }
    const dst = path.join(OUT, s.public, f.rel);
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    fs.copyFileSync(f.src, dst);
  }
}

// 2) qml-patch latest.json（Pages 镜像口径改写 url 到静态路径）
const lj = path.join(OUT, 'qml-patch', 'latest.json');
if (fs.existsSync(lj)) {
  const o = JSON.parse(fs.readFileSync(lj, 'utf8'));
  // 原 url=/api/qml-patch/download（云端动态端点），Pages 镜像改指静态 zip 路径。
  const zip = o.version + '.zip';
  o.url = '/qml-patch/' + zip;
  o.note = 'Pages 静态镜像：仅演示/分发预览，客户端现行仍读 /api/qml-patch/download';
  fs.writeFileSync(lj, JSON.stringify(o, null, 2) + '\n', 'utf8');
}

// 3) index.html
const html = `<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>星集控 · 静态集控资源站</title>
<style>
:root{color-scheme:light dark;--bg:#0e1116;--fg:#e8ecf3;--mut:#9aa7b9;--acc:#2FB8C6;--card:#161c24}
@media (prefers-color-scheme:light){:root{--bg:#f6f8fb;--fg:#121820;--mut:#5a6675;--card:#fff}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.6 system-ui,-apple-system,"Microsoft YaHei",sans-serif}
header{padding:48px 24px 28px;text-align:center;border-bottom:1px solid color-mix(in srgb,var(--fg) 8%,transparent)}
h1{margin:0 0 8px;font-size:28px;letter-spacing:.5px}.sub{color:var(--mut)}
main{max-width:880px;margin:0 auto;padding:28px 20px 60px}
.card{background:var(--card);border-radius:16px;padding:22px;margin:0 0 20px;border:1px solid color-mix(in srgb,var(--fg) 6%,transparent)}
h2{font-size:16px;margin:0 0 14px;color:var(--acc)}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:8px 6px;text-align:left;border-bottom:1px solid color-mix(in srgb,var(--fg) 7%,transparent)}th{color:var(--mut);font-weight:500}
a{color:var(--acc);text-decoration:none}a:hover{text-decoration:underline}
code{font-size:12px;color:var(--mut);word-break:break-all}.note{color:var(--mut);font-size:13px}
</style></head><body>
<header><h1>星集控 · 静态集控资源站</h1><div class="sub">Cloudflare Pages · 纯静态 · 只读 · 无令牌 · 不下发指令</div></header>
<main>
<section class="card"><h2>可下载资源</h2><table id="t"><thead><tr><th>资源</th><th>大小</th><th>SHA-256</th></tr></thead><tbody><tr><td colspan="3" class="note">读取 catalog.json…</td></tr></tbody></table></section>
<section class="card"><h2>口径</h2><p class="note">安装包 36–37 MiB 超过 Cloudflare Pages 单文件 25 MiB 上限，继续由云端 /assets/pkg/ 或 R2 提供；本站只列可公开的装机脚本与 QML 热更包镜像。</p><p class="note">本站不连云端 API、不含设备令牌、不执行任何控制指令；设备绑定与下发仍走 wss://control.245959623.xyz。</p></section>
</main>
<script>
fetch('catalog.json').then(r=>r.json()).then(d=>{
 const b=document.querySelector('#t tbody');b.innerHTML='';
 (d.files||[]).forEach(f=>{const tr=document.createElement('tr');
 const a=document.createElement('a');a.href=encodeURI(f.path);a.textContent=f.path;
 const td=document.createElement('td');td.appendChild(a);
 const sz=document.createElement('td');sz.textContent=(f.size/1048576).toFixed(2)+' MiB';
 const sh=document.createElement('td');sh.innerHTML='<code>'+f.sha256.slice(0,16)+'…</code>';sh.title=f.sha256;
 tr.append(td,sz,sh);b.appendChild(tr);});
}).catch(e=>document.querySelector('#t tbody').innerHTML='<tr><td colspan=3 class=note>catalog.json 读取失败</td></tr>');
</script></body></html>
`;
fs.writeFileSync(path.join(OUT, 'index.html'), html, 'utf8');

// 4) catalog.json（稳定时间戳）
const files = walk(OUT).filter(f => f.rel !== 'catalog.json').map(f => ({
  path: f.rel.replace(/\\/g, '/'), size: fs.statSync(f.src).size,
  sha256: sha(fs.readFileSync(f.src)),
})).sort((a, b) => a.path.localeCompare(b.path));
fs.writeFileSync(path.join(OUT, 'catalog.json'), JSON.stringify({
  kind: 'stelarith.static-portal.catalog', schema: 1, generatedAt: STAMP, files,
}, null, 2) + '\n', 'utf8');

console.log('STATIC_PORTAL_OK files=' + files.length + ' bytes=' + files.reduce((a, f) => a + f.size, 0));
for (const f of files) console.log('  ' + f.path + ' ' + f.size + ' ' + f.sha256.slice(0, 12));
