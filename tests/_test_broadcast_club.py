#!/usr/bin/env python3
# 广播社成员闭环联调（隔离实例，不碰生产 8788）
# 起一个独立云端：端口 8795 + 所有数据文件指向临时目录 + VoiceHub 写指向内置 mock。
#
# ⚠️ 设计说明：本机未必有真实 VoiceHub 在跑（它属于红线外工程，本测试不启动它）。
# 因此这里内置一个 mock VoiceHub，确定性地模拟其两个被代理的端点：
#   GET  /api/songs/public          → 返回排期数组
#   POST /api/open/schedules/reorder → 校验 x-api-key，按 id 更新 sequence，返回 ok
# 据此验证云端代理层的请求构造 / 鉴权头 / 错误映射，无需外部服务。
import subprocess, os, sys, time, tempfile, shutil, json, http.client, threading
import http.server

CLOUD_DIR = r"D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-cloud"
NODE = shutil.which("node") or "node"
PORT = 8795
VH_KEY = "vhub_aaa4374a7f630d6f5c1381aa885ad054"  # mock VoiceHub 期望的写密钥
ADMIN_TOKEN = "test-static-token"
SECRET = "test-secret-0123456789"

TMP = tempfile.mkdtemp(prefix="cloud-bc-")

# ── 内置 mock VoiceHub ──────────────────────────────────────────────────────
class _VHHandler(http.server.BaseHTTPRequestHandler):
    items = []          # 排期数组（运行时设置）
    api_key = VH_KEY    # 期望的 x-api-key
    last_key = None     # 记录最近一次收到的密钥，供断言
    def log_message(self, *a): pass
    def _send(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("content-type", "application/json")
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
            _VHHandler.last_key = self.headers.get("x-api-key")
            length = int(self.headers.get("content-length", 0) or 0)
            raw = self.rfile.read(length) if length else b"{}"
            try:
                payload = json.loads(raw.decode("utf-8", "replace"))
            except Exception:
                payload = {}
            if _VHHandler.last_key != _VHHandler.api_key:
                self._send(401, {"ok": False, "message": "bad key"})
                return
            by_id = {it["id"]: it for it in _VHHandler.items}
            for s in payload.get("schedules", []):
                it = by_id.get(s.get("id"))
                if it is not None:
                    it["sequence"] = s.get("sequence")
            self._send(200, {"ok": True, "message": "saved"})
        else:
            self._send(404, {"error": "not found"})

def start_mock_vh():
    _VHHandler.items = [
        {"id": 1, "sequence": 10, "name": "晨间新闻", "station": "class_xlzx_2028_08"},
        {"id": 2, "sequence": 20, "name": "午间点歌", "station": "class_xlzx_2028_08"},
        {"id": 3, "sequence": 30, "name": "放学音乐", "station": "class_xlzx_2028_08"},
    ]
    _VHHandler.api_key = VH_KEY
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _VHHandler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, srv.server_address[1]

def env(mock_port):
    e = dict(os.environ)
    e.update({
        "CLOUD_WS_PORT": str(PORT),
        "CLOUD_WS_HOST": "127.0.0.1",
        "CLOUD_WS_TOKEN": ADMIN_TOKEN,
        "CLOUD_VIEWER_TOKEN": ADMIN_TOKEN,
        "CLOUD_VIEWER_SECRET": SECRET,
        "VOICEHUB_BASE_URL": "http://127.0.0.1:9",   # 禁用只读桥轮询，避免打生产
        "STE_VOICEHUB_WRITE_BASE": f"http://127.0.0.1:{mock_port}",
        "STE_VOICEHUB_WRITE_KEY": VH_KEY,
        "CLOUD_CLUB_MEMBERS_FILE": os.path.join(TMP, "club-members.json"),
        "CLOUD_CLUB_AUDIT_FILE": os.path.join(TMP, "broadcast-club-audit.log"),
        "QUEUE_DB_FILE": os.path.join(TMP, "instruction-queue.db"),
        "CLASS_DIR_DB_FILE": os.path.join(TMP, "class-dir.db"),
        "EVENTS_FILE": os.path.join(TMP, "events.log"),
        "INTERACT_DB_FILE": os.path.join(TMP, "interact.db"),
        "OTA_MANIFEST_FILE": os.path.join(TMP, "ota.json"),
        "UDP_RELAY_PORT": "8796",
        "TCP_RELAY_PORT": "8797",
    })
    return e

def req(method, path, body=None, token=None, timeout=15):
    h = http.client.HTTPConnection("127.0.0.1", PORT, timeout=timeout)
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

def wait_ready(t=20):
    for _ in range(t * 2):
        try:
            s, _, _ = req("GET", "/api/public/ota")
            if s == 200:
                return True
        except Exception:
            pass
        time.sleep(0.5)
    return False

def main():
    srv, mock_port = start_mock_vh()
    print(f"[*] mock VoiceHub 已起端口={mock_port} 期望密钥={VH_KEY[:8]}…")
    log = open(os.path.join(TMP, "cloud.log"), "w", encoding="utf-8")
    proc = subprocess.Popen([NODE, "src/index.js"], cwd=CLOUD_DIR, env=env(mock_port),
                            stdout=log, stderr=log)
    try:
        print(f"[*] 启动隔离云端 pid={proc.pid} 端口={PORT} tmp={TMP}")
        if not wait_ready():
            print("[FAIL] 云端未在 20s 内就绪"); print(log.read()); return 1
        print("[ok] 云端就绪")

        # 1) 管理员建成员
        s, j, _ = req("POST", "/api/admin/broadcast-club",
                      {"username": "radioclub1", "password": "broadcast123", "station": "class_xlzx_2028_08"},
                      token=ADMIN_TOKEN)
        assert s == 200 and j.get("ok"), f"建成员失败 {s} {j}"
        print("[ok] 建成员 radioclub1 / station=class_xlzx_2028_08")

        # 1b) 管理员重复建同名应 400
        s, _, _ = req("POST", "/api/admin/broadcast-club",
                      {"username": "radioclub1", "password": "broadcast123", "station": "class_xlzx_2028_08"},
                      token=ADMIN_TOKEN)
        assert s == 400, f"重复建成员未拒 {s}"
        print("[ok] 重复建同名成员被拒(400)")

        # 1c) 管理面越权：成员令牌访问 /api/admin/broadcast-club 应 401（成员令牌禁入管理面）
        # 先登录拿到成员令牌
        s, j, _ = req("POST", "/api/broadcast-club/login",
                      {"username": "radioclub1", "password": "broadcast123"})
        assert s == 200 and j.get("token"), f"登录失败 {s} {j}"
        mtok = j["token"]
        s, _, _ = req("GET", "/api/admin/broadcast-club", token=mtok)
        assert s == 401, f"成员令牌竟能进管理面 {s}"
        print("[ok] 成员令牌访问管理面被拒(401) — 越权防护生效")

        # 2) 成员登录（已登录，复用 mtok）
        print(f"[ok] 成员登录成功，拿到令牌(前12={mtok[:12]}…)")

        # 2b) 错密码应 401
        s, _, _ = req("POST", "/api/broadcast-club/login",
                      {"username": "radioclub1", "password": "wrong"})
        assert s == 401, f"错密码未拒 {s}"
        print("[ok] 错密码被拒(401)")

        # 2c) 无令牌读排期应 401
        s, _, _ = req("GET", "/api/broadcast-club/schedules")
        assert s == 401, f"无令牌未拒 {s}"
        print("[ok] 无令牌读排期被拒(401)")

        # 3) 读排期（经代理到 mock VoiceHub）
        s, raw, _ = req("GET", "/api/broadcast-club/schedules", token=mtok)
        assert s == 200, f"读排期失败 {s} {raw[:200]}"
        arr = json.loads(raw) if isinstance(raw, str) else raw
        assert isinstance(arr, list) and len(arr) >= 2, f"排期数据异常: {raw[:200]}"
        print(f"[ok] 读排期成功，条目数={len(arr)}")
        ids = [x["id"] for x in arr]
        seqs = [x["sequence"] for x in arr]
        print(f"     顺序(改前): {list(zip(ids, seqs))}")

        # 4) 交换前两项的 sequence
        before = [{"id": x["id"], "sequence": x["sequence"]} for x in arr]
        a, b = dict(before[0]), dict(before[1])
        a["sequence"], b["sequence"] = b["sequence"], a["sequence"]
        swap = [a, b]
        s, j, _ = req("POST", "/api/broadcast-club/schedules/reorder",
                      {"schedules": swap, "before": before}, token=mtok)
        assert s == 200 and j.get("ok"), f"reorder 失败 {s} {j}"
        print("[ok] reorder 写回成功(200)")

        # 4b) 代理确实带上了 x-api-key
        assert _VHHandler.last_key == VH_KEY, f"mock 未收到正确密钥: {_VHHandler.last_key}"
        print("[ok] 代理透传 x-api-key 正确")

        # 5) 读回确认顺序已变
        s, raw2, _ = req("GET", "/api/broadcast-club/schedules", token=mtok)
        arr2 = json.loads(raw2) if isinstance(raw2, str) else raw2
        seqs2 = [x["sequence"] for x in arr2]
        assert seqs2 != seqs, f"顺序未变化 {seqs} -> {seqs2}"
        print(f"[ok] 读回确认顺序变化: {seqs} -> {seqs2}")

        # 6) 审计落盘
        audit_path = os.path.join(TMP, "broadcast-club-audit.log")
        assert os.path.exists(audit_path), "审计文件未生成"
        with open(audit_path, encoding="utf-8") as f:
            lines = [l for l in f.read().splitlines() if l.strip()]
        assert lines, "审计为空"
        rec = json.loads(lines[-1])
        assert rec["uid"] == "radioclub1" and rec["station"] == "class_xlzx_2028_08" and rec["ok"] is True, rec
        print(f"[ok] 审计落盘: uid={rec['uid']} station={rec['station']} ok={rec['ok']}")

        # 7) 还原顺序（把交换换回去）
        s, j, _ = req("POST", "/api/broadcast-club/schedules/reorder",
                      {"schedules": before}, token=mtok)
        assert s == 200, f"还原失败 {s} {j}"
        print("[ok] 顺序已还原")

        # 8) 越权：成员令牌访问管理面 /api/devices 应 401
        s, _, _ = req("GET", "/api/devices", token=mtok)
        assert s == 401, f"成员令牌竟能进管理面 {s}"
        print("[ok] 成员令牌访问 /api/devices 被拒(401) — 越权防护生效")

        # 9) 管理员关成员后再登录应 401
        s, j, _ = req("POST", "/api/admin/broadcast-club/disable",
                      {"username": "radioclub1"}, token=ADMIN_TOKEN)
        assert s == 200 and j.get("ok"), f"禁用失败 {s} {j}"
        s, _, _ = req("POST", "/api/broadcast-club/login",
                      {"username": "radioclub1", "password": "broadcast123"})
        assert s == 401, f"禁用后仍能登录 {s}"
        print("[ok] 禁用成员后登录被拒(401)")

        print("\n==== 全部通过 ✅ ====")
        return 0
    finally:
        proc.terminate()
        try: proc.wait(timeout=8)
        except Exception: proc.kill()
        try: srv.shutdown()
        except Exception: pass
        log.close()
        print(f"[*] 云端已停止；临时目录保留供排查: {TMP}")

if __name__ == "__main__":
    sys.exit(main())
