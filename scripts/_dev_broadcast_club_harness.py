#!/usr/bin/env python3
# 广播社成员界面 · 本地实弹 harness（离线可用，不碰生产 8788，不依赖停机中的真 VoiceHub）
#
# 用途：在真 VoiceHub 停机（402）期间，仍可在本机完整验证 control-qt「广播社」页的 GUI 流程：
#   登录 → 看排期 → 拖拽改顺序 → 松手保存 → 登出/令牌失效回登录 → 越权防护观感。
#
# 它做四件事：
#   ① 启动前先清掉占用目标端口的残留进程（避免上次那种“端口被占→卡死”）；
#   ② 起一个忠实于 broadcast-club.js 契约的 mock VoiceHub（GET /api/songs/public + POST /api/open/schedules/reorder）；
#   ③ 起一个隔离云端实例（独立端口 + 所有数据文件指向临时目录，绝不写进仓库）；
#   ④ 用管理面静态令牌自动种一个广播社成员，并【自动拉起】已构建的 agent 面板，直接指向本地云。
#
# 用法（一条命令即可，双击也能跑）：
#   python scripts/_dev_broadcast_club_harness.py
#   python scripts/_dev_broadcast_club_harness.py --no-exe        # 只起云，面板自己手动起
#   python scripts/_dev_broadcast_club_harness.py --smoke        # 自证模式：起云+种账号+登录/读排期/reorder 后自动退出（不拉面板）
#
# 退出：Ctrl-C 会同时停掉云端、mock VH 和面板进程。
import argparse, subprocess, sys, os, time, tempfile, json, signal, threading, socket
import http.server

CLOUD_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEF_EXE = os.path.normpath(os.path.join(
    CLOUD_DIR, "..", "Stelarith-control-qt", "build", "stelarith-agent-qt.exe"))

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--cloud-port", type=int, default=0,
                   help="云端端口，0=自动挑空闲端口（默认）")
    p.add_argument("--vh-port", type=int, default=0,
                   help="mock VoiceHub 端口，0=自动挑空闲端口（默认）")
    p.add_argument("--admin-token", default="local-dev-token")
    p.add_argument("--secret", default="local-dev-secret-0123456789")
    p.add_argument("--vh-key", default="vhub_dev_aaaa0000bbbb1111cccc2222")
    p.add_argument("--member", default="radioclub1")
    p.add_argument("--password", default="broadcast123")
    p.add_argument("--station", default="class_xlzx_2028_08")
    p.add_argument("--exe", default=DEF_EXE,
                   help="要拉起的 agent 面板 exe 路径")
    p.add_argument("--no-exe", action="store_true",
                   help="不自动拉起面板，仅起云端（自己手动起 exe）")
    p.add_argument("--smoke", action="store_true",
                   help="自证模式：起云+种账号+登录/读排期/reorder 自检后自动退出")
    p.add_argument("--standin", dest="standin", action="store_true", default=True,
                   help="使用独立 Node 本地 VoiceHub 替身（默认，走真实 HTTP 代理路径）")
    p.add_argument("--no-standin", dest="standin", action="store_false",
                   help="改用进程内内存 mock VoiceHub（离线 CI 用，不依赖 node）")
    return p.parse_args()

# ── 端口工具：自动挑空闲端口 + 清掉占用者 ──────────────────────────────────────
def pick_free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port

def kill_port_owners(port):
    """Windows: netstat 找 LISTENING 的 pid，taskkill /F /T 清掉。失败静默（端口本就是 OS 分配的空闲口，不影响）。"""
    try:
        exe = r"C:\Windows\System32\netstat.exe"
        if not os.path.exists(exe):
            exe = "netstat"
        r = subprocess.run([exe, "-ano"], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=15)
        out = (r.stdout or b"").decode("utf-8", "replace")
    except Exception as e:
        print(f"  [cleanup] netstat 不可用，跳过清理: {e}")
        return
    pids = set()
    for line in out.splitlines():
        if f":{port} " in line and "LISTENING" in line:
            parts = line.split()
            if parts:
                pids.add(parts[-1])
    for pid in pids:
        try:
            subprocess.run(["taskkill", "/PID", pid, "/F", "/T"],
                           capture_output=True, timeout=15)
            print(f"  [cleanup] 已强杀占用端口 {port} 的残留进程 pid={pid}")
        except Exception:
            pass

# ── mock VoiceHub ──────────────────────────────────────────────────────────────
class _VHHandler(http.server.BaseHTTPRequestHandler):
    items = []
    api_key = ""
    reorder_log = []
    def log_message(self, *a): pass
    def _send(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("content-type", "application/json; charset=utf-8")
        self.send_header("content-length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_GET(self):
        if self.path == "/api/songs/public":
            self._send(200, _VHHandler.items)
        else:
            self._send(404, {"error": "not found"})
    def do_POST(self):
        if self.path == "/api/open/schedules/reorder":
            got_key = self.headers.get("x-api-key")
            length = int(self.headers.get("content-length", 0) or 0)
            raw = self.rfile.read(length) if length else b"{}"
            try:
                payload = json.loads(raw.decode("utf-8", "replace"))
            except Exception:
                payload = {}
            if got_key != _VHHandler.api_key:
                self._send(401, {"ok": False, "message": "bad key"})
                return
            by_id = {it["id"]: it for it in _VHHandler.items}
            applied = []
            for s in payload.get("schedules", []):
                it = by_id.get(s.get("id"))
                if it is not None:
                    it["sequence"] = s.get("sequence")
                    applied.append({"id": it["id"], "sequence": it["sequence"]})
            _VHHandler.reorder_log.append({"ts": time.time(),
                                           "got_key_ok": got_key == _VHHandler.api_key,
                                           "applied": applied})
            print(f"  [mockVH] reorder 收到 x-api-key={'OK' if got_key == _VHHandler.api_key else 'BAD'} 应用={applied}", flush=True)
            self._send(200, {"ok": True, "message": "saved", "applied": applied})
        else:
            self._send(404, {"error": "not found"})

def seed_schedules():
    return [
        {"id": 1, "sequence": 10, "title": "晨间新闻",   "artist": "广播社", "station": "class_xlzx_2028_08", "played": True,  "isPlaying": False, "durationSeconds": 180, "musicPlatform": "local", "musicId": "m1", "songId": "s1", "broadcastPosition": 1, "playDate": "2026-10-23"},
        {"id": 2, "sequence": 20, "title": "午间点歌",   "artist": "各班投稿", "station": "class_xlzx_2028_08", "played": False, "isPlaying": True,  "durationSeconds": 240, "musicPlatform": "local", "musicId": "m2", "songId": "s2", "broadcastPosition": 2, "playDate": "2026-10-23"},
        {"id": 3, "sequence": 30, "title": "放学音乐",   "artist": "广播社", "station": "class_xlzx_2028_08", "played": False, "isPlaying": False, "durationSeconds": 210, "musicPlatform": "local", "musicId": "m3", "songId": "s3", "broadcastPosition": 3, "playDate": "2026-10-23"},
    ]

def start_mock_vh(port, key):
    _VHHandler.items = seed_schedules()
    _VHHandler.api_key = key
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), _VHHandler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv

def start_standin_node(port, key):
    """起独立 Node 本地 VoiceHub 替身（scripts/_local_voicehub_standin.js），走真实 HTTP 代理路径。"""
    env = dict(os.environ)
    env["STE_VH_STANDIN_PORT"] = str(port)
    env["STE_VOICEHUB_WRITE_KEY"] = key
    try:
        proc = subprocess.Popen([which_node(), "scripts/_local_voicehub_standin.js"],
                                cwd=CLOUD_DIR, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return proc
    except Exception as e:
        print(f"[FAIL] 启动本地替身失败: {e}")
        return None

def build_env(args, mock_port, tmp, cloud_port):
    e = dict(os.environ)
    e.update({
        "CLOUD_WS_PORT": str(cloud_port),
        "CLOUD_WS_HOST": "127.0.0.1",
        "CLOUD_WS_TOKEN": args.admin_token,
        "CLOUD_VIEWER_TOKEN": args.admin_token,
        "CLOUD_VIEWER_SECRET": args.secret,
        "VOICEHUB_BASE_URL": "http://127.0.0.1:9",   # 禁用只读桥轮询，避免打生产
        "STE_VOICEHUB_WRITE_BASE": f"http://127.0.0.1:{mock_port}",
        "STE_VOICEHUB_WRITE_KEY": args.vh_key,
        "CLOUD_CLUB_MEMBERS_FILE": os.path.join(tmp, "club-members.json"),
        "CLOUD_CLUB_AUDIT_FILE": os.path.join(tmp, "broadcast-club-audit.log"),
        "QUEUE_DB_FILE": os.path.join(tmp, "instruction-queue.db"),
        "CLASS_DIR_DB_FILE": os.path.join(tmp, "class-dir.db"),
        "EVENTS_FILE": os.path.join(tmp, "events.log"),
        "INTERACT_DB_FILE": os.path.join(tmp, "interact.db"),
        "OTA_MANIFEST_FILE": os.path.join(tmp, "ota.json"),
    })
    return e

def http_req(method, path, port, body=None, token=None, timeout=15):
    import http.client
    h = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    headers = {"content-type": "application/json"}
    if token:
        headers["authorization"] = f"Bearer {token}"
    data = json.dumps(body).encode() if body is not None else None
    h.request(method, path, body=data, headers=headers)
    r = h.getresponse()
    raw = r.read().decode("utf-8", "replace")
    h.close()
    try:
        j = json.loads(raw)
    except Exception:
        j = None
    return r.status, j, raw

def wait_ready(port, t=30):
    for _ in range(t * 2):
        try:
            s, _, _ = http_req("GET", "/api/public/ota", port)
            if s == 200:
                return True
        except Exception:
            pass
        time.sleep(0.5)
    return False

def which_node():
    import shutil
    return shutil.which("node") or "node"

def main():
    args = parse_args()
    tmp = tempfile.mkdtemp(prefix="cloud-bc-harness-")
    print(f"[*] 临时数据目录: {tmp}")

    # ① 端口：自动挑空闲 + 清掉残留占用者
    cloud_port = args.cloud_port or pick_free_port()
    vh_port = args.vh_port or pick_free_port()
    kill_port_owners(cloud_port)
    kill_port_owners(vh_port)
    time.sleep(1)

    # ② VoiceHub 后端：默认用独立 Node 本地替身（真实 HTTP 路径），--no-standin 退回内存 mock
    if args.standin:
        print(f"[*] 起独立本地 VoiceHub 替身（scripts/_local_voicehub_standin.js）端口={vh_port} 期望密钥前缀={args.vh_key[:8]}…")
        vh = start_standin_node(vh_port, args.vh_key)
        if vh is None:
            return 1
    else:
        print(f"[*] 起进程内 mock VoiceHub 端口={vh_port} 期望密钥前缀={args.vh_key[:8]}…")
        vh = start_mock_vh(vh_port, args.vh_key)

    # ③ 隔离云端
    log = open(os.path.join(tmp, "cloud.log"), "w", encoding="utf-8", errors="replace")
    env = build_env(args, vh_port, tmp, cloud_port)
    print(f"[*] 起隔离云端 端口={cloud_port}")
    try:
        proc = subprocess.Popen([which_node(), "src/index.js"], cwd=CLOUD_DIR,
                                env=env, stdout=log, stderr=log)
    except Exception as e:
        print(f"[FAIL] 启动云端失败: {e}")
        return 1

    exep = None
    try:
        if not wait_ready(cloud_port):
            print("[FAIL] 云端未在 30s 内就绪，日志：")
            print(log.read()); return 1
        print(f"[ok] 隔离云端就绪 (pid={proc.pid})")

        # 种成员账号
        s, j, _ = http_req("POST", "/api/admin/broadcast-club", cloud_port,
                           {"username": args.member, "password": args.password,
                            "station": args.station}, token=args.admin_token)
        if not (s == 200 and j and j.get("ok")):
            print(f"[FAIL] 种成员账号失败 {s} {j}"); print(log.read()); return 1
        print(f"[ok] 已种广播社成员: {args.member} / 密码: {args.password} / station: {args.station}")

        # 连通性自检：登录 + 读排期
        s, j, _ = http_req("POST", "/api/broadcast-club/login", cloud_port,
                           {"username": args.member, "password": args.password})
        if not (s == 200 and j and j.get("token")):
            print(f"[FAIL] 成员登录自检失败 {s} {j}"); print(log.read()); return 1
        mtok = j["token"]
        s, raw, _ = http_req("GET", "/api/broadcast-club/schedules", cloud_port, token=mtok)
        if s != 200:
            print(f"[FAIL] 读排期自检失败 {s} {raw[:200]}"); print(log.read()); return 1
        arr = raw if isinstance(raw, list) else json.loads(raw)
        print(f"[ok] 成员登录 + 读排期自检通过，排期条目数={len(arr)}")

        # reorder 写回自检（代理→mock VH）
        a, b = dict(arr[0]), dict(arr[1])
        a["sequence"], b["sequence"] = b["sequence"], a["sequence"]
        s, j, _ = http_req("POST", "/api/broadcast-club/schedules/reorder", cloud_port,
                           {"schedules": [a, b]}, token=mtok)
        if not (s == 200 and j and j.get("ok")):
            print(f"[FAIL] reorder 自检失败 {s} {j}"); print(log.read()); return 1
        print(f"[ok] reorder 写回自检通过（代理已带 x-api-key 落到 mock VoiceHub）")

        if args.smoke:
            print("\n==== harness 自证通过 ✅（smoke 模式，自动退出）====")
            return 0

        # ④ 自动拉起面板（除非 --no-exe）
        if not args.no_exe:
            if os.path.exists(args.exe):
                exe_dir = os.path.dirname(os.path.abspath(args.exe))
                exenv = dict(os.environ)
                exenv["STE_QT_WS_URL"] = f"ws://127.0.0.1:{cloud_port}/ws/agent"
                exenv["STE_QT_WS_TOKEN"] = args.admin_token
                # 跳过单实例互斥体：生产版被控端此刻正由 guard 守护运行，
                # 占着 Global\StelarithAgentQt_Singleton；不设这个测试实例会直接退出、不弹窗。
                exenv["STE_ALLOW_MULTI"] = "1"
                try:
                    exep = subprocess.Popen([args.exe], cwd=exe_dir, env=exenv)
                    print(f"[ok] 已自动拉起面板 exe (pid={exep.pid})")
                except Exception as e:
                    print(f"[warn] 自动拉起面板失败: {e}（请手动启动：见下方命令）")
                    exep = None
            else:
                print(f"[warn] 找不到 exe: {args.exe}（请手动启动：见下方命令）")
                exep = None

        print("\n" + "=" * 64)
        print("  本地实弹 harness 已就绪，进程保持运行。")
        if exep is None:
            print("  手动启动面板（已构建的 stelarith-agent-qt.exe）:")
            print(f"    set STE_QT_WS_URL=ws://127.0.0.1:{cloud_port}/ws/agent")
            print(f"    set STE_QT_WS_TOKEN={args.admin_token}")
            print(f"    build\\stelarith-agent-qt.exe")
        else:
            print("  面板应已自动弹出；若没弹出，按上面的手动命令重起。")
        print("")
        print(f"  在控制面板切到「广播社」页，用账号登录实测：")
        print(f"    用户名: {args.member}")
        print(f"    密码:   {args.password}")
        print("  重点看：登录门、按日隔离日期条、当日拖拽保存、登出/令牌失效回登录。")
        print("=" * 64 + "\n")
        print("[*] 按 Ctrl-C 停止并清理（云端 + mock VH + 面板一并回收）。")

        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[*] 收到中断，清理进程…")
    finally:
        for p in (proc, exep, vh):
            if isinstance(p, subprocess.Popen):
                try:
                    p.terminate(); p.wait(timeout=8)
                except Exception:
                    try: p.kill()
                    except Exception: pass
        try:
            if hasattr(vh, "shutdown"): vh.shutdown()
        except Exception: pass
        try: log.close()
        except Exception: pass
        print(f"[*] 已停。临时目录（含审计/成员库/日志）保留: {tmp}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
