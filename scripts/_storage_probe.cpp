// 存储用量三分类探针（2026-11-02 · 不入库）
//
// 造可控的临时目录结构，直接调 SettingsDomain::storageBreakdown 真实现，验：
//   A 三栏互斥且并集 == 目录真实占用（这是线上截图的直接反面）
//   B 配置栏只算 dataDir 根下的文件
//   C 日志栏只算 logs/ 与该级 *.log
//   D dataDir 用 "/" 拼、logDir 用 "\" 给，结论不受分隔符影响（本 bug 的回归）
//   E logDir 在 dataDir 之外时仍被计入且不重复
//   F 数据目录不存在时不崩、不报假数
//
// 为什么必须能变红：第一版修复（只把 startsWith 换成路径归一）仍会把所有文件算成
// 日志，因为 g_logPath 默认 dataDir/agent-qt.log 取其 absolutePath 就是 dataDir
// 本身。断言 C 就是为这个盲区设的。
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtGlobal>
#include <cstdio>

#include "domain/settings_domain.h"

static int g_bad = 0;

static void ck(const char *name, bool cond, const QString &extra = QString())
{
    if (cond) std::printf("  ok   %s\n", name);
    else {
        ++g_bad;
        std::printf("  FAIL %s%s%s\n", name,
                    extra.isEmpty() ? "" : "  ← ",
                    extra.isEmpty() ? "" : qPrintable(extra));
    }
}

static void mk(const QString &path, qint64 size)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    QByteArray blob(int(size), 'x');
    f.write(blob);
}

// 目录真实占用（独立实现，刻意不复用被测函数）
static qint64 realSize(const QString &dir)
{
    qint64 t = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); t += it.fileInfo().size(); }
    return t;
}

int main()
{
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::printf("临时目录建不出来\n"); return 1; }
    const QString root = QDir::toNativeSeparators(tmp.path());

    // ── 造结构：模拟真实数据目录 ──
    const QString data = root + "\\xingjikong";
    mk(data + "\\agent.env",                               1024);   // 配置
    mk(data + "\\classcode.txt",                            128);   // 配置
    mk(data + "\\agent-qt.log",                            4096);   // 日志（根级）
    mk(data + "\\agent-qt.log.1",                          2048);   // 日志（根级）
    mk(data + "\\logs\\xingjikong-2026-11-02.log",         8192);   // 日志（logs/ 下）
    mk(data + "\\shots\\a.png",                           65536);   // 数据
    mk(data + "\\ota\\pkg.exe",                         131072);   // 数据
    mk(data + "\\incoming\\b.bin",                       32768);   // 数据

    const qint64 cfgExpect  = 1024 + 128;
    const qint64 logExpect  = 4096 + 2048 + 8192;
    const qint64 dataExpect = 65536 + 131072 + 32768;
    const qint64 realTotal  = realSize(data);

    // ── A/B/C：标准情形（logDir 就在 dataDir 里，且**等于** dataDir —— 真实默认）──
    {
        const SettingsDomain::StorageBreakdown b =
            SettingsDomain::storageBreakdown(data, data);
        std::printf("[标准情形] config=%lld data=%lld logs=%lld total=%lld real=%lld\n",
                    b.config, b.data, b.logs, b.total(), realTotal);
        ck("A 三栏之和 == 目录真实占用", b.total() == realTotal,
           QStringLiteral("%1 vs %2").arg(b.total()).arg(realTotal));
        ck("A' 三栏互不相等（线上截图三栏相同 = bug）",
           !(b.config == b.data && b.data == b.logs));
        ck("B 配置栏 == 根下配置文件之和", b.config == cfgExpect,
           QStringLiteral("%1 vs %2").arg(b.config).arg(cfgExpect));
        ck("C 日志栏 == logs/ + 根级 *.log", b.logs == logExpect,
           QStringLiteral("%1 vs %2").arg(b.logs).arg(logExpect));
        ck("C' 数据栏 == 其余子目录之和", b.data == dataExpect,
           QStringLiteral("%1 vs %2").arg(b.data).arg(dataExpect));
    }

    // ── D：分隔符不一致（本 bug 的正面回归）──
    {
        QString slashData = data;
        slashData.replace('\\', '/');
        const QString backLog = QDir::toNativeSeparators(data);
        const SettingsDomain::StorageBreakdown b =
            SettingsDomain::storageBreakdown(slashData, backLog);
        std::printf("[分隔符混用] config=%lld data=%lld logs=%lld\n",
                    b.config, b.data, b.logs);
        ck("D '/' 拼的 dataDir 与 '\\' 给的 logDir 结论一致",
           b.total() == realTotal && b.logs == logExpect,
           QStringLiteral("total=%1 logs=%2").arg(b.total()).arg(b.logs));
    }

    // ── E：logDir 在 dataDir 之外 ──
    {
        const QString ext = root + "\\outside-logs";
        mk(ext + "\\x.log", 16384);
        const SettingsDomain::StorageBreakdown b =
            SettingsDomain::storageBreakdown(data, ext);
        std::printf("[日志在树外] config=%lld data=%lld logs=%lld\n",
                    b.config, b.data, b.logs);
        ck("E 树外日志被计入且不重复",
           b.logs == logExpect + 16384 && b.total() == realTotal + 16384,
           QStringLiteral("logs=%1 total=%2").arg(b.logs).arg(b.total()));
    }

    // ── F：数据目录不存在 ──
    {
        const SettingsDomain::StorageBreakdown b =
            SettingsDomain::storageBreakdown(root + "\\不存在的目录", root + "\\outside-logs");
        std::printf("[数据目录不存在] config=%lld data=%lld logs=%lld\n",
                    b.config, b.data, b.logs);
        ck("F 不崩、不报假数（配置与数据为 0，日志照实）",
           b.config == 0 && b.data == 0 && b.logs == 16384,
           QStringLiteral("logs=%1").arg(b.logs));
    }

    std::printf(g_bad == 0 ? "\n全部通过（%d 项断言）\n" : "\n%d 项失败\n", g_bad == 0 ? 0 : g_bad);
    if (g_bad == 0) std::printf("STORAGE PROBE OK\n");
    return g_bad == 0 ? 0 : 20;
}
