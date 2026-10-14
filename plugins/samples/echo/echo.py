#!/usr/bin/env python3
# 星集控 · 示例插件（进程外 / Python）
# ───────────────────────────────────────────────────────────────────────────
# 依赖：pip install websocket-client
# 运行：python echo.py   （被控端需已启动本地插件服务 ws://127.0.0.1:26789）
# 行为：云端下发 action="echo" → 本插件回显文本，并用 island_show 在灵动岛弹提示。
#
# 这是「官方 SDK 示例」的 Python 版；同样的逻辑用 C++（StelarithPluginSDK.h）
# 或 C#（WebSocket.Client）写一份即可，三者协议完全一致。
import json
import os
import sys

try:
    import websocket  # websocket-client
except ImportError:
    print("缺少依赖：pip install websocket-client", file=sys.stderr)
    sys.exit(1)

PORT = os.environ.get("STE_PLUGIN_PORT", "26789")
URL = f"ws://127.0.0.1:{PORT}"

MANIFEST = {
    "id": "echo",
    "name": "Echo 示例插件",
    "version": "1.0.0",
    "author": "Stelarith",
    "description": "演示进程外插件",
    "minAgentVersion": "0.6.31",
    "permissions": ["island"],
    "runtime": "python",
    "entry": "echo.py",
    "actions": ["echo"],
}


def send(ws, obj):
    ws.send(json.dumps(obj))


def on_message(ws, text):
    msg = json.loads(text)
    t = msg.get("type")
    if t == "welcome":
        if not msg.get("ok"):
            print("被控端拒绝接入：", msg.get("error"), file=sys.stderr)
            ws.close()
            return
        print("接入成功，授予权限：", msg.get("granted"))
        send(ws, {"type": "register", "actions": MANIFEST["actions"]})
    elif t == "action":
        action = msg["action"]
        params = msg.get("params", {})
        print(f"收到 action={action} params={params}")
        if action == "echo":
            text = params.get("text", "")
            # 调用被控端能力：灵动岛提示（受 island 权限约束）
            send(ws, {"type": "api", "id": "api1", "call": "island_show",
                      "args": {"title": "Echo 插件", "desc": f"回显：{text}",
                               "icon": "chat", "form": 1, "durationMs": 5000}})
            send(ws, {"type": "result", "id": msg["id"], "result": "done",
                      "data": {"echo": text}})
    elif t == "api_result":
        print("api 回执：", msg)
    elif t == "event":
        print("事件：", msg.get("name"), msg.get("payload"))


def on_open(ws):
    send(ws, {"type": "hello", "manifest": MANIFEST})


def main():
    ws = websocket.WebSocketApp(URL, on_open=on_open, on_message=on_message)
    print(f"Echo 插件连接 {URL} ...")
    ws.run_forever(reconnect=5)


if __name__ == "__main__":
    main()
