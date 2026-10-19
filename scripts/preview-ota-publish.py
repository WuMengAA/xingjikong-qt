# -*- coding: utf-8 -*-
"""
发版链路端到端预演（2026-10-07）—— 安全：清单指向临时目录，生产 ota.json 只被读。

目的：回答「发 0.6.23-alpha.N 到底会发生什么」，给你拍板发不发版时手里有实证。

环境变量怎么用是脚本内部实现（path.join(ROOT, ...)），所以：
  CLOUD_OTA_MANIFEST_FILE / CLOUD_ASSETS_DIR 只能给**相对路径**（相对 Stelarith-cloud 根），
  且 ASSETS_DIR 后面还会自动拼一个 'pkg'。
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # 工作区根（本脚本住 scripts/ 下）
CLOUD = os.path.join(ROOT, 'Stelarith-cloud')
PRE = os.path.join(CLOUD, '_ota-preview')
NODE = r'C:/Users/Administrator/.workbuddy/binaries/node/versions/22.22.2-6/node.exe'

MANI_REL = '_ota-preview/ota.json'
ASSETS_REL = '_ota-preview/assets'   # 脚本会自动拼 'pkg'
FAKE = os.path.join(PRE, 'src')

# 生产清单指纹（跑前记录，跑后比对）—— 预演绝不该动它。
# ⚠️ 不能把指纹写成死常量：真发过版之后清单必然变，硬编码会每次假红。
PROD = os.path.join(CLOUD, 'ota.json')

results = []

# 出一次包要落在磁盘上的量 = 包体 × 这个系数（zip 本体 1 + 逐级复制的暂存约 1.2 + 余量约 0.5）。
# 2026-10-07 实测：viewer 绿色包 131MB，D 盘只剩 62MB ⇒ 真出包必炸，而预演看不出来。
SAFE_FACTOR = 2.2


def sha256f(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def run(args):
    env = dict(os.environ)
    env['CLOUD_OTA_MANIFEST_FILE'] = MANI_REL
    env['CLOUD_ASSETS_DIR'] = ASSETS_REL
    p = subprocess.run([NODE] + args, cwd=CLOUD, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode('utf-8', 'replace')
    return p.returncode, out


def manifest():
    with open(os.path.join(CLOUD, MANI_REL), 'rb') as f:
        b = f.read()
    return json.loads(b.decode('utf-8')), b


def make_fake(version, size=1024):
    """造一个名字里带版本号的假包（内容随机 ⇒ sha 必不相同）。"""
    os.makedirs(FAKE, exist_ok=True)
    name = 'stelarith-agent-qt-%s.exe' % version
    p = os.path.join(FAKE, name)
    body = hashlib.sha256(('payload-' + version).encode()).digest() * (size // 32)
    with open(p, 'wb') as f:
        f.write(body[:size])
    return p


def guard(label, ok, detail=''):
    results.append((label, ok, detail))
    print(('  ✓ ' if ok else '  ✗ ') + label + ((' — ' + str(detail)[:150]) if detail else ''))


def main():
    # 0. 预演准备：清掉上次预演，从生产清单复制一份当 prev（这样能推断 origin/目录）
    prod_before = sha256f(PROD)
    if os.path.isdir(PRE):
        shutil.rmtree(PRE, ignore_errors=True)
    os.makedirs(os.path.join(FAKE), exist_ok=True)
    shutil.copy2(PROD, os.path.join(CLOUD, MANI_REL))
    m0, _ = manifest()
    guard('预演起点：清单沿用生产的 agent 条目 v%s' % m0['products']['agent']['version'],
          m0['products']['agent']['version'] == '0.6.22', m0['products']['agent']['version'])

    cases = [
        # (说明, version, 期望退出码, 期望发布后清单版本, 额外参数)
        ('A 发内测：alpha.1 接在 0.6.22 之后', '0.6.23-alpha.1', 0, '0.6.23-alpha.1', []),
        ('B 内测晋升候选：rc.1 接在 alpha.1 之后', '0.6.23-rc.1', 0, '0.6.23-rc.1', []),
        ('C 候选退回内测：alpha.5 接在 rc.1 之后', '0.6.23-alpha.5', 1, '0.6.23-rc.1', []),
        ('D 阶段号 0：alpha.0', '0.6.23-alpha.0', 1, '0.6.23-rc.1', []),
        ('E 裸后缀：0.6.23-alpha', '0.6.23-alpha', 1, '0.6.23-rc.1', []),
        ('F 内测转正：0.6.23 接在 rc.1 之后', '0.6.23', 0, '0.6.23', []),
    ]

    print('\n── publish 用例 ──')
    for desc, version, want_rc, want_ver, extra in cases:
        f = make_fake(version)
        rc, out = run(['scripts/publish-release.mjs', 'publish', '--product', 'agent',
                       '--version', version, '--file', f] + extra)
        m, _ = manifest()
        got = m['products']['agent']['version']
        guard('%s → rc=%d' % (desc, rc), rc == want_rc and got == want_ver,
              '清单版本 %s' % got)

    # G. 显式回滚通道
    print('\n── 其它 ──')
    f = make_fake('0.6.23-alpha.5')
    rc, out = run(['scripts/publish-release.mjs', 'publish', '--product', 'agent',
                   '--version', '0.6.23-alpha.5', '--file', f, '--allow-downgrade'])
    m, _ = manifest()
    guard('G 回滚通道：--allow-downgrade 允许 alpha.5（清单 %s）' % m['products']['agent']['version'],
          rc == 0 and m['products']['agent']['version'] == '0.6.23-alpha.5',
          m['products']['agent']['version'])
    # 回滚之后，把线上拨回 0.6.23 作为后续用例的起点
    f = make_fake('0.6.23')
    rc, out = run(['scripts/publish-release.mjs', 'publish', '--product', 'agent',
                   '--version', '0.6.23', '--file', f, '--allow-downgrade'])

    # H. dry-run 不许动清单
    before, _ = manifest()
    f = make_fake('0.7.0-rc.1')
    rc, out = run(['scripts/publish-release.mjs', 'publish', '--product', 'agent',
                   '--version', '0.7.0-rc.1', '--file', f, '--dry-run'])
    after, _ = manifest()
    guard('H --dry-run 不改清单（仍 %s）' % after['products']['agent']['version'],
          rc == 0 and after == before)

    # I0. 根因佐证：JS 唯一实现同样不认 alpha.0（与 C++ 侧"要求 n>=1"同源）
    probe = subprocess.run(
        [NODE, '--input-type=module', '-e',
         "import { isCandidate, cmpVersion } from './scripts/version-compare.mjs';"
         "console.log(JSON.stringify([isCandidate('0.6.23-alpha.0'),"
         " cmpVersion('0.6.23-alpha.0','0.6.23')]));"],
        cwd=ROOT, stdout=subprocess.PIPE)
    val = json.loads(probe.stdout.decode('utf-8', 'replace').strip())
    guard('I0 JS 侧也判 alpha.0 不是候选（isCandidate=%s, cmp与0.6.23=%s ⇒ 等同正式版）'
          % (val[0], val[1]), val[0] is False and val[1] == 0)

    # I. 清单必须是 UTF-8 无 BOM（历史头号坑）
    _, raw = manifest()
    guard('I 清单无 BOM 且可解析', not raw.startswith(b'\xef\xbb\xbf'))

    # J. 三处副本一致（清单 sha / 大小 与磁盘实际文件）
    m, _ = manifest()
    ent = m['products']['agent']
    url_file = os.path.basename(ent['url'])
    disk = os.path.join(PRE, 'assets', 'pkg', url_file)
    ok_disk = os.path.exists(disk) and sha256f(disk) == ent['sha256'] and os.path.getsize(disk) == ent['size']
    guard('J 清单 sha256/size 与 assets 里真文件一致', ok_disk,
          '%s → %d 字节' % (url_file[:44], ent['size']))

    # K. 体检闭环（预演只造了 agent 的包；viewer 那条指向生产 131MB 资产目录，
    #    留着会产生"文件不在磁盘"的假阳性 ⇒ 摘掉后再验 agent 的三路闭环）
    m, _ = manifest()
    m['products'].pop('viewer', None)
    with open(os.path.join(CLOUD, MANI_REL), 'w', encoding='utf-8') as fh:
        fh.write(json.dumps(m, ensure_ascii=False, indent=2) + '\n')
    rc, out = run(['scripts/publish-release.mjs', 'check'])
    guard('K 发布后 check 退出码 0（三路副本闭环）', rc == 0,
          [l for l in out.splitlines() if l.strip().startswith('[agent]')][:1]
          or [l for l in out.splitlines() if '体检结果' in l][:1])

    # L. 生产清单必须原封不动（跑前跑后同一份字节）
    prod_after = sha256f(PROD)
    guard('L 生产 ota.json 未被预演改动（跑前 %s… / 跑后 %s…）'
          % (prod_before[:12], prod_after[:12]), prod_after == prod_before)

    # M. 落盘空间（2026-10-07 真踩的坑：D 盘只剩 62MB，真出包在复制 Qt6WebEngineCore.dll
    #    时 ENOSPC 半途炸掉；而预演用的是 1024 字节假包 ⇒ 14/0 全绿却照样撞墙。
    #    这条把「磁盘够不够」也纳入预演：出一次包要落 zip 本体 + 逐级复制的暂存 + 余量。
    need_unit = max([p['size'] for p in m0['products'].values()] or [1 << 20])
    free = shutil.disk_usage(os.path.dirname(PROD)).free
    need = int(need_unit * SAFE_FACTOR)
    guard('M 落盘空间够发版（剩 %.0f MB / 需 %.0f MB = 最大包 %.0f MB ×%s）'
          % (free / 2 ** 20, need / 2 ** 20, need_unit / 2 ** 20, SAFE_FACTOR),
          free >= need,
          '不够时真出包会在复制中途 ENOSPC（预演用假包看不出来）')

    # 清理
    shutil.rmtree(PRE, ignore_errors=True)

    print('\n──────── 汇总 ────────')
    bad = [r for r in results if not r[1]]
    print('预演用例 %d 条，通过 %d / 失败 %d' % (len(results), len(results) - len(bad), len(bad)))
    for label, ok, _ in bad:
        print('  未过：' + label)
    print('已清理 ' + MANI_REL + ' 临时目录；生产 ota.json 原样。')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
