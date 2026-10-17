// 星集控 · 沙箱的假云端（T 档验证设施，2026-10-17）
// 目的：让沙箱实例能真正"注册成功"，从而走通 executeAction 主循环 ——
//   文件传输 / 媒体会话 / 对讲频谱 这几类岛状态在真机上是从云端指令来的
//   （file_push / media_session_start / audio.start），没有云端就触发不到；
//   而**不能**连生产云端（会打乱生产设备表，违反"只动自己线内沙箱"）。
// 只实现被控端注册/心跳所需的最小面：register → registered（带 heartbeatMs），心跳忽略。
// ⚠️ 只监听 127.0.0.1。
//
// 编译方式（为什么不用 Add-Type）：
//   pwsh 自带的 Add-Type 默认引用集不含 System.Net.HttpListener / System.Net.WebSockets，
//   PowerShell 反过来加载 BCL 的 dll（Add-Type -Path）也不可靠（实测 CS1069/CS0012 一串）。
//   所以用 dotnet 的 Roslyn（csc）**显式引用运行时目录下的全部 System*.dll** 编成 exe，
//   见 scripts/_sandbox-fakecloud.build.ps1。
using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

public static class FakeCloud {
    static HttpListener http;
    static List<WebSocket> socks = new List<WebSocket>();
    static int frames = 0, registers = 0, viewers = 0, frameCount = 0, badMsg = 0, instrSeq = 0;
    static List<WebSocket> agentSocks = new List<WebSocket>();

    public static string Start(string port) {
        http = new HttpListener();
        http.Prefixes.Add("http://127.0.0.1:" + port + "/");
        http.Start();
        Task.Run(async () => {
            while (http.IsListening) {
                HttpListenerContext ctx;
                try { ctx = await http.GetContextAsync(); } catch { break; }
                if (!ctx.Request.IsWebSocketRequest) { ctx.Response.StatusCode = 400; ctx.Response.Close(); continue; }
                var wsc = await ctx.AcceptWebSocketAsync(null);
                var sock = wsc.WebSocket;
                Log("ws accepted from " + ctx.Request.RemoteEndPoint);
                lock (socks) socks.Add(sock);
                _ = Task.Run(() => Loop(sock));
            }
        });
        return "FAKE-CLOUD on ws://127.0.0.1:" + port + "/";
    }

    static async Task Loop(WebSocket s) {
        var buf = new byte[65536];
        Log("loop start");
        try {
            while (s.State == WebSocketState.Open) {
                // ⚠️ 每条消息都要从头累积：上一版把 MemoryStream 放在 while 外面，帧消息 continue 时
                //    **不清空**（改 continue 前是 m.SetLength(0)，改之后丢了），于是后续每条都被当成"头字节≠1"的
                //    文本去解析，日志里全是 `unknown msg type: `（空）。把缓冲移到循环内，结构上消灭这条路径。
                var m = new MemoryStream();
                WebSocketReceiveResult r;
                do {
                    r = await s.ReceiveAsync(new ArraySegment<byte>(buf), CancellationToken.None);
                    if (r.MessageType == WebSocketMessageType.Close) {
                        Log("close received");
                        try { await s.CloseAsync(WebSocketCloseStatus.NormalClosure, "", CancellationToken.None); } catch { }
                        return;
                    }
                    m.Write(buf, 0, r.Count);
                } while (!r.EndOfMessage);
                var text = Encoding.UTF8.GetString(m.ToArray());
                Interlocked.Increment(ref frames);
                string type = "";
                // 协议 v1 是两条独立 WS 消息：先 [1B ver][2B headerLen][header JSON]，紧接一段**原始 JPEG 帧**。
                // 帧消息的时间戳甚至嵌在二进制里 —— 都不能当 UTF-8 文本反序列化。
                if (m.Position > 0 && m.GetBuffer()[0] == 0x01) {
                    try {
                        int hlen = (m.GetBuffer()[1] << 8) | m.GetBuffer()[2];
                        var head = Encoding.UTF8.GetString(m.GetBuffer(), 3, hlen);
                        using (var d = JsonDocument.Parse(head)) {
                            if (d.RootElement.TryGetProperty("type", out var t)) type = t.GetString();
                        }
                    } catch { type = "binary"; }
                } else {
                    try {
                        using (var d = JsonDocument.Parse(text)) {
                            if (d.RootElement.TryGetProperty("type", out var t)) type = t.GetString();
                        }
                    } catch { }
                }
                if (type == "frame") { Interlocked.Increment(ref frameCount); continue; }
                // 什么都不记的"哑掉"曾经出现过：所以每条非帧消息都记一行
                Log("msg type=" + (type ?? "(null)") + " len=" + m.Length
                    + (m.Length > 0 ? " head=" + m.GetBuffer()[0].ToString("X2") : ""));
                if (string.IsNullOrEmpty(type)) {
                    ++badMsg; continue;
                }
                if (type == "register") {
                    Interlocked.Increment(ref registers);
                    lock (agentSocks) agentSocks.Add(s);
                    Reply(s, "{\"v\":1,\"type\":\"registered\",\"payload\":{"
                          + "\"uid\":\"ste-sbx-t\",\"server\":\"fake-cloud/0.0.1\","
                          + "\"heartbeatMs\":10000,\"onlineCount\":1}}");
                    Log("register received (#" + registers + ")");
                } else if (type == "auth") {
                    Interlocked.Increment(ref viewers);
                    Reply(s, "{\"v\":1,\"type\":\"authed\",\"payload\":{\"mode\":\"token\",\"uid\":null}}");
                    Log("viewer authed (#" + viewers + ")");
                } else if (type == "instruction") {
                    // 被控端**不回** instruction 类型；云端负责下发。测试要把指令送到设备，
                    // 这里直接按 payload 转成被控端认的信封（makeEnvelope 同形：v/type/id/ts/payload）。
                    string action = "", uid = "";
                    try {
                        using (var d = JsonDocument.Parse(text)) {
                            var pl = d.RootElement.GetProperty("payload");
                            action = pl.GetProperty("action").GetString();
                            if (pl.TryGetProperty("uid", out var u)) uid = u.GetString();
                        }
                    } catch { }
                    // ⚠️ id 必须每条唯一（本轮踩的坑）：转发时曾把 id 写成 "fc-" + action，
                    //    于是同一 action 的多条指令（ft / media / spectrum 都是 dev_act）**共用 id**，
                    //    被控端的重复下发保护认 id ⇒ 第 2、3 条被判"已执行过"跳过（日志 result=skipped），
                    //    只有第一条真正生效。保留管理端原始 id；拿不到就按序号兜底，保证唯一。
                    string instrId = "";
                    try {
                        using (var d = JsonDocument.Parse(text)) instrId = d.RootElement.GetProperty("id").GetString();
                    } catch { }
                    if (string.IsNullOrEmpty(instrId)) instrId = "fc-" + (++instrSeq);
                    Log("forward instruction id=" + instrId + " action=" + action + " uid=" + uid);
                    string payload = "{}";
                    try {
                        using (var d = JsonDocument.Parse(text)) payload = d.RootElement.GetProperty("payload").GetRawText();
                    } catch { }
                    string toAgent = "{\"v\":1,\"type\":\"instruction\",\"id\":\"" + instrId + "\",\"ts\":"
                                   + DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + ",\"payload\":" + payload + "}";
                    lock (agentSocks) foreach (var a in agentSocks) Reply(a, toAgent);
                } else if (type == "heartbeat") {
                    // 忽略：云端不超时踢人
                } else {
                    Log("msg type=" + type);
                }
            }
        } catch { }
    }

    static void Reply(WebSocket s, string text) {
        try {
            var b = Encoding.UTF8.GetBytes(text);
            // ⚠️ 不 await 却在**请求上下文之后**才跑 = 发送可能永远不落地（上一版"发出去没影"的嫌疑）。
            //    改成同步等待发送完成再返回，代价是这段线程被占住（消息很小，可接受）。
            s.SendAsync(new ArraySegment<byte>(b), WebSocketMessageType.Text, true, CancellationToken.None)
             .GetAwaiter().GetResult();
            Log("replied " + (text.Length > 80 ? text.Substring(0, 80) : text));
        } catch (Exception e) { Log("reply FAIL " + e.GetType().Name + " " + e.Message); }
    }

    // 日志直写文件（本轮排查用）：Console 输出经重定向后曾经"有内容但看不到/不会刷"，
    // 直接 append 到文件最可靠；路径由环境变量 FAKE_CLOUD_LOG 指定，缺省写到同目录。
    static readonly object logLock = new object();
    static void Log(string s) {
        try {
            var p = Environment.GetEnvironmentVariable("FAKE_CLOUD_LOG");
            if (string.IsNullOrEmpty(p)) p = System.IO.Path.Combine(AppContext.BaseDirectory, "fake-cloud.log");
            lock (logLock) System.IO.File.AppendAllText(p, DateTime.Now.ToString("HH:mm:ss.fff") + " " + s + "\n", Encoding.UTF8);
        } catch { }
    }

    public static string Stats() { return "frames=" + frames + " registers=" + registers + " socks=" + socks.Count; }
    public static void Stop() { try { http.Stop(); } catch { } foreach (var s in socks) { try { s.Abort(); } catch { } } socks.Clear(); }
}

public static class Entry {
    public static void Main(string[] args) {
        string port = args.Length > 0 ? args[0] : "18799";
        Console.OutputEncoding = Encoding.UTF8;
        Console.WriteLine("START " + FakeCloud.Start(port));
        var stop = new ManualResetEventSlim(false);
        Console.CancelKeyPress += (s, e) => { e.Cancel = true; stop.Set(); };
        AppDomain.CurrentDomain.ProcessExit += (s, e) => stop.Set();
        stop.Wait();
        FakeCloud.Stop();
    }
}



