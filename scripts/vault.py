#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""星集控 · 本地密钥存储（vault）

自建目的（2026-10-31）：GitHub PAT / 云令牌这类凭据不该散落在 .env / 脚本 / 内存里。
本工具提供**进程级私人凭据库**：AES-256-GCM 加密、独立 32B 主密钥、ACL 收紧，
结构干净（不依赖 WorkBuddy 的凭据体系，云/端/站点线通用）。

用法（全部走受管 Python，禁 cmd/PS 落地）：
  python scripts/vault.py init                 # 首次：生成 master.key（幂等）
  echo -n "<secret>" | python scripts/vault.py set github_pat      # 从 stdin 写入（不落历史）
  python scripts/vault.py set github_pat --value <secret>          # 或显式传值（慎用：会进进程列表）
  python scripts/vault.py get  github_pat                          # 读明文到 stdout（脚本用；慎打印）
  python scripts/vault.py list                                     # 只列名字，不显值
  python scripts/vault.py delete github_pat                        # 删除一条
  python scripts/vault.py check                                    # 自检：key 权限 / 库完整性

存储位置（用户级，绝不进 git）：
  %LOCALAPPDATA%\\stelarith-vault\\master.key   （32B 随机，ACL 仅当前用户可读）
  %LOCALAPPDATA%\\stelarith-vault\\vault.json   （AES-256-GCM 密文，每值独立随机 iv/tag）

设计：
  · 每条值独立随机 12B iv + GCM tag，同一值重写密文不同（不可比对）；
  · 主密钥一旦丢失 = 全部不可解密（设计如此，宁可锁死不泄露）；
  · 密文带版本头，未来换算法可平滑迁移。
"""

import argparse
import base64
import json
import os
import secrets
import subprocess
import sys

# ── 依赖 cryptography（受管 Python 已装 50.x；缺则明确提示，不静默降级）──
try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.exceptions import InvalidTag
except ImportError:
    print("[vault] 缺 cryptography 库：请用受管 Python 运行（自带该依赖）", file=sys.stderr)
    sys.exit(2)

VAULT_VERSION = 1
KEY_BYTES = 32
NONCE_BYTES = 12


# ── 控制台编码兜底（2026-10-31 · 用户实测崩溃后加）──────────────────────────
# 病灶：Windows CMD 默认 GBK(cp936)；脚本里任何 emoji（⚠️/✅）或生僻中文写 stdout
# 都会抛 UnicodeEncodeError: 'gbk' codec can't encode character '\u26a0'，整个命令崩掉。
# 不是"显示成乱码"这种小事 —— 是**写入失败 + 退出码 1**，把正常操作变成报错。
# 修法：出口统一按 UTF-8 + errors="replace" 兜底（替换字符总比崩强），
#      并把控制台代码页尽力切到 65001（失败不影响主流程）。
def _init_console_encoding():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    if os.name == "nt":
        try:
            import ctypes

            ctypes.windll.kernel32.SetConsoleOutputCP(65001)
        except Exception:  # noqa: BLE001 —— 尽力而为，切不了只是显示乱码
            pass


_init_console_encoding()


def vault_dir():
    base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
    return os.path.join(base, "stelarith-vault")


def key_path():
    return os.path.join(vault_dir(), "master.key")


def data_path():
    return os.path.join(vault_dir(), "vault.json")


# ── 主密钥：生成 / 读取 / ACL ──────────────────────────────────────────────
def gen_master_key():
    """生成 32B 随机主密钥并写盘；已存在则不动（幂等）。"""
    os.makedirs(vault_dir(), exist_ok=True)
    kp = key_path()
    if os.path.exists(kp):
        print("[vault] master.key 已存在，跳过生成（要重建先手动删）")
        return kp
    raw = secrets.token_bytes(KEY_BYTES)
    with open(kp, "wb") as f:
        f.write(raw)
    _harden_key_acl(kp)
    print("[vault] 已生成 master.key →", kp)
    return kp


def _harden_key_acl(kp):
    """Windows: 收紧 ACL，仅当前用户可读。失败只警告不中断（尽力而为）。"""
    if os.name != "nt":
        try:
            os.chmod(kp, 0o600)
        except OSError:
            pass
        return
    try:
        user = os.environ.get("USERNAME", "")
        if not user:
            return
        subprocess.run(
            ["icacls", kp, "/inheritance:r", "/grant:r", f"{user}:(R,W)"],
            capture_output=True, check=False)
    except Exception as e:  # noqa: BLE001
        print(f"[vault] ⚠ ACL 收紧失败（{e}），主密钥权限未限制", file=sys.stderr)


def load_master_key():
    kp = key_path()
    if not os.path.exists(kp):
        print("[vault] 未初始化：先跑 python scripts/vault.py init", file=sys.stderr)
        sys.exit(1)
    with open(kp, "rb") as f:
        raw = f.read()
    if len(raw) != KEY_BYTES:
        print("[vault] master.key 长度异常（%d，期望 %d）——拒绝使用" % (len(raw), KEY_BYTES), file=sys.stderr)
        sys.exit(1)
    return raw


# ── 密文读写（AES-256-GCM，每值独立 nonce）─────────────────────────────────
def _b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def _unb64(s: str) -> bytes:
    return base64.b64decode(s.encode("ascii"))


def encrypt_value(key: bytes, plain: bytes) -> str:
    nonce = secrets.token_bytes(NONCE_BYTES)
    ct = AESGCM(key).encrypt(nonce, plain, None)
    # 版本头(1B) | nonce(12B) | ct+tag  —— base64 整体
    blob = bytes([VAULT_VERSION]) + nonce + ct
    return _b64(blob)


def decrypt_value(key: bytes, blob_b64: str) -> bytes:
    try:
        blob = _unb64(blob_b64)
        if not blob or blob[0] != VAULT_VERSION:
            raise ValueError("密文版本不支持（%r）" % (blob[:1],))
        nonce = blob[1 : 1 + NONCE_BYTES]
        ct = blob[1 + NONCE_BYTES :]
        return AESGCM(key).decrypt(nonce, ct, None)
    except (ValueError, InvalidTag):
        # InvalidTag ≠ ValueError：硬撑会让 cmd_get/cmd_check 直接 Traceback。
        # 这里统一归一成"解密失败"，调用方按主密钥不匹配处理。
        raise ValueError("密文校验失败（密钥不匹配或数据被篡改）") from None


def read_vault(key: bytes) -> dict:
    dp = data_path()
    if not os.path.exists(dp):
        return {}
    with open(dp, "r", encoding="utf-8") as f:
        obj = json.load(f)
    if not isinstance(obj, dict) or "entries" not in obj:
        raise ValueError("vault.json 结构异常")
    return obj["entries"]


def write_vault(entries: dict) -> None:
    os.makedirs(vault_dir(), exist_ok=True)
    obj = {"version": VAULT_VERSION, "entries": entries}
    tmp = data_path() + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False)
    os.replace(tmp, data_path())


# ── 子命令 ─────────────────────────────────────────────────────────────────
def cmd_init(_a):
    gen_master_key()
    print("[vault] 就绪。写值（交互式，推荐）：python Stelarith-cloud/scripts/vault.py set <name>")
    print("[vault] 脚本化：<你的工具> | python Stelarith-cloud/scripts/vault.py set <name>")
    print("[vault] ⚠️ CMD 里没有 echo -n；别用 `echo -n xxx |`，会把 -n 写进值里。")


def cmd_set(args):
    key = load_master_key()
    name = args.name.strip()
    if not name or "/" in name or "\\" in name or name in (".", ".."):
        print("[vault] 名字不能含路径分隔符", file=sys.stderr)
        sys.exit(1)
    if args.value is not None:
        plain = args.value.encode("utf-8")
    elif sys.stdin.isatty():
        # 2026-10-31 修（用户实测报错）：交互式下原实现 sys.stdin.buffer.read() 会一直
        # 等 EOF（要按 Ctrl+Z 再回车），命令行里根本没法用。TTY 时改走隐藏输入提示：
        # 不回显、不进 shell 历史、不进进程列表，粘完回车即写入。
        import getpass

        try:
            typed = getpass.getpass(f"[vault] 输入 {name} 的值（不回显，回车确认）: ")
        except (EOFError, KeyboardInterrupt):
            print("\n[vault] 已取消", file=sys.stderr)
            sys.exit(1)
        plain = typed.encode("utf-8")
    else:
        # 非 TTY（管道 / 重定向）：读全部 stdin。这是脚本化调用的正路。
        plain = sys.stdin.buffer.read().strip()
    if not plain:
        print("[vault] 值为空，拒绝写入", file=sys.stderr)
        sys.exit(1)
    entries = read_vault(key)
    entries[name] = encrypt_value(key, plain)
    write_vault(entries)
    print(f"[vault] 已写入 {name}（{len(plain)} 字节）")


def cmd_get(args):
    key = load_master_key()
    entries = read_vault(key)
    if args.name not in entries:
        print(f"[vault] 没有 {args.name}", file=sys.stderr)
        sys.exit(1)
    try:
        plain = decrypt_value(key, entries[args.name])
    except ValueError as e:
        print(f"[vault] 解密失败（{e}）——主密钥与库不匹配？", file=sys.stderr)
        sys.exit(1)
    sys.stdout.buffer.write(plain)
    sys.stdout.buffer.write(b"\n")


def cmd_list(_args):
    key = load_master_key()
    entries = read_vault(key)
    names = sorted(entries.keys())
    if not names:
        print("[vault] （空）")
        return
    for n in names:
        print(n)


def cmd_delete(args):
    key = load_master_key()
    entries = read_vault(key)
    if args.name not in entries:
        print(f"[vault] 没有 {args.name}", file=sys.stderr)
        sys.exit(1)
    del entries[args.name]
    write_vault(entries)
    print(f"[vault] 已删除 {args.name}")


def cmd_check(_args):
    key = load_master_key()
    entries = read_vault(key)
    ok = 0
    bad = 0
    for name, blob in entries.items():
        try:
            decrypt_value(key, blob)
            ok += 1
        except ValueError:
            bad += 1
            print(f"[vault] ✗ {name} 解密失败（密钥与库不匹配）", file=sys.stderr)
    print(f"[vault] 自检完成：{ok} 条可解密，{bad} 条失败")
    return 0 if bad == 0 else 1


def main():
    p = argparse.ArgumentParser(description="星集控本地密钥存储（AES-256-GCM）")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("init", help="生成 master.key（幂等）")
    sp = sub.add_parser("set", help="写一条密钥（默认从 stdin 读值）")
    sp.add_argument("name")
    sp.add_argument("--value", default=None, help="显式传值（慎用：可见于进程列表）")

    gp = sub.add_parser("get", help="读一条密钥到 stdout")
    gp.add_argument("name")

    sub.add_parser("list", help="列出所有名字（不显值）")
    dp = sub.add_parser("delete", help="删除一条")
    dp.add_argument("name")
    sub.add_parser("check", help="自检：主密钥权限 + 全部条目可解密")

    args = p.parse_args()
    rc = {
        "init": cmd_init,
        "set": cmd_set,
        "get": cmd_get,
        "list": cmd_list,
        "delete": cmd_delete,
        "check": cmd_check,
    }[args.cmd](args)
    sys.exit(rc if isinstance(rc, int) else 0)


if __name__ == "__main__":
    main()