// P3 长期记录查询/展示对话框（2026-10-30 main 拆分 #6b 迁出，逻辑逐字保留）。
// 只读展示端：噪音/自习长期记录的按天汇总与话术生成，纯展示无写入。

#include "p3_reader.h"

#include "p3_log.h"   // dataRoot()
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QIODevice>
#include <QByteArray>
#include <QTextStream>
#include <QMap>
#include <QVector>
#include <QDateTime>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QStringList>
#include <QLatin1Char>

// 读 subdir 下全部 YYYY-MM-DD.jsonl，逐行解析成数组（跨日汇总用）。
QJsonArray readJsonlDir(const QString &subdir)
{
    QJsonArray arr;
    const QString dir = dataRoot() + QLatin1Char('/') + subdir;
    QDir d(dir);
    if (!d.exists()) return arr;
    for (const QFileInfo &fi : d.entryInfoList(QStringList() << QStringLiteral("*.jsonl"), QDir::Files, QDir::Name)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        while (!f.atEnd()) {
            const QByteArray line = f.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            if (!o.isEmpty()) arr.append(o);
        }
    }
    return arr;
}

void openFolder(const QString &subdir)
{
    const QString dir = dataRoot() + QLatin1Char('/') + subdir;
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void openNoiseLogDialog()
{
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("噪音记录"));
    dlg.resize(520, 420);
    auto *lay = new QVBoxLayout(&dlg);
    auto *te = new QTextEdit(&dlg);
    te->setReadOnly(true);
    lay->addWidget(te);

    const QJsonArray all = readJsonlDir(QStringLiteral("noise_log"));
    if (all.isEmpty()) {
        te->setPlainText(QStringLiteral("还没有噪音记录。\n开启「噪音监测」或「自习模式」后，每次采样都会记到这里。"));
    } else {
        QMap<QString, QVector<double>> byDay;
        for (const QJsonValue &v : all) {
            const QJsonObject o = v.toObject();
            const qint64 ts = o.value(QStringLiteral("ts")).toVariant().toLongLong();
            const QString day = QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd"));
            byDay[day].append(o.value(QStringLiteral("db")).toDouble());
        }
        QString out; QTextStream ts(&out);
        ts << QStringLiteral("共 %1 天、%2 条采样\n\n").arg(byDay.size()).arg(all.size());
        for (auto it = byDay.begin(); it != byDay.end(); ++it) {
            const QVector<double> &v = it.value();
            double sum = 0, mn = 1e9, mx = -1e9;
            for (double d : v) { sum += d; if (d < mn) mn = d; if (d > mx) mx = d; }
            ts << QStringLiteral("%1：采样 %2 次，均值 %3 dB，区间 [%4, %5] dB\n")
                  .arg(it.key()).arg(v.size())
                  .arg(QString::number(sum / v.size(), 'f', 1))
                  .arg(QString::number(mn, 'f', 1)).arg(QString::number(mx, 'f', 1));
        }
        te->setPlainText(out);
    }

    auto *row = new QHBoxLayout();
    auto *btnOpen = new QPushButton(QStringLiteral("打开记录文件夹"), &dlg);
    auto *btnClose = new QPushButton(QStringLiteral("关闭"), &dlg);
    row->addWidget(btnOpen); row->addStretch(1); row->addWidget(btnClose);
    lay->addLayout(row);
    QObject::connect(btnOpen, &QPushButton::clicked, &dlg, [] { openFolder(QStringLiteral("noise_log")); });
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void openStudyLogDialog()
{
    QDialog dlg;
    dlg.setWindowTitle(QStringLiteral("自习记录"));
    dlg.resize(520, 420);
    auto *lay = new QVBoxLayout(&dlg);
    auto *te = new QTextEdit(&dlg);
    te->setReadOnly(true);
    lay->addWidget(te);

    const QJsonArray all = readJsonlDir(QStringLiteral("study_log"));
    if (all.isEmpty()) {
        te->setPlainText(QStringLiteral("还没有自习记录。\n开启「自习模式」后，每次会话（开始/结束/时长/异常次数）都会记到这里。"));
    } else {
        QVector<QJsonObject> sessions;
        QMap<qint64, QJsonObject> pending;
        for (const QJsonValue &v : all) {
            QJsonObject o = v.toObject();
            const QString ev = o.value(QStringLiteral("event")).toString();
            if (ev == QStringLiteral("start")) {
                pending.insert(o.value(QStringLiteral("ts")).toVariant().toLongLong(), o);
            } else if (ev == QStringLiteral("stop")) {
                const qint64 st = o.value(QStringLiteral("startTs")).toVariant().toLongLong();
                QJsonObject s = pending.take(st);
                s.insert(QStringLiteral("endTs"), o.value(QStringLiteral("ts")));
                s.insert(QStringLiteral("durationSec"), o.value(QStringLiteral("durationSec")));
                s.insert(QStringLiteral("alertCount"), o.value(QStringLiteral("alertCount")));
                sessions.append(s);
            }
        }
        QString out; QTextStream ts(&out);
        ts << QStringLiteral("共 %1 次自习会话\n\n").arg(sessions.size());
        for (const QJsonObject &s : sessions) {
            const qint64 st = s.value(QStringLiteral("ts")).toVariant().toLongLong();
            const int dur = s.value(QStringLiteral("durationSec")).toInt();
            const int al = s.value(QStringLiteral("alertCount")).toInt();
            ts << QStringLiteral("%1：时长 %2 分 %3 秒，异常录制 %4 次（阈值 %5 dB）\n")
                  .arg(QDateTime::fromSecsSinceEpoch(st).toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                  .arg(dur / 60).arg(dur % 60).arg(al)
                  .arg(QString::number(s.value(QStringLiteral("thresholdDb")).toDouble(), 'f', 0));
        }
        te->setPlainText(out);
    }

    auto *row = new QHBoxLayout();
    auto *btnOpen = new QPushButton(QStringLiteral("打开记录文件夹"), &dlg);
    auto *btnClose = new QPushButton(QStringLiteral("关闭"), &dlg);
    row->addWidget(btnOpen); row->addStretch(1); row->addWidget(btnClose);
    lay->addLayout(row);
    QObject::connect(btnOpen, &QPushButton::clicked, &dlg, [] { openFolder(QStringLiteral("study_log")); });
    QObject::connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

// 记录文本（照 openNoiseLogDialog / openStudyLogDialog 的生成口径）
QString buildNoiseLogText()
{
    const QJsonArray all = readJsonlDir(QStringLiteral("noise_log"));
    if (all.isEmpty())
        return QStringLiteral("还没有噪音记录。\n开启「噪音监测」或「自习模式」后，每次采样都会记到这里。");
    QMap<QString, QVector<double>> byDay;
    for (const QJsonValue &v : all) {
        const QJsonObject o = v.toObject();
        const qint64 ts = o.value(QStringLiteral("ts")).toVariant().toLongLong();
        byDay[QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd"))]
            .append(o.value(QStringLiteral("db")).toDouble());
    }
    QString out; QTextStream ts(&out);
    ts << QStringLiteral("共 %1 天、%2 条采样\n\n").arg(byDay.size()).arg(all.size());
    for (auto it = byDay.begin(); it != byDay.end(); ++it) {
        const QVector<double> &v = it.value();
        double sum = 0, mn = 1e9, mx = -1e9;
        for (double d : v) { sum += d; if (d < mn) mn = d; if (d > mx) mx = d; }
        ts << QStringLiteral("%1：采样 %2 次，均值 %3 dB，区间 [%4, %5] dB\n")
              .arg(it.key()).arg(v.size())
              .arg(QString::number(sum / v.size(), 'f', 1))
              .arg(QString::number(mn, 'f', 1)).arg(QString::number(mx, 'f', 1));
    }
    return out;
}
QString buildStudyLogText()
{
    const QJsonArray all = readJsonlDir(QStringLiteral("study_log"));
    if (all.isEmpty())
        return QStringLiteral("还没有自习记录。\n开启「自习模式」后，每次会话（开始/结束/时长/异常次数）都会记到这里。");
    QVector<QJsonObject> sessions;
    QMap<qint64, QJsonObject> pending;
    for (const QJsonValue &v : all) {
        QJsonObject o = v.toObject();
        const QString ev = o.value(QStringLiteral("event")).toString();
        if (ev == QStringLiteral("start")) {
            pending.insert(o.value(QStringLiteral("ts")).toVariant().toLongLong(), o);
        } else if (ev == QStringLiteral("stop")) {
            const qint64 st = o.value(QStringLiteral("startTs")).toVariant().toLongLong();
            QJsonObject s = pending.take(st);
            s.insert(QStringLiteral("endTs"), o.value(QStringLiteral("ts")));
            s.insert(QStringLiteral("durationSec"), o.value(QStringLiteral("durationSec")));
            s.insert(QStringLiteral("alertCount"), o.value(QStringLiteral("alertCount")));
            sessions.append(s);
        }
    }
    QString out; QTextStream ts(&out);
    ts << QStringLiteral("共 %1 次自习会话\n\n").arg(sessions.size());
    for (const QJsonObject &s : sessions) {
        const qint64 st = s.value(QStringLiteral("ts")).toVariant().toLongLong();
        const int dur = s.value(QStringLiteral("durationSec")).toInt();
        ts << QStringLiteral("%1：时长 %2 分 %3 秒，异常录制 %4 次（阈值 %5 dB）\n")
              .arg(QDateTime::fromSecsSinceEpoch(st).toString(QStringLiteral("yyyy-MM-dd HH:mm")))
              .arg(dur / 60).arg(dur % 60)
              .arg(s.value(QStringLiteral("alertCount")).toInt())
              .arg(QString::number(s.value(QStringLiteral("thresholdDb")).toDouble(), 'f', 0));
    }
    return out;
}