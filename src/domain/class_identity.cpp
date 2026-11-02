#include "class_identity.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
// 域内单例状态：路径 + 两个全局变量的地址 + 名变更回调。
// 用指针而非引用成员，是因为 init() 之后才有效 —— 未 init 时所有写操作都变成
// 静默 no-op（而不是崩），这与"服务态/探针"下的容错口径一致。
QString g_dir;
QString *g_codeRef = nullptr;
QString *g_nameRef = nullptr;
void (*g_onNameChanged)(const QString &) = nullptr;

inline bool ready() { return g_codeRef && g_nameRef && !g_dir.isEmpty(); }

QString pathOf(const char *name)
{
    return g_dir.isEmpty() ? QString() : (g_dir + QLatin1Char('/') + QLatin1String(name));
}

// 读一个纯文本小文件；不存在/为空都回空串（这是"没有"的两种合法表示）。
QString readTrimmed(const QString &path)
{
    if (path.isEmpty()) return QString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const QString s = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    return s;
}

// 写一个纯文本小文件（目录不存在就建）。
// 为什么不复用 settings_domain 的备份逻辑：那套是"多文件打包"，这里是"单值落盘"，
// 混在一起会让备份路径也跟着改行为 —— 备份不该因为某个值改写法而变。
bool writeText(const QString &path, const QString &text)
{
    if (path.isEmpty()) return false;
    const QFileInfo fi(path);
    QDir dir = fi.absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(text.toUtf8());
    f.close();
    return true;
}
} // namespace

namespace ClassIdentity {

void init(const QString &configDir,
          QString &classCode,
          QString &className,
          void (*onNameChanged)(const QString &name))
{
    g_dir = configDir;
    g_codeRef = &classCode;
    g_nameRef = &className;
    g_onNameChanged = onNameChanged;
}

QString codePath() { return pathOf("classcode.txt"); }
QString namePath() { return pathOf("classname.txt"); }

void loadPersisted()
{
    if (!ready()) return;
    const QString c = readTrimmed(codePath());
    if (!c.isEmpty()) {
        *g_codeRef = c;
        qInfo("[agent-qt] 已载入班级码 %s", qPrintable(c));
    }
    const QString n = readTrimmed(namePath());
    if (!n.isEmpty()) {
        *g_nameRef = n;
        qInfo("[agent-qt] 已载入班级名 %s", qPrintable(n));
    }
}

void saveCode(const QString &code)
{
    if (!ready()) return;
    const QString c = code.trimmed();
    if (c.isEmpty()) return;   // 空码不是"清空" —— 清空是 clearAll() 的职责
    *g_codeRef = c;
    if (writeText(codePath(), c)) {
        qInfo("[agent-qt] 已保存班级码 → %s", qPrintable(codePath()));
    } else {
        qWarning("[agent-qt] ⚠️ 无法保存班级码：%s", qPrintable(codePath()));
    }
}

void saveName(const QString &name)
{
    if (!ready()) return;
    const QString n = name.trimmed();
    if (n.isEmpty()) return;   // 空名不覆盖已有（云端没带名时别把旧的抹掉）
    *g_nameRef = n;
    if (g_onNameChanged) g_onNameChanged(n);
    if (!writeText(namePath(), n)) {
        qWarning("[agent-qt] ⚠️ 无法保存班级名：%s", qPrintable(namePath()));
    }
}

bool clearAll()
{
    if (!ready()) return false;
    const bool hadCode = !g_codeRef->isEmpty();
    const bool hadName = !g_nameRef->isEmpty();
    if (hadCode) {
        g_codeRef->clear();
        QFile::remove(codePath());
    }
    if (hadName) {
        g_nameRef->clear();
        QFile::remove(namePath());
        if (g_onNameChanged) g_onNameChanged(QString());   // 菜单栏别挂着旧班名
    }
    return hadCode || hadName;
}

bool applyBind(const QString &code, const QString &name)
{
    if (!ready()) return false;
    const QString c = code.trimmed();
    if (c.isEmpty()) return false;
    const QString n = name.trimmed();
    const bool codeChanged = (c != *g_codeRef);
    if (codeChanged) saveCode(c);
    if (!n.isEmpty()) saveName(n);
    return codeChanged;
}

} // namespace ClassIdentity
