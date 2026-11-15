// 实例自治契约单测（2026-11-15）
import { readFileSync } from 'node:fs';
import {
  SOVEREIGNTY_CONTRACT, FORBIDDEN_CROSS_CLOUD_KEYS, resolvePublicBase, wsUrlOf,
  provisionReadiness, forbiddenConfigured, isVendorBase, foreignBaseConfigured, instanceSummary,
} from '../src/instance.js';

let pass = 0, fail = 0;
function ok(name, cond) { if (cond) { pass++; } else { fail++; console.error('FAIL  ' + name); } }

ok('契约：跨云端控制为 false', SOVEREIGNTY_CONTRACT.crossCloudControl === false);
ok('契约：带版本号', SOVEREIGNTY_CONTRACT.version === 1);
ok('不配地址 ⇒ 空串（不回落）', resolvePublicBase({}) === '');
ok('去尾斜杠', resolvePublicBase({ CLOUD_PUBLIC_BASE: 'https://cloud.school-a.edu.cn/' }) === 'https://cloud.school-a.edu.cn');
ok('带路径的地址拒绝', resolvePublicBase({ CLOUD_PUBLIC_BASE: 'https://x/y' }) === '');
ok('非 http(s) 拒绝', resolvePublicBase({ CLOUD_PUBLIC_BASE: 'ftp://x' }) === '');
ok('https ⇒ wss', wsUrlOf('https://cloud.school-a.edu.cn', '/ws/agent') === 'wss://cloud.school-a.edu.cn/ws/agent');
ok('http ⇒ ws 且保留端口', wsUrlOf('http://127.0.0.1:8788', '/ws/viewer') === 'ws://127.0.0.1:8788/ws/viewer');
ok('空地址 ⇒ 空 WS', wsUrlOf('', '/ws/agent') === '');
const r0 = provisionReadiness({});
ok('未配置 ⇒ 装机不就绪', r0.ok === false && r0.agentWsUrl === '' && r0.viewerWsUrl === '' && r0.reason.length > 0);
const r1 = provisionReadiness({ CLOUD_PUBLIC_BASE: 'https://cloud.school-a.edu.cn' });
ok('配置后 ⇒ 双端 WS 都派生自本实例', r1.ok === true && r1.agentWsUrl === 'wss://cloud.school-a.edu.cn/ws/agent' && r1.viewerWsUrl === 'wss://cloud.school-a.edu.cn/ws/viewer');
ok('禁用键名单含中心令牌', FORBIDDEN_CROSS_CLOUD_KEYS.includes('CLOUD_CENTRAL_TOKEN'));
ok('只报真正配置的禁用键', JSON.stringify(forbiddenConfigured({ CLOUD_CENTRAL_TOKEN: 'x', CLOUD_WS_TOKEN: 'ok' })) === JSON.stringify(['CLOUD_CENTRAL_TOKEN']));
ok('识别官方域名', isVendorBase('https://control.245959623.xyz') === true);
ok('不误判学校自有域名', isVendorBase('https://cloud.school-a.edu.cn') === false);
ok('严格判据看配置', foreignBaseConfigured({ CLOUD_PUBLIC_BASE: 'https://control.245959623.xyz' }) === true && foreignBaseConfigured({ CLOUD_PUBLIC_BASE: 'https://cloud.school-a.edu.cn' }) === false);
const sum = instanceSummary({ CLOUD_PUBLIC_BASE: 'https://control.245959623.xyz', STE_UPSTREAM_CLOUD_URL: 'x' });
ok('汇总带 vendorBase / 禁用键 / 契约', sum.crossCloudControl === false && sum.vendorBase === true && sum.forbiddenConfigured.length === 1);
const src = readFileSync(new URL('../src/provision.js', import.meta.url), 'utf8');
ok('provision.js 不再硬编码官方域名', !src.includes('control.245959623.xyz'));
ok('provision.js 改用自治真源', src.includes("from './instance.js'") && src.includes('resolvePublicBase()') && src.includes('wsUrlOf('));
const envExample = readFileSync(new URL('../.env.example', import.meta.url), 'utf8');
ok('.env.example 声明本实例地址', envExample.includes('CLOUD_PUBLIC_BASE='));

console.log('PASS ' + pass + ' / FAIL ' + fail);
if (fail) process.exit(1);
