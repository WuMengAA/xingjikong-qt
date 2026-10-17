using System;
using System.Collections.Generic;
using System.IO;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

// 星集控 · 被控端沙箱客户端（C# 辅助）——T 档"旧调用零变化"基线比对用
//
// 为什么是 C# 而不是纯 PowerShell：.NET 的 ReceiveAsync 会在缓冲区满时分帧（EndOfMessage=false），
// PowerShell 里很容易把半截 JSON 当完整消息；取消令牌也难以可靠打断挂起接收（2026-10-17 实测）。
//
// 两条**互不干扰**的连接（本轮实测：在同一对象里复用同一个 socket 会让插件那路被踢）：
//   · ws  —— 插件连接：hello → api（island_show / notify），回包里带 islandSnapshot
//   · vws —— 管理端连接：auth → instruction（dev_act 走这条进 executeAction）
public static class Sb {
    static ClientWebSocket ws = new ClientWebSocket();    // 插件连接（Connect 里重建）
    static string vwsUrl = "";        // 管理端目标（云端地址，ConnectViewer 里设）
    static bool authedUrl = false;    // 是否已完成 auth（Instruct 内部按需重做）
    static int seq = 0;

    // ── 通用收发 ──────────────────────────────────────────────
    static void SendOn(ClientWebSocket c, string s) {
        var b = Encoding.UTF8.GetBytes(s);
        c.SendAsync(new ArraySegment<byte>(b), WebSocketMessageType.Text, true, CancellationToken.None)
         .GetAwaiter().GetResult();
    }
    static string RecvOn(ClientWebSocket c, int ms) {
        var cts = new CancellationTokenSource(ms);
        var m = new MemoryStream();
        var buf = new byte[65536];
        try {
            while (true) {
                var r = c.ReceiveAsync(new ArraySegment<byte>(buf), cts.Token).GetAwaiter().GetResult();
                if (r.MessageType == WebSocketMessageType.Close) return null;
                m.Write(buf, 0, r.Count);
                if (r.EndOfMessage) break;
            }
        } catch { return null; }
        return Encoding.UTF8.GetString(m.ToArray());
    }

    // ── 插件连接 ──────────────────────────────────────────────
    public static string Connect(string port, string id, string name) {
        ws = new ClientWebSocket();
        try { ws.ConnectAsync(new Uri("ws://127.0.0.1:" + port), CancellationToken.None).GetAwaiter().GetResult(); }
        catch (Exception e) { return "CONNECT-FAIL " + e.Message; }
        SendOn(ws, "{\"type\":\"hello\",\"manifest\":{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"version\":\"1.0.0\","
                 + "\"author\":\"sandbox\",\"description\":\"p\",\"minAgentVersion\":\"0.0.0\","
                 + "\"permissions\":[\"island\",\"notify\"],\"runtime\":\"python\",\"entry\":\"x.py\",\"actions\":[]}}");
        return RecvOn(ws, 4000) ?? "NO-WELCOME";
    }
    public static string Call(string api, string argsJson) {
        var id = "c" + (++seq);
        SendOn(ws, "{\"type\":\"api\",\"id\":\"" + id + "\",\"call\":\"" + api + "\",\"args\":" + argsJson + "}");
        return RecvOn(ws, 6000) ?? "__timeout__";
    }

    // ── 管理端连接 ────────────────────────────────────────────
    // 只登记目标地址与 token，不建立长连接：Instruct 内部每条指令各自短连接（见下）。
    public static string ConnectViewer(string port, string token) {
        vwsUrl = "ws://127.0.0.1:" + port;
        return "TARGET " + vwsUrl;
    }
    // 下发一条指令。**每条指令用一条新连接**（发完即弃）——
    // 本轮实测：长连接的第二、三条 dev_act 会因连接已被 Aborted 而 SEND-FAIL，
    // 导致 media/spectrum 那两步实际没执行，基线里看着像"状态没变"。
    // 短连接代价是一条 TCP，但换来"每条指令都真的落地"。
    public static string Instruct(string uid, string action, string paramsJson) {
        // ⚠️ id 要**跨进程唯一**：被控端按 instruction id 做"重复下发保护"（g_lastInstrId），
        //    而 id 只按本进程序号递增 ⇒ 重跑录制脚本时会与上一轮残留同 id（实测第一条被判 skipped）。
        //    加进程级前缀（时间戳+随机）即可避开。
        var id = "p" + DateTime.UtcNow.ToString("HHmmss") + new Random(Environment.TickCount).Next(100, 999) + "-" + (++seq);
        ClientWebSocket c = null;
        try {
            c = new ClientWebSocket();
            c.ConnectAsync(new Uri(vwsUrl), CancellationToken.None).GetAwaiter().GetResult();
            if (!authedUrl) {
                SendOn(c, "{\"v\":1,\"type\":\"auth\",\"payload\":{\"token\":\"dev-cloud-token\"}}");
                RecvOn(c, 1500);   // 收 authed（收不到也不阻塞）
            }
            SendOn(c, "{\"v\":1,\"type\":\"instruction\",\"id\":\"" + id + "\",\"payload\":{\"uid\":\"" + uid
                     + "\",\"action\":\"" + action + "\",\"params\":" + paramsJson + "}}");
            var r = RecvOn(c, 1200);
            return r ?? "sent";
        } catch (Exception e) {
            return "SEND-FAIL " + e.GetType().Name;
        } finally {
            try { if (c != null) c.Abort(); } catch { }
        }
    }

    // ── 工具 ─────────────────────────────────────────────────
    public static string Snap(string replyJson) {
        if (string.IsNullOrEmpty(replyJson) || replyJson == "__timeout__") return null;
        try {
            using (var d = JsonDocument.Parse(replyJson)) {
                if (d.RootElement.TryGetProperty("islandSnapshot", out var v)) return v.GetString();
            }
        } catch { }
        return null;
    }
    public static void Close() {
        try { if (ws != null) ws.Abort(); } catch { }
    }
}
