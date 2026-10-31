// P3 记录读取的「错误态」探针
//
// 守的是什么：readJsonlDir 把三种完全不同的原因**折叠成同一个"空"**——
//   ① 目录不存在        = 还没启用过功能          → 该显示「还没有记录」
//   ② 目录在、文件打不开 = 故障（权限 / 被占用）    → 该显示错误态
//   ③ 文件在、JSON 全坏 = 数据损坏                 → 该显示错误态
// 旧实现三者都返回空数组，界面一律显示「还没有噪音记录」。
// 在 ②③ 下用户的记录**可能完好地躺在磁盘上**，界面却告诉他没有 —— 这是让"有记录"
// 这个状态本身变成谎话，比报错更糟（用户不会去修，也不会去找回）。
//
// 本探针用临时目录造出四种场景，断言诊断分类正确。
// 用法：python scripts/_p3_reader_probe.py
#include "p3_reader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTemporaryDir>
#include <QDebug>

static int fails = 0;
static void chk(const char *name, bool ok, const QString &detail)
{
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << name << " <- " << detail;
    if (!ok) ++fails;
}

static void writeFile(const QString &path, const QString &content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "写测试文件失败:" << path;
        return;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << content;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) { qWarning() << "临时目录创建失败"; return 2; }

    // ══════════ 场景 ①：目录根本不存在 → 应判 Ok（真的没数据）══════════
    {
        const QString sub = QStringLiteral("noise_log_missing");
        const P3ReadResult r = readJsonlDirWithDiag(tmp.path(), sub);
        chk("① 目录不存在 → Ok（不是错误）",
            r.status == P3ReadResult::Ok && r.records.isEmpty(),
            QStringLiteral("status=%1 reason=%2").arg(int(r.status)).arg(r.reason));
    }

    // ══════════ 场景 ②：目录在、有正常文件 → Ok + 解析出记录 ══════════
    {
        const QString sub = QStringLiteral("noise_log_good");
        QDir().mkpath(tmp.path() + "/" + sub);
        writeFile(tmp.path() + "/" + sub + "/2026-10-31.jsonl",
                  "{\"ts\":1761883200,\"db\":45.5}\n{\"ts\":1761883201,\"db\":52.0}\n");
        const P3ReadResult r = readJsonlDirWithDiag(tmp.path(), sub);
        chk("② 正常文件 → Ok 且解析出 2 条",
            r.status == P3ReadResult::Ok && r.records.size() == 2,
            QStringLiteral("status=%1 size=%2").arg(int(r.status)).arg(r.records.size()));
    }

    // ══════════ 场景 ③：文件在、内容全是坏 JSON → 应判 Corrupt ════════
    {
        const QString sub = QStringLiteral("noise_log_corrupt");
        QDir().mkpath(tmp.path() + "/" + sub);
        writeFile(tmp.path() + "/" + sub + "/2026-10-31.jsonl",
                  "this is not json\n{broken\n%%%%\n");
        const P3ReadResult r = readJsonlDirWithDiag(tmp.path(), sub);
        chk("③ 全坏 JSON → Corrupt（不是「没有记录」）",
            r.status == P3ReadResult::Corrupt,
            QStringLiteral("status=%1 badLines=%2").arg(int(r.status)).arg(r.badLines));
        chk("③ 坏行数被数出来（用于上屏说明）",
            r.badLines == 3,
            QStringLiteral("badLines=%1（期望 3）").arg(r.badLines));
    }

    // ══════════ 场景 ④：好行 + 坏行混合 → 仍可用，但要记下坏行数 ══════
    {
        const QString sub = QStringLiteral("noise_log_mixed");
        QDir().mkpath(tmp.path() + "/" + sub);
        writeFile(tmp.path() + "/" + sub + "/2026-10-31.jsonl",
                  "{\"ts\":1761883200,\"db\":45.5}\nGARBAGE-LINE\n{\"ts\":1761883202,\"db\":50.0}\n");
        const P3ReadResult r = readJsonlDirWithDiag(tmp.path(), sub);
        chk("④ 好坏混合 → Ok 且保留好行、记下 1 坏行",
            r.status == P3ReadResult::Ok && r.records.size() == 2 && r.badLines == 1,
            QStringLiteral("status=%1 size=%2 bad=%3")
                .arg(int(r.status)).arg(r.records.size()).arg(r.badLines));
    }

    // ══════════ 场景 ⑤：目录存在、但只有子目录没有 .jsonl → Ok 空态 ══════
    // （说明"没有证据说是故障时不要吓用户"这条判断在这里成立）
    {
        const QString sub = QStringLiteral("noise_log_subdir_only");
        QDir().mkpath(tmp.path() + "/" + sub + "/2026-11-01.jsonl");   // 同名子目录
        const P3ReadResult r = readJsonlDirWithDiag(tmp.path(), sub);
        chk("⑤ 目录在但无 .jsonl 文件 → Ok 空态（不是错误）",
            r.status == P3ReadResult::Ok && r.records.isEmpty(),
            QStringLiteral("status=%1").arg(int(r.status)));
    }

    // ══════════ 场景 ⑥：host 为空 → Missing（环境故障，不是"没数据"）══════
    {
        const P3ReadResult r = readJsonlDirWithDiag(QString(), QStringLiteral("noise_log"));
        chk("⑥ 数据根为空 → Missing 且给出原因与影响",
            r.status == P3ReadResult::Missing && !r.reason.isEmpty() && !r.impact.isEmpty(),
            QStringLiteral("status=%1 reason=%2").arg(int(r.status)).arg(r.reason.left(20)));
    }

    // ══════════ 场景 ⑦：兼容性——旧 readJsonlDir 签名行为不变 ══════════
    {
        const QString sub = QStringLiteral("noise_log_good");
        const QJsonArray old = readJsonlDir(tmp.path(), sub);
        chk("⑦ 旧 readJsonlDir(host, sub) 仍可用且返回同样的 2 条",
            old.size() == 2, QStringLiteral("size=%1").arg(old.size()));
    }

    qInfo().noquote() << "";
    qInfo().noquote() << (fails == 0 ? "[结论] 诊断分类正确：错误不再被折叠成「没有记录」"
                                     : "[结论] 有断言失败");
    return fails == 0 ? 0 : 1;
}
