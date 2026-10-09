# 临时取证脚本：把 viewer-qt 的链接命令跑一遍，但输出成 build/viewer-qt-new.exe。
#
# 为什么要这么绕：桌面上有另一个 viewer-qt 实例正在跑，它占着 build/viewer-qt.exe，
# 直接 ninja viewer-qt 会在 link 阶段 LNK1104 失败（C++ 那一步其实是编译过的，
# 只是出不来可执行文件）。换个输出名就能一边不打扰那个实例、一边拿到新 exe 跑真机验证。
import io, os, re, subprocess, sys

BUILD = r"D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-viewer-qt\build"
NINJA = r"C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Scripts\ninja.exe"

def ninja_commands():
    p = subprocess.run([NINJA, "-C", BUILD, "-t", "commands", "viewer-qt"],
                       capture_output=True, text=True, errors="replace")
    if p.returncode != 0:
        print("ninja -t commands 失败：", p.stderr[:800]); sys.exit(1)
    return [l for l in p.stdout.splitlines() if l.strip()]

link = None
for line in ninja_commands():
    low = line.lower()
    if "link.exe" in low and "/out:" in low:
        link = line; break
if not link:
    print("没找到链接命令"); sys.exit(1)

newlink = link.replace("viewer-qt.exe", "viewer-qt-new.exe")
newlink = newlink.replace("viewer-qt.lib", "viewer-qt-new.lib")
print("OUT =", re.search(r"/out:(\S+)", newlink).group(1)[:120])

# link.exe 靠环境变量找 lib（命令里只有 /libpath 没列全），和 build-qt-viewer.bat 对齐设一份
MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
SDKVER = "10.0.26100.0"
if not os.environ.get("LIB"):
    sl = "/"
    os.environ["LIB"] = ";".join([MSVC + "/lib/x64",
                                  SDK + "/Lib/" + SDKVER + "/ucrt/x64",
                                  SDK + "/Lib/" + SDKVER + "/um/x64"])
    os.environ["INCLUDE"] = ";".join([MSVC + "/include",
                                      SDK + "/Include/" + SDKVER + "/ucrt",
                                      SDK + "/Include/" + SDKVER + "/um",
                                      SDK + "/Include/" + SDKVER + "/shared",
                                      SDK + "/Include/" + SDKVER + "/winrt"])
# 链接器要的库路径已经在命令里，补上环境变量就能跑
p = subprocess.run(newlink, shell=True, cwd=BUILD,
                   capture_output=True, text=True, errors="replace")
tail = (p.stdout or "") + (p.stderr or "")
for line in tail.splitlines():
    if re.search(r"error|LNK\d+|fatal", line, re.I):
        print("  " + line[:200])
print("rc =", p.returncode)
out = os.path.join(BUILD, "viewer-qt-new.exe")
print("new exe:", out, os.path.getsize(out) if os.path.exists(out) else "不存在")
