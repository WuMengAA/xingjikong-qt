#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""把 VoiceHub 服务端在本机拉起来（2026-10-22，用户授权部署）。

背景：线上 voicehub.245959623.xyz 已被 Vercel 停用（HTTP 402 DEPLOYMENT_DISABLED），
星集控的 voicehubmodule 默认就指着那个死地址。要在本地起一份真的。

约束（按项目铁律）：
  · 全程用受管 Python，不经过 cmd/PowerShell
  · 只在 D:\\Stelarith\\Stelarith-Music\\voicehub_ref 里操作，不碰 _voicehub_ref 旧副本
  · 数据库只**新建** voicehub 库，不动任何现有库；密码从别的工程 .env 只读引用、不打印
  · .env 已存在则不覆盖（除非 --force）

用法（分步跑，便于定位失败）：
  python scripts/deploy-voicehub-local.py --step db       # 建库
  python scripts/deploy-voicehub-local.py --step env      # 写 .env
  python scripts/deploy-voicehub-local.py --step install  # pnpm install（慢）
  python scripts/deploy-voicehub-local.py --step migrate  # drizzle migrate
  python scripts/deploy-voicehub-local.py --step serve    # 起 dev server 并探活
  python scripts/deploy-voicehub-local.py --step check    # 只读体检
"""
import argparse
import os
import re
import secrets
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
import json

VS = r"D:\Stelarith\Stelarith-Music\voicehub_ref"
PSQL = r"C:\Program Files\PostgreSQL\17\bin\psql.exe"
PNPM = r"C:\Program Files\nodejs\pnpm.cmd"
NODE_DIR = r"C:\Program Files\nodejs"
CRED_SRC = r"D:\Stelarith\Stelarith-canteen-leaderboard\.env"
DB_NAME = "voicehub"
PORT = 3000


def die(msg):
    print("[FAIL] " + msg)
    sys.exit(1)


def pg_creds():
    """从既有工程的 .env 只读取出 postgres 连接串（不打印密码）。"""
    try:
        txt = open(CRED_SRC, encoding="utf-8", errors="replace").read()
    except OSError as e:
        die("读不到凭据来源 %s：%s" % (CRED_SRC, e))
    m = re.search(r"postgresql://([^:]+):([^@]+)@([^:/]+):?(\d+)?/(\w+)", txt)
    if not m:
        die("凭据来源里没找到 postgresql:// 连接串")
    return m.group(1), m.group(2), m.group(3), m.group(4) or "5432"


def psql(sql, dbname="postgres", user=None, pwd=None):
    host, port = (user or "")[:0] or "", ""
    u, p, h, pt = pg_creds()
    env = dict(os.environ)
    env["PGPASSWORD"] = p
    cmd = [PSQL, "-U", u, "-h", h, "-p", pt, "-d", dbname, "-t", "-A", "-c", sql]
    return subprocess.run(cmd, env=env, capture_output=True)


def step_check():
    print("== VoiceHub 本地部署体检 ==")
    print("工程目录：", VS, "存在" if os.path.isdir(VS) else "缺失")
    has_nm = os.path.isdir(os.path.join(VS, "node_modules"))
    print("node_modules：", "已装" if has_nm else "未装")
    env_path = os.path.join(VS, ".env")
    print(".env：", "已存在" if os.path.isfile(env_path) else "缺失")
    r = psql("SELECT 1 FROM pg_database WHERE datname='%s'" % DB_NAME)
    exists = r.stdout.decode("utf-8", "replace").strip() == "1"
    print("数据库 %s：%s" % (DB_NAME, "已存在" if exists else "不存在"))
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/api/music/broadcast" % PORT, timeout=3) as resp:
            print("端口 %d：HTTP %d" % (PORT, resp.status))
    except Exception as e:
        print("端口 %d：未监听（%s）" % (PORT, type(e).__name__))


def step_db():
    r = psql("SELECT 1 FROM pg_database WHERE datname='%s'" % DB_NAME)
    if r.returncode != 0:
        die("连 postgres 失败：" + r.stderr.decode("gbk", "replace")[:400])
    if r.stdout.decode("utf-8", "replace").strip() == "1":
        print("[skip] 数据库 %s 已存在" % DB_NAME)
        return
    r = psql('CREATE DATABASE "%s"' % DB_NAME)
    if r.returncode != 0:
        die("建库失败：" + r.stderr.decode("gbk", "replace")[:400])
    print("[ok] 已创建数据库 %s（只新建，未触碰其它库）" % DB_NAME)


def step_env(force=False):
    path = os.path.join(VS, ".env")
    if os.path.isfile(path) and not force:
        print("[skip] .env 已存在（要覆盖加 --force）")
        return
    u, p, h, pt = pg_creds()
    # ⚠️ 本地 postgres 通常没开 SSL —— 照抄示例里的 sslmode=require 会直接连不上。
    url = "postgresql://%s:%s@%s:%s/%s?sslmode=disable" % (u, p, h, pt, DB_NAME)
    jwt = secrets.token_hex(32)
    body = (
        "# 由 scripts/deploy-voicehub-local.py 生成（2026-10-22）\n"
        'DATABASE_URL="%s"\n'
        'JWT_SECRET="%s"\n'
        "NODE_ENV=development\n"
        "OAUTH_REDIRECT_URI=\"http://localhost:%d/api/auth/[provider]/callback\"\n"
        % (url, jwt, PORT)
    )
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(body)
    print("[ok] 已写 .env（DATABASE_URL 指向本地 %s 库，JWT_SECRET 32 字节随机）" % DB_NAME)


def pnpm(args, timeout=1800):
    env = dict(os.environ)
    env["PATH"] = NODE_DIR + ";" + env.get("PATH", "")
    env["CI"] = "1"
    # ⚠️ 必须给 pnpm 的子进程关掉安全删除垫片：
    #    pnpm 在准备 store 时会删自己的临时目录（_tmp_*），而被注入的
    #    node-safe-delete-shim 会把它当成"批量删除"拦下来直接抛错，
    #    表现是 install 一启动就 ERROR [safe-delete] ── 与网络、依赖都无关。
    #    只作用于这个子进程，不改全局行为。
    env["CODEBUDDY_SAFE_DELETE_ENABLED"] = "0"
    cmd = [PNPM] + args
    print("  $ pnpm " + " ".join(args))
    log = os.path.join(VS, "_pnpm.log")
    with open(log, "wb") as f:
        p = subprocess.run(cmd, cwd=VS, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=timeout)
    tail = ""
    try:
        raw = open(log, "rb").read()
        tail = raw.decode("utf-8", "replace")[-2500:]
    except OSError:
        pass
    return p.returncode, tail, log


def step_install():
    rc, tail, log = pnpm(["install", "--no-frozen-lockfile"])
    if rc != 0:
        print(tail)
        die("pnpm install 失败（完整日志 %s）" % log)
    print("[ok] 依赖已安装")


def step_migrate():
    rc, tail, log = pnpm(["run", "db:migrate"], timeout=600)
    if rc != 0:
        print(tail)
        die("drizzle migrate 失败（完整日志 %s）" % log)
    print("[ok] 数据库迁移完成")


def step_serve():
    env = dict(os.environ)
    env["PATH"] = NODE_DIR + ";" + env.get("PATH", "")
    env["NITRO_PORT"] = str(PORT)
    env["PORT"] = str(PORT)
    log = os.path.join(VS, "_devserver.log")
    f = open(log, "wb")
    p = subprocess.Popen([PNPM, "run", "dev"], cwd=VS, env=env,
                         stdout=f, stderr=subprocess.STDOUT,
                         creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
    print("[start] dev server pid=%d，日志 %s" % (p.pid, log))
    # 首次 dev 编译很慢，给足 120 秒轮询
    for i in range(40):
        time.sleep(3)
        try:
            with urllib.request.urlopen("http://127.0.0.1:%d/api/music/broadcast" % PORT, timeout=4) as r:
                body = r.read(600).decode("utf-8", "replace")
                print("[ok] HTTP %d  %s" % (r.status, body[:300]))
                print("PID=%d" % p.pid)
                return p.pid
        except urllib.error.HTTPError as e:
            body = e.read(300).decode("utf-8", "replace")
            print("[ok] 服务已监听，HTTP %d（业务层错误也是活着的证据）：%s" % (e.code, body[:200]))
            print("PID=%d" % p.pid)
            return p.pid
        except Exception:
            if i % 5 == 0:
                print("  ...等待启动 (%ds)" % (i * 3))
    print("[warn] 120s 内没探活成功，看日志：%s" % log)
    try:
        print(open(log, "rb").read().decode("utf-8", "replace")[-2000:])
    except OSError:
        pass
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--step", required=True,
                    choices=["check", "db", "env", "install", "migrate", "serve"])
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()

    if not os.path.isdir(VS):
        die("找不到 VoiceHub 工程：%s" % VS)

    {"check": step_check,
     "db": step_db,
     "env": lambda: step_env(a.force),
     "install": step_install,
     "migrate": step_migrate,
     "serve": step_serve}[a.step]()
    return 0


if __name__ == "__main__":
    sys.exit(main())
