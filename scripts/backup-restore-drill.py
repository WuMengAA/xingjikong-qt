#!/usr/bin/env python3
# 备份恢复演练：隔离目录里造 4 个真 SQLite 库 + 流水 + 报修 → 快照 → 删库 → 恢复 → 回读。
# 不碰生产库、不重启服务、不用 cmd；只调 node scripts/backup.mjs 与 Python 标准库。
import json
import os
import pathlib
import shutil
import sqlite3
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BACKUP = ROOT / 'scripts' / 'backup.mjs'
DRILL = pathlib.Path('D:/Stelarith/_demo/drill_root')
DB_ENV = {
    'CLOUD_QUEUE_DB_FILE': 'instruction-queue.db',
    'CLOUD_CLASS_DIR_DB_FILE': 'class-dir.db',
    'CLOUD_CLASS_ROOM_DB_FILE': 'class-room.db',
    'CLOUD_INTERACT_DB_FILE': 'interact.db',
}
DB_TAGS = {
    'instruction-queue.db': 'queue-20261115',
    'class-dir.db': 'dir-20261115',
    'class-room.db': 'room-20261115',
    'interact.db': 'interact-20261115',
}
PLAIN = {
    'events.log': b'EVENT before-drill\n',
    'events.log.1': b'EVENT rotated-archive\n',
    'repairs.json': b'{"items":[{"id":1,"text":"before-drill"}]}\n',
}
ALL_FILES = list(DB_TAGS) + list(PLAIN)
PASS = 0
FAILS = []


def check(name, fn):
    global PASS
    try:
        fn()
        PASS += 1
    except Exception as e:
        FAILS.append(name + ' :: ' + str(e))


def env_clean():
    env = dict(os.environ)
    for k in list(env):
        if k.startswith('CLOUD_') and k.endswith('_DB_FILE'):
            del env[k]
    env.update(DB_ENV)
    env['PYTHONIOENCODING'] = 'utf-8'
    return env


def run_node(args, env):
    q = subprocess.run(['node'] + args, capture_output=True, env=env)
    out = (q.stdout or b'').decode('utf-8', 'replace') + (q.stderr or b'').decode('utf-8', 'replace')
    if q.returncode != 0:
        raise RuntimeError('node rc=' + str(q.returncode) + ' ' + out[-800:])
    return out


def make_db(path, tag):
    con = sqlite3.connect(path)
    con.execute('CREATE TABLE drill (k TEXT PRIMARY KEY, v TEXT NOT NULL)')
    con.execute('INSERT INTO drill(k, v) VALUES (?, ?)', ('sentinel', tag))
    con.commit()
    con.close()


def read_tag(path):
    con = sqlite3.connect(path)
    row = con.execute("SELECT v FROM drill WHERE k = 'sentinel'").fetchone()
    con.close()
    return row[0] if row else None


def main():
    if DRILL.exists():
        shutil.rmtree(DRILL, ignore_errors=True)
    DRILL.mkdir(parents=True, exist_ok=True)
    for name, tag in DB_TAGS.items():
        make_db(DRILL / name, tag)
    for name, data in PLAIN.items():
        (DRILL / name).write_bytes(data)
    env = env_clean()
    out = run_node([str(BACKUP), '--root', str(DRILL)], env)
    check('backup.mjs 打印完成行', lambda: (_ for _ in ()).throw(AssertionError('no completion')) if '备份完成' not in out else None)
    lines = [l for l in out.splitlines() if l.strip().startswith('✓') or '备份完成' in l]
    check('快照打印了拷贝项', lambda: (_ for _ in ()).throw(AssertionError('no copy lines')) if len(lines) < 7 else None)
    backups = sorted((DRILL / 'backups').glob('*'))
    check('生成了备份目录', lambda: (_ for _ in ()).throw(AssertionError('no backup dir')) if not backups else None)
    dest = backups[-1]
    manifest = json.loads((dest / 'backup-manifest.json').read_text(encoding='utf-8'))
    check('manifest version=1', lambda: (_ for _ in ()).throw(AssertionError('bad version')) if manifest.get('version') != 1 else None)
    keys = set(i['key'] for i in manifest['items'])
    for k in ['queue', 'classRoom', 'classDir', 'interact', 'events', 'repairs', 'eventsRotate']:
        check('manifest 含 ' + k, lambda k=k: (_ for _ in ()).throw(AssertionError(k)) if k not in keys else None)
    for name in ALL_FILES:
        check('快照含 ' + name, lambda name=name: (_ for _ in ()).throw(AssertionError(name)) if not (dest / name).exists() else None)
    list_out = run_node([str(BACKUP), '--root', str(DRILL), '--list'], env)
    check('--list 能看到快照', lambda: (_ for _ in ()).throw(AssertionError('empty list')) if dest.name not in list_out else None)
    originals = {name: (DRILL / name).read_bytes() for name in ALL_FILES}
    for name in ALL_FILES:
        p = DRILL / name
        if p.exists():
            p.unlink()
    check('删库后 4 库 + 3 文件全不在', lambda: (_ for _ in ()).throw(AssertionError('still exists')) if any((DRILL / n).exists() for n in ALL_FILES) else None)
    for item in manifest['items']:
        src = dest / item['file']
        if src.exists():
            shutil.copy2(src, DRILL / item['file'])
    check('恢复后 4 库 + 3 文件全回来', lambda: (_ for _ in ()).throw(AssertionError('missing')) if any(not (DRILL / n).exists() for n in ALL_FILES) else None)
    for name, tag in DB_TAGS.items():
        check('SQLite 回读 ' + name, lambda name=name, tag=tag: (_ for _ in ()).throw(AssertionError(name + '=' + str(read_tag(DRILL / name)))) if read_tag(DRILL / name) != tag else None)
    for name, data in PLAIN.items():
        check('字节回读 ' + name, lambda name=name, data=data: (_ for _ in ()).throw(AssertionError(name)) if (DRILL / name).read_bytes() != data else None)
    check('原始字节与恢复字节一致', lambda: (_ for _ in ()).throw(AssertionError('drift')) if any((DRILL / n).read_bytes() != originals[n] for n in ALL_FILES) else None)
    # 反向场景：空目录不能产出假快照；新装实例只有一个文件时也能备份。
    empty = DRILL / 'empty_root'
    empty.mkdir(exist_ok=True)
    q = subprocess.run(['node', str(BACKUP), '--root', str(empty)], capture_output=True, env=env)
    check('空目录备份 exit 1', lambda: (_ for _ in ()).throw(AssertionError('rc=' + str(q.returncode))) if q.returncode != 1 else None)
    check('空目录不留下快照', lambda: (_ for _ in ()).throw(AssertionError('left dir')) if list((empty / 'backups').glob('*')) else None)
    sparse = DRILL / 'sparse_root'
    sparse.mkdir(exist_ok=True)
    (sparse / 'repairs.json').write_bytes(PLAIN['repairs.json'])
    sparse_out = run_node([str(BACKUP), '--root', str(sparse)], env)
    sb = sorted((sparse / 'backups').glob('*'))[-1]
    sm = json.loads((sb / 'backup-manifest.json').read_text(encoding='utf-8'))
    check('稀疏实例只备份存在项', lambda: (_ for _ in ()).throw(AssertionError(str(len(sm['items'])))) if len(sm['items']) != 1 else None)
    check('稀疏实例项是报修台账', lambda: (_ for _ in ()).throw(AssertionError('key')) if sm['items'][0]['key'] != 'repairs' else None)
    check('稀疏实例打印完成', lambda: (_ for _ in ()).throw(AssertionError('no done')) if '备份完成' not in sparse_out else None)

    print('DRILL_ROOT ' + str(DRILL))
    print('DRILL PASS ' + str(PASS) + ' / FAIL ' + str(len(FAILS)))
    for f in FAILS:
        print('FAIL ' + f)
    return 1 if FAILS else 0


if __name__ == '__main__':
    raise SystemExit(main())
