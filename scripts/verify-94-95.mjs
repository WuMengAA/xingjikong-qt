// #94 / #95 的量化回归（2026-10-07）
//
// 这两条都是"肉眼看才看出来、或根本看不见"的问题，所以不靠截图下结论，靠两个数：
//   ① #94 通知弹窗输入框的**对比度**（WCAG 相对亮度，算出来的，不是看出来的）；
//   ② #95 远程终端状态机的**转移推演**（把 ViewerBackend 那套 Pending/Open/Idle 复刻一遍，
//      断言每一条该进的进、该丢的丢、该复位的复位）。
//
// 对照的是修前修后两套：
//   修前 = 自画 Rectangle + 裸 TextInput（只给 color、没给 background ⇒ 吃系统调色板白底）
//   修后 = 统一走 InputField（底色透明，实际底色＝弹窗 cream）

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
let pass = 0, fail = 0;
function ok(cond, msg, extra = '') {
    if (cond) { pass++; console.log('  PASS  ' + msg + (extra ? '  ' + extra : '')); }
    else { fail++; console.log('  FAIL  ' + msg + '  ' + extra); }
}

// ── 0. 先从源码里把两套主题抠出来，别写死（写死就会和界面脱节）────────────
function grabTheme(file, name) {
    const src = readFileSync(file, 'utf8');
    const i = src.indexOf(name);
    if (i < 0) throw new Error('找不到主题 ' + name);
    const start = src.indexOf('{', i);
    let depth = 0, end = -1;
    for (let k = start; k < src.length; ++k) {
        if (src[k] === '{') depth++;
        else if (src[k] === '}') { depth--; if (depth === 0) { end = k; break; } }
    }
    const body = src.slice(start + 1, end);
    const out = {};
    for (const m of body.matchAll(/(\w+):\s*"(#[0-9A-Fa-f]{6})"/g)) out[m[1]] = m[2];
    return out;
}
const dark = grabTheme(join(ROOT, 'qml/Main.qml'), 'darkTh');
const light = grabTheme(join(ROOT, 'qml/Main.qml'), 'lightTh');

// ── 1. WCAG 对比度 ────────────────────────────────────────────────────────
const srgb = (c) => { c /= 255; return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); };
function lum(hex) {
    const r = parseInt(hex.slice(1, 3), 16), g = parseInt(hex.slice(3, 5), 16), b = parseInt(hex.slice(5, 7), 16);
    return 0.2126 * srgb(r) + 0.7152 * srgb(g) + 0.0722 * srgb(b);
}
function ratio(a, b) {
    const [x, y] = [lum(a), lum(b)].sort((p, q) => q - p);
    return (x + 0.05) / (y + 0.05);
}

console.log('\n#94 通知弹窗输入框对比度（正文线 WCAG AA ≥ 4.5:1）');
// 修前：QQC2 TextInput 默认 background = palette.base（Windows 恒白），字色 = theme.fg
const beforeDark = ratio('#FFFFFF', dark.fg);
const beforeLight = ratio('#FFFFFF', light.fg);
console.log(`  修前 暗色：白底 #FFFFFF × 字 ${dark.fg} = ${beforeDark.toFixed(2)}:1`);
console.log(`  修前 亮色：白底 #FFFFFF × 字 ${light.fg} = ${beforeLight.toFixed(2)}:1`);
ok(beforeDark < 1.2, '修前 暗色输入框白底白字（≈1:1，用户说的"打不了字"就是这个）', `${beforeDark.toFixed(2)}:1`);
// 修后：InputField 底色 transparent ⇒ 实际底色是弹窗的 cream
const afterDark = ratio(dark.cream, dark.fg);
const afterLight = ratio(light.cream, light.fg);
console.log(`  修后 暗色：底 ${dark.cream} × 字 ${dark.fg} = ${afterDark.toFixed(2)}:1`);
console.log(`  修后 亮色：底 ${light.cream} × 字 ${light.fg} = ${afterLight.toFixed(2)}:1`);
ok(afterDark >= 4.5, '修后 暗色达标 AA', `${afterDark.toFixed(2)}:1`);
ok(afterLight >= 4.5, '修后 亮色达标 AA', `${afterLight.toFixed(2)}:1`);

// ── 2. 终端状态机推演（复刻 viewerbackend.cpp 的 TermIdle/Pending/Open）──────
console.log('\n#95 终端状态机');
const Idle = 0, Pending = 1, Open = 2;
// 复刻新版：terminal_opened 对 Pending 放行；两条兜底定时器到点回 Idle
class Term {
    constructor(waitMs = 20000, closeMs = 2500) {
        this.state = Idle; this.sid = ''; this.note = '';
        this.waitMs = waitMs; this.closeMs = closeMs; this.sent = []; this.dropped = 0;
        this.closing = false;   // 对应 m_termClosing：关断请求已下发、还没拿到 terminal_closed
    }
    open() {
        if (this.state !== Idle) return 'refuse';           // termOpen() 里的守卫
        this.sid = 'v' + (++this.seq || (this.seq = 1));
        this.state = Pending; this.note = '等本机点头…'; return 'sent';
    }
    onOpened(sid) {
        // 新版判据：Idle 或 sid 串台才丢；Pending / Open 都收
        if (this.state === Idle || sid !== this.sid) { this.dropped++; return 'drop'; }
        this.closing = false;
        this.state = Open; this.note = ''; return 'open';
    }
    onClosed(sid, reason) { if (sid && sid !== this.sid) return 'drop'; this.closing = false; this.state = Idle; this.note = reason || ''; return 'closed'; }
    close() {
        if (this.state === Idle) return 'skip';
        if (this.closing) return 'dup';           // C++ 侧的去重（界面那边也禁了 Pending 态按钮）
        this.closing = true;
        this.sent.push('terminal_close'); this.state = Pending; this.note = '正在关…'; return 'sent';
    }
    // 两条兜底定时器：只有还停在 Pending 才复位
    tickWaitMs(t) {
        if (this.state === Pending && t >= this.waitMs) { this.closing = false; this.state = Idle; this.note = '等本机点头超时（本机没允许，会话没开）'; return true; }
        return false;
    }
    tickCloseMs(t) {
        if (this.state === Pending && t >= this.closeMs) { this.closing = false; this.state = Idle; this.note = '关断没回话（按已关处理，可以重开）'; return true; }
        return false;
    }
}

// 2.1 旧判据（`m_termState != TermOpen` 就丢）会把 Pending 下的 terminal_opened 扔光
{
    const oldRule = (st) => st !== Open ? 'drop' : 'open';
    ok(oldRule(Pending) === 'drop', '旧判据：Pending 下 terminal_opened 被丢（#95「连不上」根因）');
}
// 2.2 新判据：Pending 能进 Open
{
    const t = new Term();
    t.open();
    ok(t.state === Pending, '点「开终端」→ Pending（等本机允许）');
    ok(t.onOpened(t.sid) === 'open', '新版：收到的 terminal_opened 被采纳 → Open');
    ok(t.state === Open, '状态真的到 Open，输入框可用');
}
// 2.3 重复/重放的 terminal_opened 不会把 Open 打回 Pending
{
    const t = new Term(); t.open(); t.onOpened(t.sid);
    ok(t.onOpened(t.sid) === 'open' && t.state === Open, '重复 terminal_opened 幂等（仍 Open）');
}
// 2.4 串台 sid 仍然丢（不能糊进当前会话）
{
    const t = new Term(); t.open();
    ok(t.onOpened('v999') === 'drop' && t.state === Pending, '别的会话的 terminal_opened 被丢（不串台）');
}
// 2.5 等本机点头超时 → 回 Idle（能重开）
{
    const t = new Term(); t.open();
    ok(t.tickWaitMs(19999) === false && t.state === Pending, '19.999 秒还没到点，仍 Pending');
    ok(t.tickWaitMs(20000) === true && t.state === Idle, '20 秒超时 → Idle（可再开）');
    ok(t.onOpened(t.sid) === 'drop', '超时后迟到的 terminal_opened 不会偷偷把它拉成 Open');
    ok(t.open() === 'sent', '超时复位后可以重新发起终端');
}
// 2.6 关断：回 terminal_closed → Idle
{
    const t = new Term(); t.open(); t.onOpened(t.sid);
    ok(t.close() === 'sent' && t.state === Pending, '点「关终端」→ 本地先收摊为 Pending');
    ok(t.onClosed(t.sid, '本机拒绝') === 'closed' && t.state === Idle, '被控端回 terminal_closed → Idle');
}
// 2.7 关断不回话：2.5 秒兜底 → 仍 Idle（否则这台永远开不了新终端）
{
    const t = new Term(); t.open(); t.onOpened(t.sid);
    t.close();
    ok(t.state === Pending, '刚点关闭，还在等回话');
    ok(t.tickCloseMs(2400) === false, '2.4 秒没到点，仍 Pending');
    ok(t.tickCloseMs(2500) === true && t.state === Idle, '2.5 秒兜底 → Idle（不再卡死）');
    ok(t.open() === 'sent' && t.onOpened(t.sid) === 'open', '关断超时后仍能重新开一条终端');
}
// 2.8 关断中重复点按钮不会重复发指令（界面已禁用，这里验证状态层面）
{
    const t = new Term(); t.open(); t.onOpened(t.sid);
    t.close(); t.close(); t.close();
    ok(t.sent.filter((x) => x === 'terminal_close').length === 1, '连点关闭只发一条 terminal_close（界面已禁用 Pending 态按钮）');
}

console.log(`\n结果：${pass} 项通过 / ${fail} 项失败`);
process.exit(fail === 0 ? 0 : 1);
