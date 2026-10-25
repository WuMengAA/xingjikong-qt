#!/usr/bin/env python3
"""星集控手机端 · APK 打包（Python 版，替代 make-apk.ps1）。

为什么重写：本机生产纪律禁止用 cmd/PowerShell 落地，而原 .ps1 里每一步都要过 PowerShell。
这里用 Python 直接调 cmake / java / apksigner，行为与原 .ps1 等价，并且修掉了三个坑：

  坑 1（假死）——`subprocess(capture_output=True)` 包 `cmake --build` 会**永久挂起**：
       Android 目标末尾的 androiddeployqt 会拉起 gradle，守护进程继承了管道，读不到 EOF。
       ⇒ 所有子进程输出一律**重定向到文件**，并给 gradle 加 `--no-daemon`。

  坑 2（OOM）——`GRADLE_USER_HOME`（本机 D:\\.gradle）的全局 gradle.properties 把堆定死
       `-Xmx2560m`，且**优先级高于项目级**；打包/lint 时必崩
       （`Native memory allocation (mmap) failed to map ... for G1 virtual space`）。
       ⇒ 命令行 `-Dorg.gradle.jvmargs=` 覆盖 + 跳过 `lintVital*`。

  坑 3（版本号丢失）——跳过 configure 时 `AndroidManifest` 被写成 `versionCode=1/versionName=1.0`；
       手改 build 目录只能撑到下一次 androiddeployqt。
       ⇒ 打包前按 `CMakeLists.txt` 的 `VIEWER_VERSION` **确定性改写** manifest。
       （Qt 6.12 在本工程配置下不消费 QT_ANDROID_VERSION_NAME/CODE，实测部署 json 里没有这两个键。）

  坑 4（LLVM OOM，2026-10-26）——ninja 默认按核数并行，本机 14.8 GB 内存下
       `viewerbackend.cpp`（2800 行 + 一堆 Qt 头）/ `viewerbackend_voice.cpp` /
       `viewerbackend_account.cpp` / `mocs_compilation.cpp` 同时编 ⇒
       `LLVM ERROR: out of memory` + `clang frontend command failed due to signal`。
       ⇒ 显式传 `-j`，默认 **2**（可用内存 ÷ 单 TU 峰值 ~2-3GB 的安全值）。
       想快可以 `--jobs 4`，但先确认任务管理器里可用内存 > 8GB。

用法：
  python scripts/make-apk.py                # 全流程（增量：已编过就只做打包+签名）
  python scripts/make-apk.py --skip-build   # 只打包+签名（.so 已是最新时）
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # Stelarith-viewer-qt
BDIR = ROOT / "build-android"
AB = BDIR / "android-build"
JAVA_HOME = Path(r"C:\Program Files\Java\jdk-17")
SDK = Path(r"D:\Android\Sdk")
NDK = SDK / "ndk" / "27.0.12077973"
QT_BIN = Path(r"D:\QTqwq\6.12.0\android_arm64_v8a\bin")
NINJA = Path(r"D:\QTqwq\Tools\Ninja")
CMAKE = Path(r"C:\Users\Administrator\.workbuddy\binaries\python\envs\default"
             r"\Lib\site-packages\cmake\data\bin\cmake.exe")
OPENSSL_DIR = Path(r"D:\Stelarith\_probe\android_openssl-master\ssl_3\arm64-v8a")
KEYSTORE = ROOT / "android" / "stelarith-release.keystore"
KS_PASS = "stelarith2026"
FINAL = ROOT / "dist" / "星集控-手机端-manager-arm64-release.apk"
TMPLOG = Path(os.environ.get("TEMP", "."))


def env() -> dict:
    e = dict(os.environ)
    e.update({
        "ANDROID_SDK_ROOT": str(SDK),
        "ANDROID_NDK_ROOT": str(NDK),
        "ANDROID_NDK_HOME": str(NDK),
        "ANDROID_JAVA_HOME": str(JAVA_HOME),
        "JAVA_HOME": str(JAVA_HOME),
        "PATH": f"{QT_BIN};{NINJA};" + e.get("PATH", ""),
    })
    if not e.get("STE_VIEWER_TOKEN_BUILD"):
        envfile = ROOT.parent / "Stelarith-cloud" / ".env"
        if envfile.exists():
            for line in envfile.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.startswith("CLOUD_VIEWER_TOKEN="):
                    e["STE_VIEWER_TOKEN_BUILD"] = line.split("=", 1)[1].strip()
                    break
    return e


def run_to_file(cmd: list[str], cwd: Path, log: Path, e: dict) -> int:
    with open(log, "wb") as f:
        return subprocess.run(cmd, cwd=str(cwd), env=e, stdout=f, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL).returncode


def sha256f(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()


def version_from_cmake() -> tuple[str, int]:
    txt = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r'set\(VIEWER_VERSION\s+"([^"]+)"\)', txt)
    ver = m.group(1) if m else "0.0.0"
    vm = re.match(r"(\d+)\.(\d+)\.(\d+)(?:-(?:rc|alpha)\.(\d+))?$", ver)
    code = (int(vm.group(1)) * 1000000 + int(vm.group(2)) * 10000
            + int(vm.group(3)) * 100 + int(vm.group(4) or 0)) if vm else 1
    return ver, code


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-build", action="store_true")
    # 坑 4：默认并行度必须压住 —— ninja 按核数跑会把 clang++ 堆到 OOM。
    ap.add_argument("--jobs", type=int, default=2,
                    help="编译并行度（默认 2；LLVM OOM 时再降 1，内存充足可给 4）")
    # 坑 5：本机「提交内存」（RAM+页面文件）常在 95%~99%，JVM 连几百 MB 都可能
    #   commit 失败（DOS error 1455）。堆开小一点比开大更容易起得来。
    ap.add_argument("--heap", default="900m",
                    help="gradle JVM 堆上限（默认 900m；提交内存紧张时试 640m）")
    a = ap.parse_args()
    e = env()

    if not a.skip_build:
        print("== 1) cmake --build build-android （-j %d）==" % a.jobs)
        rc = run_to_file([str(CMAKE), "--build", str(BDIR), "--", "-j", str(a.jobs)],
                         ROOT, TMPLOG / "apk-build.log", e)
        print("   rc =", rc)
        if rc != 0:
            txt = (TMPLOG / "apk-build.log").read_text(encoding="utf-8", errors="replace").splitlines()
            print("\n".join([l for l in txt if "error" in l.lower() or "FAILED" in l][-20:]))
            return 1

    print("== 2) 拷 .so（viewer + OpenSSL）==")
    libs = AB / "libs" / "arm64-v8a"
    libs.mkdir(parents=True, exist_ok=True)
    shutil.copy2(BDIR / "libviewer-qt_arm64-v8a.so", libs / "libviewer-qt_arm64-v8a.so")
    for n in ("libcrypto_3.so", "libssl_3.so"):
        shutil.copy2(OPENSSL_DIR / n, libs / n)
    print("   ok")

    print("== 3) gradle.properties：压缩 + 降堆 ==")
    gp = AB / "gradle.properties"
    s = gp.read_text(encoding="utf-8")
    s = re.sub(r"(?m)^legacyPackaging=.*$", "legacyPackaging=true", s)
    if "legacyPackaging=true" not in s:
        s += "\nlegacyPackaging=true\n"
    s = re.sub(r"(?m)^org\.gradle\.jvmargs=.*$",
               "org.gradle.jvmargs=-Xmx1600m -XX:MaxMetaspaceSize=512m -Dfile.encoding=UTF-8", s)
    s = re.sub(r"(?m)^org\.gradle\.parallel=.*$", "org.gradle.parallel=false", s)
    gp.write_text(s, encoding="utf-8")

    print("== 4) AndroidManifest 版本号（按 CMakeLists 真源）==")
    ver, code = version_from_cmake()
    mf = AB / "AndroidManifest.xml"
    ms = mf.read_text(encoding="utf-8")
    ms = re.sub(r'android:versionCode="[^"]*"', f'android:versionCode="{code}"', ms, count=1)
    ms = re.sub(r'android:versionName="[^"]*"', f'android:versionName="{ver}"', ms, count=1)
    mf.write_text(ms, encoding="utf-8")
    print(f"   versionName={ver} versionCode={code}")

    print("== 5) gradle assembleRelease ==")
    t0 = time.time()
    rc = run_to_file([str(JAVA_HOME / "bin" / "java.exe"),
                      "-classpath", str(AB / "gradle" / "wrapper" / "gradle-wrapper.jar"),
                      "org.gradle.wrapper.GradleWrapperMain", "assembleRelease", "--no-daemon",
                      "-Dorg.gradle.jvmargs=-Xmx%s -XX:MaxMetaspaceSize=384m" % a.heap,
                      "-Dorg.gradle.parallel=false", "-Dorg.gradle.workers.max=1",
                      "-x", "lintVitalAnalyzeRelease", "-x", "lintVitalRelease"],
                     AB, TMPLOG / "apk-gradle.log", e)
    print(f"   rc = {rc}  {time.time()-t0:.0f}s")
    if rc != 0:
        txt = (TMPLOG / "apk-gradle.log").read_text(encoding="utf-8", errors="replace").splitlines()
        i = next((n for n, l in enumerate(txt) if "What went wrong" in l), None)
        print("\n".join(txt[i:i + 12]) if i is not None else "\n".join(txt[-15:]))
        return 1

    print("== 6) 签名 ==")
    unsigned = AB / "build" / "outputs" / "apk" / "release" / "android-build-release-unsigned.apk"
    FINAL.parent.mkdir(parents=True, exist_ok=True)
    rc = run_to_file([str(JAVA_HOME / "bin" / "java.exe"),
                      "-Xmx2048m", "-XX:MaxDirectMemorySize=1024m",
                      "-jar", str(SDK / "build-tools" / "34.0.0" / "lib" / "apksigner.jar"), "sign",
                      "--ks", str(KEYSTORE), "--ks-pass", f"pass:{KS_PASS}",
                      "--key-pass", f"pass:{KS_PASS}", "--ks-key-alias", "stelarith",
                      "--out", str(FINAL), str(unsigned)],
                     ROOT, TMPLOG / "apk-sign.log", e)
    print("   rc =", rc)
    if rc != 0:
        print((TMPLOG / "apk-sign.log").read_text(encoding="utf-8", errors="replace")[-1200:])
        return 1

    print(f"\n完成：{FINAL.name} {FINAL.stat().st_size/1e6:.1f} MB")
    print("sha256:", sha256f(FINAL))
    print(f"版本：{ver}（versionCode {code}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
