#!/usr/bin/env python3
# 文档生成块同步：把 src/class-catalog.js 的五个块写进开发文档的 CATALOG 标记。
# 用法：python scripts/sync-class-doc.py            # 重生成
#       python scripts/sync-class-doc.py --check    # 只比对，漂移 exit 1
import json
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DOC = ROOT / 'docs' / '开发文档-班级后台-2026-11-15.md'
RENDER = ROOT / 'scripts' / 'class-catalog-blocks.mjs'
# \n? 让「空占位」与「已生成」两种形态都能匹配；捕获组不含尾随换行。
PAT = re.compile(r'<!-- CATALOG:begin:([a-z]+) -->\n(.*?)\n?<!-- CATALOG:end:\1 -->', re.S)


def render():
    env = dict(os.environ, PYTHONIOENCODING='utf-8')
    q = subprocess.run(['node', str(RENDER)], capture_output=True, env=env)
    if q.returncode != 0:
        sys.stderr.write((q.stderr or b'').decode('utf-8', 'replace'))
        raise SystemExit('CATALOG_RENDER_FAIL')
    return json.loads((q.stdout or b'').decode('utf-8'))['blocks']


def main():
    blocks = render()
    text = DOC.read_bytes().decode('utf-8')
    seen = {}

    def repl(m):
        key = m.group(1)
        seen[key] = m.group(2)
        want = blocks.get(key)
        if want is None:
            return m.group(0)
        return '<!-- CATALOG:begin:' + key + ' -->\n' + want + '\n<!-- CATALOG:end:' + key + ' -->'

    out = PAT.sub(repl, text)
    missing = sorted(set(blocks) - set(seen))
    extra = sorted(set(seen) - set(blocks))
    drift = sorted(k for k in blocks if k in seen and seen[k] != blocks[k])
    bad = missing or extra or drift
    if '--check' in sys.argv[1:]:
        if bad:
            print('CATALOG_DOC FAIL missing=' + str(missing) + ' extra=' + str(extra) + ' drift=' + str(drift))
            return 1
        print('CATALOG_DOC PASS ' + str(len(seen)) + ' blocks')
        return 0
    if missing:
        print('CATALOG_DOC FAIL 缺标记：' + str(missing))
        return 1
    if out != text:
        DOC.write_bytes(out.encode('utf-8'))
    print('CATALOG_DOC SYNCED ' + str(len(seen)) + ' blocks; drift=' + str(drift))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
