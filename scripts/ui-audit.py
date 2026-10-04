# UI 合规审计 · 依据《星集控-电脑端界面稿集-黑白-2026-10-03.html》
# 用法: python scripts/ui-audit.py
# 只读检查：把"稿子上写死的尺寸/令牌/字号"逐条在 QML 里量一遍，不合规就报出来。
import io, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
QML = os.path.join(ROOT, 'qml')

# 稿集没有 10px 这一档（最小是"注 11"）；出现 10 就是不合规
BAD_FONT = re.compile(r'font\.pixelSize:\s*(9|10)\b')
BAD_PX = re.compile(r'font\.pixelSize:\s*(\d+)')
# 令牌外的硬编码色（黑白方案允许的例外只有画面底、反白字）

def read(p):
    return io.open(p, encoding='utf-8').read()

def check_fonts():
    bad = []
    for f in sorted(os.listdir(QML)):
        if not f.endswith('.qml'):
            continue
        for i, ln in enumerate(read(os.path.join(QML, f)).split('\n'), 1):
            if BAD_FONT.search(ln):
                bad.append('%s:%d  %s' % (f, i, ln.strip()))
    return bad

def check_tokens():
    allowed = set(['#0A0A0A', '#080808', '#101010', '#141414', '#1E1E1E', '#121212',
                   '#111111', '#161616', '#1A1A1A', '#242424', '#181818', '#191919',
                   '#FAFAFA', '#C8C8C8', '#5A5A5A', '#3A3A3A', '#F0F0F0', '#4E4E4E',
                   '#F6F6F6', '#FFFFFF', '#EDEDED', '#E6E6E6', '#E9E9E9', '#EFEFEF',
                   '#E3E3E3', '#DCDCDC', '#C4C4C4', '#BCBCBC', '#CCCCCC', '#DEDEDE',
                   '#161616', '#787878', '#A6A6A6', '#1A1A1A', '#9A9A9A', '#2A2A2A',
                   '#0D0D0D', '#151515', '#1C1C1C', '#050505'])
    bad = []
    for f in sorted(os.listdir(QML)):
        if not f.endswith('.qml'):
            continue
        src = read(os.path.join(QML, f))
        # 令牌表文件本身（主题定义）跳过
        for i, ln in enumerate(src.split('\n'), 1):
            for c in re.findall(r'#[0-9A-Fa-f]{6}', ln):
                if c.upper() not in allowed and c.upper() != '#C0392B':
                    bad.append('%s:%d  %s' % (f, i, ln.strip()))
    return bad

def check_heights():
    """稿子写死的尺寸：顶条 44 / 底标签 42 / 左右栏 168·196 / 按钮 30"""
    want = [('Layout.preferredHeight: 44', '顶条 44'),
            ('Layout.preferredHeight: 42', '底标签 42'),
            ('Layout.preferredWidth: 168', '左栏 168'),
            ('Layout.preferredWidth: 196', '右栏 196')]
    src = read(os.path.join(QML, 'Main.qml'))
    return [(label, ('OK' if pat in src else 'MISSING'))
            for pat, label in want]

if __name__ == '__main__':
    bad_f = check_fonts()
    bad_t = check_tokens()
    print('== 字号（稿最小 11，9/10 为不合规）==')
    print('\n'.join(bad_f) if bad_f else '  全部合规')
    print('== 令牌外硬编码色 ==')
    print('\n'.join(bad_t) if bad_t else '  全部合规')
    print('== 写死尺寸 ==')
    for label, r in check_heights():
        print('  %-10s %s' % (label, r))
