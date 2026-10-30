// P3 长期记录：噪音/自习会话 本地落盘内核（2026-10-30 main 拆分 #6a 迁出，逻辑逐字保留）。
// 落盘目录统一在 AppDataLocation/Stelarith 下（与现存 island_history 同族，独立子目录）。

#include "p3_log.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDate>
#include <QDateTime>
#include <QLatin1Char>
#include <QIODevice>

QString dataRoot()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/Stelarith");
    QDir().mkpath(dir);
    return dir;
}

// 追加一行 JSON（Compact + 换行）到 subdir/YYYY-MM-DD.jsonl。
void appendJsonl(const QString &subdir, const QJsonObject &o)
{
    const QString dir = dataRoot() + QLatin1Char('/') + subdir;
    QDir().mkpath(dir);
    // 2026-10-21（审查 #13）：按天文件从不清理 —— 500ms 采样一天 ~13MB、一学期 2GB+，
    //   而查看端（openNoiseLogDialog/buildNoiseLogText）是把**所有文件全读进内存**再
    //   统计的，学期末点开必卡。每天**首次写入**时清掉 7 天前的旧文件（只清 jsonl，
    //   不碰当天/近几天；一天一次，不增加高频写路径的开销）。
    {
        static QDate lastPruned;
        const QDate today = QDate::currentDate();
        if (lastPruned != today) {
            lastPruned = today;
            const QFileInfoList entries =
                QDir(dir).entryInfoList({ QStringLiteral("*.jsonl") }, QDir::Files);
            const QDate cutoff = today.addDays(-7);
            for (const QFileInfo &fi : entries) {
                const QDate d = QDate::fromString(fi.completeBaseName(), QStringLiteral("yyyy-MM-dd"));
                if (d.isValid() && d < cutoff) QFile::remove(fi.absoluteFilePath());
            }
        }
    }
    const QString file = dir + QLatin1Char('/') + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".jsonl");
    QFile f(file);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.write("\n");
    }
}

// 噪音采样落盘（采不到不记，避免污染趋势）。
void logNoiseSample(double db, bool active, int seq, bool study, bool ok)
{
    if (!ok) return;
    QJsonObject o;
    o.insert(QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch());
    o.insert(QStringLiteral("db"), db);
    o.insert(QStringLiteral("active"), active);
    o.insert(QStringLiteral("seq"), seq);
    o.insert(QStringLiteral("study"), study);
    appendJsonl(QStringLiteral("noise_log"), o);
}

// 自习会话：开始/结束各记一行（带 startTs 便于算时长）。返回 startTs。
qint64 logStudyStart(double th, int need, int recSec)
{
    const qint64 ts = QDateTime::currentSecsSinceEpoch();
    QJsonObject o;
    o.insert(QStringLiteral("event"), QStringLiteral("start"));
    o.insert(QStringLiteral("ts"), ts);
    o.insert(QStringLiteral("thresholdDb"), th);
    o.insert(QStringLiteral("streakNeed"), need);
    o.insert(QStringLiteral("recordSec"), recSec);
    appendJsonl(QStringLiteral("study_log"), o);
    return ts;
}
void logStudyStop(qint64 startTs, int alertCount)
{
    QJsonObject o;
    o.insert(QStringLiteral("event"), QStringLiteral("stop"));
    o.insert(QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch());
    o.insert(QStringLiteral("startTs"), startTs);
    o.insert(QStringLiteral("durationSec"),
             startTs > 0 ? (QDateTime::currentSecsSinceEpoch() - startTs) : 0);
    o.insert(QStringLiteral("alertCount"), alertCount);
    appendJsonl(QStringLiteral("study_log"), o);
}