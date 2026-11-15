// 单渠道 OTA 回归（2026-11-16）：不连服务、不改生产清单，只在临时目录里造一份构建表。
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';

const dir = mkdtempSync(path.join(tmpdir(), 'stelarith-ota-unified-'));
process.env.CLOUD_OTA_MANIFEST_FILE = path.join(dir, 'ota.json');
const OTA = await import(pathToFileURL(path.join(process.cwd(), 'src/ota.js')).href);
const { OTA_CHANNELS_UNIFIED, unifiedChannel, manifestFileFor, pickUnifiedBuild, latestFor } = OTA;
let pass=0, fail=0;
function ok(name, cond, extra='') { if(cond){pass++; console.log('PASS '+name);} else {fail++; console.error('FAIL '+name+' '+extra);} }
ok('单一渠道开关', OTA_CHANNELS_UNIFIED === true);
ok('归一渠道 stable', unifiedChannel() === 'stable');
for (const ch of ['', 'stable', 'alpha', 'gray', 'beta', 'rc', '../secret', 'weird'])
  ok('任意 channel 都落回唯一清单: '+ch, manifestFileFor(ch) === process.env.CLOUD_OTA_MANIFEST_FILE, manifestFileFor(ch));
const reg = { builds: {
  a: {product:'agent',version:'0.6.70-alpha.1',url:'https://x/a.exe',sha256:'a'.repeat(64),size:1,publishedAt:'2026-11-15T00:00:00.000Z'},
  b: {product:'agent',version:'0.6.70',url:'https://x/b.exe',sha256:'b'.repeat(64),size:2,publishedAt:'2026-11-15T01:00:00.000Z'},
  c: {product:'agent',version:'0.6.72',url:'https://x/c.exe',sha256:'c'.repeat(64),size:3,publishedAt:'2026-11-16T00:00:00.000Z'},
  v: {product:'viewer',version:'9.9.9',url:'https://x/v.exe',sha256:'d'.repeat(64),size:4,publishedAt:'2026-11-16T00:00:00.000Z'}
}};
ok('按版本序挑最新而非按时间晋升', pickUnifiedBuild(reg,'agent',Date.parse('2026-11-16T00:01:00Z'))?.version === '0.6.72');
ok('预览构建不被 Product 串台', pickUnifiedBuild(reg,'viewer').version === '9.9.9');
writeFileSync(path.join(dir,'ota-builds.json'), JSON.stringify(reg));
const now = Date.parse('2026-11-16T00:01:00Z');
for (const ch of ['alpha','gray','beta','rc','stable','']) {
  const e = latestFor('agent', ch, now);
  ok('旧档 '+ch+' 与 stable 返回同一版', e && e.version === '0.6.72' && e.channel === 'stable' && e.unifiedChannels === true && e.appearAfterMs === 0, JSON.stringify(e));
}
const e2 = latestFor('agent', 'alpha', Date.parse('2026-11-15T00:01:00Z'));
ok('发布时间早于候选版也不再等 24h', e2 && e2.version === '0.6.72', JSON.stringify(e2));
writeFileSync(process.env.CLOUD_OTA_MANIFEST_FILE, JSON.stringify({products:{agent:{version:'0.6.70',url:'https://x/stable.exe',sha256:'e'.repeat(64),size:9,notes:'',mandatory:false}}}));
rmSync(path.join(dir,'ota-builds.json'));
const e3 = latestFor('agent','gray',now);
ok('无构建表时旧 channel 也读唯一清单', e3 && e3.version === '0.6.70', JSON.stringify(e3));
console.log('PASS '+pass+' / FAIL '+fail);
rmSync(dir,{recursive:true,force:true});
if(fail) process.exit(1);
