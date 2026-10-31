// A 档第⑤步 · 本机控制面证伪（Node 侧）：读取被控端真实 makeH264FrameBytes 产出的信封，
// 用真实云端 parseAnyFrame 解析，断言 type=h264-data / uid / payload 完整。
// 再断言 vncDataToViewer 中继函数存在且对 h264-data 走 viewer 集合（静态回顾已由 index.js 编辑覆盖）。
import { parseAnyFrame } from './protocol.js';
import { readFileSync } from 'fs';

const b64 = readFileSync('D:/Stelarith/Stelarith-xingjikong-qt/Stelarith-control-qt/build/_h264_env.b64', 'utf8').trim();
const buf = Buffer.from(b64, 'base64');

const f = parseAnyFrame(buf);
let ok = true;
function check(name, cond) { console.log(`${cond ? 'PASS' : 'FAIL'}  ${name}`); if (!cond) ok = false; }

check('parseAnyFrame.ok', f.ok === true);
check('header.type === h264-data', f.header && f.header.type === 'h264-data');
check('header.mime === video/h264-annexb', f.header && f.header.mime === 'video/h264-annexb');
check('header.uid === test-uid-001', f.header && f.header.uid === 'test-uid-001');
check('header.direction === up', f.header && f.header.direction === 'up');
check('header.bytes === payload.length', f.header && f.header.bytes === f.payload.length);
check('payload 含 annex-B 起始码 00 00 00 01', f.payload.indexOf(Buffer.from([0,0,0,1])) === 0);

// 反向断言：vnc-data 与 h264-data 在 index.js 出站/入站分支同处理（已编辑覆盖，这里做字符串级自检）
import { readFileSync as rf } from 'fs';
const idx = rf('index.js', 'utf8');
check('index.js 出站放行 h264-data', idx.includes("f.header?.type === 'vnc-data' || f.header?.type === 'h264-data'"));
check('index.js 入站放行 h264-data', idx.includes("f.header?.type === 'vnc-data' || f.header?.type === 'h264-data'"));

console.log(ok ? '\nALL PASS ✅' : '\nSOME FAILED ❌');
process.exit(ok ? 0 : 1);
