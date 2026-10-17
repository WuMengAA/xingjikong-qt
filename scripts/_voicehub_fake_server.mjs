// 假 VoiceHub 服务（#329 探针夹具 · 2026-10-17）
// ───────────────────────────────────────────────────────────────────────────
// 为什么需要它：VoiceHub 生产服务（voicehub.245959623.xyz）当前无广播、
// 且不能往生产写状态；本地又没起服务端（voicehub-fork 只剩旧版参考）。
// 本文件用 Node 内置 http 复刻**被测契约的那一面**（匿名只读）：
//   · GET  /api/music/broadcast   → 快照 {broadcast,nextUp,listeners,baselineReleased,serverTime}
//   · GET  /api/music/websocket   → SSE：建连即 connection_established + broadcast_state，
//                                   之后广播状态变化推送；每 15s 发注释行心跳（测注释忽略）
//   · POST /__ctl                 → 脚本控制：state / stop / kill（掐 SSE）/ sse404|sseok /
//                                   hold（轮询挂起不响应，测网络错路径）
//   · GET  /__ctl/stats           → {sseConnects, sseCloses, polls}（探针断言论据）
// 契约口径来源：2026-10-17 实测线上 + Stelarith-voicehub-component 的
// BroadcastStateClient.cs / AI-必读-广播正在播放-桥接提示词.md。
//
// 用法：node scripts/_voicehub_fake_server.mjs <port>
// 退出：Ctrl+C（探针驱动负责拉起与回收）。

import http from "node:http";
import process from "node:process";

const port = Number(process.argv[2] || 8791);

// ── 脚本状态 ────────────────────────────────────────────────────────────────
const state = {
    broadcast: null,      // BroadcastState | null
    nextUp: null,
    listeners: 0,
    baselineReleased: false,
};
const flags = { sseDisabled: false, holdPoll: false };
const stats = { sseConnects: 0, sseCloses: 0, polls: 0 };
const sseClients = new Set();   // { res, timer }

function snapshot() {
    return {
        broadcast: state.broadcast,
        nextUp: state.nextUp,
        listeners: state.listeners,
        baselineReleased: state.baselineReleased,
        serverTime: Date.now(),
    };
}

function sseFrame(obj) {
    return `data: ${JSON.stringify(obj)}\n\n`;
}

// SSE 的 broadcast_state 载荷形状（与桥接文档同口径）：
// broadcast 对象上**挂着** nextUp / listeners / baselineReleased（"同一个对象上"），
// 停播时为 null。轮询端点则是这三者与 broadcast 平级——两种形状被测端都要认。
function broadcastPayload() {
    if (!state.broadcast) return null;
    return {
        ...state.broadcast,
        nextUp: state.nextUp,
        listeners: state.listeners,
        baselineReleased: state.baselineReleased,
    };
}

function pushState() {
    const frame = sseFrame({ type: "broadcast_state", data: broadcastPayload() });
    for (const c of sseClients) {
        try { c.res.write(frame); } catch { /* 死连接由 close 清理 */ }
    }
}

function readBody(req) {
    return new Promise((resolve) => {
        let buf = "";
        req.on("data", (c) => { buf += c; });
        req.on("end", () => {
            try { resolve(JSON.parse(buf || "{}")); } catch { resolve({}); }
        });
    });
}

const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, "http://127.0.0.1");

    // ── 被测端点：轮询快照 ──
    if (url.pathname === "/api/music/broadcast" && req.method === "GET") {
        stats.polls += 1;
        if (flags.holdPoll) return;            // 挂起不响应（探针测网络错/过期路径）
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify(snapshot()));
        return;
    }

    // ── 被测端点：SSE ──
    if (url.pathname === "/api/music/websocket" && req.method === "GET") {
        if (flags.sseDisabled) {
            res.writeHead(404, { "Content-Type": "application/json" });
            res.end(JSON.stringify({ error: "sse disabled by ctl" }));
            return;
        }
        stats.sseConnects += 1;
        res.writeHead(200, {
            "Content-Type": "text/event-stream",
            "Cache-Control": "no-cache",
            Connection: "keep-alive",
            "X-Accel-Buffering": "no",
        });
        // 建连即快照（线上同款：connection_established + broadcast_state）
        res.write(sseFrame({
            type: "connection_established",
            data: { connectionId: `fake_${Date.now()}`, userId: null, timestamp: Date.now() },
        }));
        res.write(sseFrame({ type: "broadcast_state", data: broadcastPayload() }));
        const client = { res, timer: null };
        // 15s 注释行心跳：验证客户端忽略 ':' 开头的行（不当帧解析）
        client.timer = setInterval(() => {
            try { res.write(": ping\n\n"); } catch { /* close 会清理 */ }
        }, 15000);
        sseClients.add(client);
        req.on("close", () => {
            clearInterval(client.timer);
            sseClients.delete(client);
            stats.sseCloses += 1;
        });
        return;
    }

    // ── 控制面（探针专用，不是契约）──
    if (url.pathname === "/__ctl" && req.method === "POST") {
        const body = await readBody(req);
        switch (body.op) {
            case "state":
                state.broadcast = body.broadcast ?? null;
                state.nextUp = body.nextUp ?? null;
                state.listeners = body.listeners ?? 0;
                state.baselineReleased = body.baselineReleased ?? false;
                pushState();
                break;
            case "stop":
                state.broadcast = null;
                state.nextUp = null;
                state.listeners = 0;
                pushState();
                break;
            case "kill":
                for (const c of [...sseClients]) {
                    clearInterval(c.timer);
                    try { c.res.destroy(); } catch { /* 已断 */ }
                    sseClients.delete(c);
                    stats.sseCloses += 1;
                }
                break;
            case "sse404":
                flags.sseDisabled = true;
                break;
            case "sseok":
                flags.sseDisabled = false;
                break;
            case "hold":
                flags.holdPoll = true;
                break;
            case "unhold":
                flags.holdPoll = false;
                break;
            default:
                res.writeHead(400);
                res.end(JSON.stringify({ error: "unknown op" }));
                return;
        }
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ ok: true, ...snapshot() }));
        return;
    }
    if (url.pathname === "/__ctl/stats" && req.method === "GET") {
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify(stats));
        return;
    }

    res.writeHead(404, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: "not found" }));
});

server.listen(port, "127.0.0.1", () => {
    console.log(`[fake-voicehub] listening on http://127.0.0.1:${port}`);
});
