#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""被控端「开发调试」启动器 —— 让改动立刻能看、能点、能改 QML 重来。

=== 为什么需要它（2026-10-31 补做的）===
之前的交付方式是"改完 → 跑离屏探针 → 发截图"。用户当场指出三个问题：
  ① 你看不到真的窗口，只能看我截的图 —— 截图"全绿"不等于需求完成；
  ② 生产目录 `C:\\Program Files\\Stelarith\\` 装的是老固件（9-26 的），
     我在 `build/` 里改的东西你在屏幕上完全看不见；
  ③ 生产实例在跑（PID 常驻），它的**全局互斥体挡住任何第二个实例** ——
     所以我连"启动一个能看的窗口"都做不到，只能退回离屏探针，
     然后把探针结果当成交付证据。这是错的。
  ④ 该软件**没有 QML 热重载**（main.cpp 里 QML_WATCH / QFileSystemWatcher 均无）。

=== 本脚本做什么 ===
  1. 增量构建 `build/stelarith-agent-qt.exe`
  2. **强制同步 QML** 到 `build/qml/`（改完不重建也能生效的前提）
  3. 若检测到生产实例在跑 → 明示并询问是否停掉（不擅自动生产）
  4. 启动开发实例，**日志转 UTF-8 实时打到你的控制台**（原来的 GBK 乱码修掉了）
  5. `--watch`：盯 qml/ 与 src/，改动自动同步/重编并提示重启

=== 三种用法 ===
  python scripts/_dev_run.py                 # 构建 + 同步 + 启动（最常用）
  python scripts/_dev_run.py --qml-only      # 只同步 QML 再启动（改样式最快）
  python scripts/_dev_run.py --watch         # 常驻：改 QML 自动同步，改 C++ 自动重编

=== 你（用户）能怎么看 ===
  · 窗口直接弹出来，你自己点、自己切页面
  · 改 QML 存一下（--watch 模式）→ 自动同步 → Ctrl+C 重跑即可看到
  · 日志（含 QML ERROR）实时打在这个控制台里，**不再是乱码**
  · 关窗口 = 结束本次调试
"""
import os
import shutil
import subprocess
import sys
import time

# 真实控制台（.cmd + chcp 65001）里 Python 默认可能仍按 GBK 输出，
# 于是任何非 GBK 字符（[OK] [!] 等）都会 UnicodeEncodeError 直接崩掉整个启动器。
# 实测踩过：在我这里（管道）能跑，用户双击就崩。故显式改写 stdout/stderr。
if hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ROOT)
BUILD = os.path.join(ROOT, "build")
EXE = os.path.join(BUILD, "stelarith-agent-qt.exe")
QML_SRC = os.path.join(ROOT, "qml")
QML_DST = os.path.join(BUILD, "qml")
SHARED_UI = os.path.join(REPO, "Stelarith-shared-qml", "ui")
SHARED_ISLAND = os.path.join(REPO, "Stelarith-shared-qml", "island")
BUILD_SCRIPT = os.path.join(ROOT, "scripts", "_build_agent.py")
PROD_EXE = r"C:\Program Files\Stelarith\stelarith-agent-qt.exe"
DEV_LOCALAPPDATA = os.path.join(BUILD, "_devlocal")


def log(msg):
    print(msg, flush=True)


def newest_mtime(path):
    newest = 0.0
    for base, _dirs, files in os.walk(path):
        for f in files:
            try:
                newest = max(newest, os.path.getmtime(os.path.join(base, f)))
            except OSError:
                pass
    return newest


def sync_qml():
    """qml/ + 共享 ui/ + 共享 island/ → build/qml/（与 CMakeLists 的 copy_directory 一致）"""
    for src, dst in ((QML_SRC, QML_DST),
                     (SHARED_UI, os.path.join(QML_DST, "components")),
                     (SHARED_ISLAND, os.path.join(QML_DST, "island"))):
        if not os.path.isdir(src):
            log("  [!] 跳过（源不存在）：" + src)
            continue
        os.makedirs(dst, exist_ok=True)
        for item in os.listdir(src):
            s = os.path.join(src, item)
            d = os.path.join(dst, item)
            if os.path.isdir(s):
                if os.path.isdir(d):
                    shutil.rmtree(d)
                shutil.copytree(s, d)
            else:
                if (not os.path.exists(d)) or os.path.getmtime(s) > os.path.getmtime(d):
                    shutil.copy2(s, d)
    log("  [OK] QML 已同步到 build/qml（源最新 mtime = %s）"
        % time.strftime("%H:%M:%S", time.localtime(newest_mtime(QML_SRC))))


def build(qml_only=False):
    if qml_only:
        return True
    if not os.path.isfile(BUILD_SCRIPT):
        log("  [!] 找不到 _build_agent.py，跳过编译")
        return os.path.isfile(EXE)
    log("[1/3] 增量构建 ...")
    p = subprocess.run([sys.executable, BUILD_SCRIPT],
                       cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode("utf-8", "replace")
    if p.returncode != 0:
        for ln in out.splitlines():
            if "error" in ln.lower() or "FAILED" in ln:
                log("  " + ln)
        log("  [X] 构建失败")
        return False
    log("  [OK] 构建成功")
    return True


def running_pids(name_hint="stelarith-agent-qt"):
    """找出正在跑的 agent 进程（用来提示"生产实例会挡路"）。"""
    p = subprocess.run(["tasklist", "/FI", "IMAGENAME eq %s.exe" % name_hint, "/NH"],
                       capture_output=True)
    s = p.stdout.decode("gbk", "replace")
    pids = []
    for ln in s.splitlines():
        parts = ln.split()
        if len(parts) >= 2 and parts[0].lower().startswith(name_hint):
            try:
                pids.append(int(parts[1]))
            except ValueError:
                pass
    return pids


def show_state():
    if os.path.isfile(EXE):
        log("  exe      : %s  (%s, %.1f MB)"
            % (os.path.relpath(EXE, ROOT),
               time.strftime("%m-%d %H:%M:%S", time.localtime(os.path.getmtime(EXE))),
               os.path.getsize(EXE) / 1048576))
    else:
        log("  exe      : 不存在！")
    log("  数据目录 : %s   [独立于生产数据目录]"
        % os.path.relpath(DEV_LOCALAPPDATA, ROOT))
    pids = running_pids()
    if pids:
        log("  [!] 已有 agent 在跑：PID %s" % pids)
        if os.path.isfile(PROD_EXE):
            log("     生产固件：%s (%s)"
                % (PROD_EXE,
                   time.strftime("%m-%d %H:%M:%S",
                                 time.localtime(os.path.getmtime(PROD_EXE)))))
        log("     它的全局互斥体会挡住开发实例 —— 需要先停掉（见下方提示）")
    return pids


def stop_existing(pids, assume_yes=False):
    if not pids:
        return True
    if not assume_yes:
        log("")
        log("要停掉上述进程才能启动开发实例。输入 y 确认，其它键跳过：")
        try:
            ans = input("  > ").strip().lower()
        except EOFError:
            ans = ""
        if ans != "y":
            log("  已跳过；开发实例很可能因互斥体起不来。")
            return False
    for pid in pids:
        subprocess.run(["taskkill", "/F", "/PID", str(pid), "/T"],
                       capture_output=True)
        log("  已停止 PID %d" % pid)
    time.sleep(1.5)
    return True




def launch():
    r"""启动开发实例，并把 stdout/stderr 实时转 UTF-8 显示。

    数据隔离：被控端的数据目录是**硬编码**的 %LOCALAPPDATA%\xingjikong
    （main.cpp:3262 等多处），没有可配开关。故这里重定向 LOCALAPPDATA 环境变量
    来实现隔离 —— 开发实例的噪音/自习记录、渠道、班级码全部落在
    build/_devlocal\xingjikong，**完全不碰生产的真实数据**。

    （我一开始写成"设 STE_QT_DEV_HOME"，但代码里根本没读那个变量，等于空话。
      实测发现开发实例往生产数据目录写了 agent-run.flag 才发现问题。）
    """
    os.makedirs(DEV_LOCALAPPDATA, exist_ok=True)
    env = dict(os.environ)
    env["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    env["LOCALAPPDATA"] = DEV_LOCALAPPDATA      # ← 真正的数据隔离
    env["STE_QML_RELOAD"] = "1"                 # ← 改 QML 自动重画（不需重启）
    # 开发期直接把控制面板打开：否则托盘程序启动后只有一个岛，
    # 你得先去点托盘才知道界面长什么样（调试时这步很烦）。
    env["STE_QT_SHELL_AUTOOPEN"] = "1"

    log("[3/3] 启动开发实例（关窗口 = 结束本次调试）...")
    log("      数据目录：%s   （独立，不碰生产）" % os.path.relpath(DEV_LOCALAPPDATA, ROOT))
    log("      日志实时输出，QML ERROR 也在。")
    log("")
    p = subprocess.Popen([EXE], cwd=BUILD, env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p


def pump_output(proc, until_exit=False):
    """把子进程输出逐行解码打印（GBK 优先，失败退 UTF-8）。"""
    import io
    if not proc or not proc.stdout:
        return
    for raw in iter(proc.stdout.readline, b""):
        for enc in ("gbk", "utf-8"):
            try:
                line = raw.decode(enc).rstrip()
                break
            except UnicodeDecodeError:
                continue
        else:
            line = raw.decode("utf-8", "replace").rstrip()
        print(line, flush=True)
        if not until_exit and proc.poll() is not None:
            break


def watch_loop(proc):
    log("")
    log("=== --watch 已启用：改 QML 自动同步 / 改 C++ 自动重编 ===")
    log("    [!] 本软件没有 QML 热重载，改完仍需重启进程才生效（Ctrl+C 后重跑 --qml-only）")
    qml_t = newest_mtime(QML_SRC)
    src_t = newest_mtime(os.path.join(ROOT, "src"))
    while True:
        time.sleep(1.5)
        nq, ns = newest_mtime(QML_SRC), newest_mtime(os.path.join(ROOT, "src"))
        if nq > qml_t:
            qml_t = nq
            log("[watch] QML 有改动 → 同步 ...")
            sync_qml()
            log("[watch] 请重启进程查看")
        if ns > src_t:
            src_t = ns
            log("[watch] C++ 有改动 → 重新构建 ...")
            if build():
                log("[watch] 构建成功，请重启进程")
        if proc and proc.poll() is not None:
            log("[watch] 进程已退出（exit=%s）" % proc.returncode)
            return


def main():
    qml_only = "--qml-only" in sys.argv
    watch = "--watch" in sys.argv
    assume_yes = "--yes" in sys.argv or "-y" in sys.argv

    log("=" * 70)
    log("被控端开发调试启动器")
    log("=" * 70)

    if not build(qml_only=qml_only):
        return 1
    sync_qml()
    pids = show_state()
    if pids:
        stop_existing(pids, assume_yes=assume_yes)

    proc = launch()
    try:
        if watch:
            # watch 模式：后台线程抽输出，主线程盯文件
            import threading
            t = threading.Thread(target=pump_output, args=(proc, True), daemon=True)
            t.start()
            watch_loop(proc)
        else:
            pump_output(proc, until_exit=True)
    except KeyboardInterrupt:
        log("\n[dev] 收到 Ctrl+C，结束开发实例")
    finally:
        if proc and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
