# -*- coding: utf-8 -*-
"""发布 viewer 0.6.50-rc.10（外壳转正）到 GitHub Release + 同步 ota-rc.json。
PAT 走 dsh-vault（不落盘、不打印）。
"""
import os, sys, json, hashlib, urllib.request, urllib.error, importlib.util, datetime

ROOT = r"D:\Stelarith\Stelarith-xingjikong-qt"
VAULT_SCRIPT = os.path.join(ROOT, "Stelarith-cloud", "scripts", "vault.py")
OWNER_REPO = "WuMengAA/xingjikong-qt"
TAG = "v0.6.51-rc.1"
API = "https://api.github.com/repos/" + OWNER_REPO
GH_PROXY = "https://gh.245959623.xyz/" + OWNER_REPO + "/releases/download/" + TAG + "/"

PKG = os.path.join(ROOT, "Stelarith-viewer-qt", "dist", "stelarith-viewer-setup-0.6.50-rc.10.exe")
VERSION = "0.6.50-rc.10"
OTA = os.path.join(ROOT, "Stelarith-cloud", "ota-rc.json")

NOTES = (
    "✨新增 新版外壳**转正** —— PreviewConsole 成为唯一外壳："
    "左导航轨 + 顶栏（班级切换/登录/头像）+ 统计行 + 业务页 + 状态条 + 竖屏底栏\n"
    "🔧修复 顶栏班级切换补回真功能（ClassSort 排序 + 双写 pickClass；"
    "实测 全部班级→12班→5班 四处同步变化）\n"
    "🔧修复 状态条 / 竖屏底栏随转正补齐；头像落点仍是个人中心（不受 system:settings 门禁）\n"
    "⚡优化 MainWindow 561→445 行；5 个经典外壳件退役"
    "（Sidebar/TopBar/ActionBar/BottomNav/StatusBar）；预览开关随转正删除\n"
    "—— 代号「壳定于一」· 一套外壳到底 🔵"
)


def read_pat():
    spec = importlib.util.spec_from_file_location("vaultmod", VAULT_SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.vault_dir = lambda: r"C:\Users\Administrator\.dsh\vault"
    key = mod.load_master_key()
    vault = mod.read_vault(key)
    blob = vault.get("github-pat") or vault.get("github_pat")
    if not blob:
        raise SystemExit("dsh-vault 无 github-pat")
    return mod.decrypt_value(key, blob).decode("utf-8")


def req(url, method="GET", body=None, headers=None, timeout=1800):
    r = urllib.request.Request(url, data=body, method=method)
    r.add_header("Accept", "application/vnd.github+json")
    r.add_header("X-GitHub-Api-Version", "2022-11-28")
    r.add_header("User-Agent", "stelarith-ota")
    for k, v in (headers or {}).items():
        r.add_header(k, v)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return resp.status, resp.read(), dict(resp.headers)
    except urllib.error.HTTPError as e:
        return e.code, e.read(), dict(e.headers)


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()


def main():
    pat = read_pat()
    auth = {"Authorization": "Bearer " + pat}

    size = os.path.getsize(PKG)
    digest = sha256(PKG)
    print("pkg  :", PKG)
    print("size :", size)
    print("sha  :", digest)

    st, body, _ = req(API + "/releases/tags/" + TAG, headers=auth)
    if st != 200:
        print("get release failed:", st, body[:200])
        return 1
    rel = json.loads(body)
    upload_url = rel["upload_url"].split("{")[0]
    name = os.path.basename(PKG)

    for a in rel.get("assets", []):
        if a["name"] == name:
            s2, _, _ = req(API + "/releases/assets/" + str(a["id"]), method="DELETE", headers=auth)
            print("deleted old asset ->", s2)

    print("uploading ...")
    st3, b3, _ = req(upload_url + "?name=" + name, method="POST",
                     body=open(PKG, "rb").read(),
                     headers={"Content-Type": "application/octet-stream", **auth})
    if st3 not in (200, 201):
        print("upload failed:", st3, b3[:300])
        return 1
    print("uploaded (HTTP %d)" % st3)

    url = GH_PROXY + name
    s4, _, hd = req(url, method="HEAD", timeout=120)
    print("proxy HEAD:", s4, "len=", hd.get("Content-Length"))

    data = json.load(open(OTA, encoding="utf-8"))
    data["products"]["viewer"] = {
        "version": VERSION,
        "url": url,
        "sha256": digest,
        "size": size,
        "notes": NOTES,
        "mandatory": False,
    }
    data["generatedAt"] = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z")
    data["generatedBy"] = "scripts/_publish_viewer_rc10.py"
    with open(OTA, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print("ota-rc.json viewer ->", VERSION)
    return 0


if __name__ == "__main__":
    sys.exit(main())
