// P3 记录页结构化视图实现（2026-10-31）。
// 口径与 buildNoiseLogText / buildStudyLogText **保持一致**（同一批 jsonl、同样的
// 「按天均值 / 按会话时长」算法），只是把结果从"一段多行文本"改成"若干行两列"，
// 这样界面才能按行排版，而不是把一堆等宽字符糊在卡片里。
#include "p3_record_view.h"

#include "p3_reader.h"   // readJsonlDirWithDiag / build*LogText
#include "p3_log.h"      // dataRoot()

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace {

// 上屏行数上限：记录会无限增长，界面是"看一眼最近情况"，不是审计台账。
// 超出的部分在 summary 里如实写明"只显示最近 N 条"，不做静默截断。
constexpr int kMaxRows = 30;

QVariantMap makeRow(const QString &label, const QString &value)
{
    QVariantMap row;
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("value"), value);
    return row;
}

}   // namespace

QVariantMap buildNoiseRecordView()
{
    QVariantMap out;
    QVariantList rows;

    const P3ReadResult rr = readJsonlDirWithDiag(dataRoot(), QStringLiteral("noise_log"));
    const QJsonArray &all = rr.records;
    // 读取失败：把原因/影响一并带出去（QML 走错误态，不再伪装成「还没有记录」）
    if (!rr.isOk()) {
        out.insert(QStringLiteral("summary"), QString());
        out.insert(QStringLiteral("rows"), rows);
        out.insert(QStringLiteral("failReason"), rr.reason);
        out.insert(QStringLiteral("failImpact"), rr.impact);
        return out;
    }
    if (all.isEmpty()) {
        out.insert(QStringLiteral("summary"), QStringLiteral("还没有噪音记录"));
        out.insert(QStringLiteral("rows"), rows);
        return out;
    }

    // 按天分组（与 buildNoiseLogText 同口径：ts → yyyy-MM-dd，db → 采样值）
    QMap<QString, QVector<double>> byDay;
    for (const QJsonValue &v : all) {
        const QJsonObject o = v.toObject();
        const qint64 ts = o.value(QStringLiteral("ts")).toVariant().toLongLong();
        byDay[QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd"))]
            .append(o.value(QStringLiteral("db")).toDouble());
    }

    // QMap 的 key 天然升序 ⇒ 倒着遍历即"新的一天在上"
    const QStringList days = byDay.keys();
    for (int i = days.size() - 1; i >= 0 && rows.size() < kMaxRows; --i) {
        const QVector<double> &v = byDay.value(days.at(i));
        if (v.isEmpty()) continue;
        double sum = 0, mn = 1e9, mx = -1e9;
        for (double d : v) { sum += d; if (d < mn) mn = d; if (d > mx) mx = d; }
        rows.append(makeRow(
            days.at(i),
            QStringLiteral("%1 次 · 均值 %2 dB · 峰值 %3 dB")
                .arg(v.size())
                .arg(QString::number(sum / v.size(), 'f', 1))
                .arg(QString::number(mx, 'f', 1))));
    }

    QString summary = QStringLiteral("共 %1 天 · %2 条采样")
                          .arg(byDay.size())
                          .arg(all.size());
    if (byDay.size() > kMaxRows)
        summary += QStringLiteral("（只显示最近 %1 天）").arg(kMaxRows);

    out.insert(QStringLiteral("summary"), summary);
    out.insert(QStringLiteral("rows"), rows);
    return out;
}

QVariantMap buildStudyRecordView()
{
    QVariantMap out;
    QVariantList rows;

    const P3ReadResult rr = readJsonlDirWithDiag(dataRoot(), QStringLiteral("study_log"));
    const QJsonArray &all = rr.records;
    if (!rr.isOk()) {
        out.insert(QStringLiteral("summary"), QString());
        out.insert(QStringLiteral("rows"), rows);
        out.insert(QStringLiteral("failReason"), rr.reason);
        out.insert(QStringLiteral("failImpact"), rr.impact);
        return out;
    }
    if (all.isEmpty()) {
        out.insert(QStringLiteral("summary"), QStringLiteral("还没有自习记录"));
        out.insert(QStringLiteral("rows"), rows);
        return out;
    }

    // 会话配对（与 buildStudyLogText 同口径）：start 按 ts 挂起，stop 用 startTs 取回
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

    for (int i = sessions.size() - 1; i >= 0 && rows.size() < kMaxRows; --i) {
        const QJsonObject &s = sessions.at(i);
        const qint64 st = s.value(QStringLiteral("ts")).toVariant().toLongLong();
        const int dur = s.value(QStringLiteral("durationSec")).toInt();
        rows.append(makeRow(
            QDateTime::fromSecsSinceEpoch(st).toString(QStringLiteral("MM-dd HH:mm")),
            QStringLiteral("时长 %1 分 %2 秒 · 异常录制 %3 次")
                .arg(dur / 60)
                .arg(dur % 60)
                .arg(s.value(QStringLiteral("alertCount")).toInt())));
    }

    QString summary = QStringLiteral("共 %1 次自习会话").arg(sessions.size());
    if (sessions.size() > kMaxRows)
        summary += QStringLiteral("（只显示最近 %1 次）").arg(kMaxRows);

    out.insert(QStringLiteral("summary"), summary);
    out.insert(QStringLiteral("rows"), rows);
    return out;
}
// ── 「view 字典 → AgentRecordUi」拆解（2026-10-31 二改，动机见头文件）──────────
// 这里 include agent_ui.h 是**唯一**的跨界点：.h 里只用前向声明，避免 776 行声明
// 被拖进每个 include p3_record_view.h 的 TU。
#include "../agent_ui.h"

// 不用 bool 参数区分噪音/自习：applyView(x, y, z, true) 的调用点读起来要回翻签名，
// 而这里恰是最容易写反的地方 —— 写反了不报错，只是噪音页显示成自习记录。
void applyNoiseRecordView(AgentUi *ui)
{
    if (!ui) return;
    const QVariantMap view = buildNoiseRecordView();
    ui->applyNoiseLogView(buildNoiseLogText(), view,
                          view.value(QStringLiteral("failReason")).toString(),
                          view.value(QStringLiteral("failImpact")).toString());
}

void applyStudyRecordView(AgentUi *ui)
{
    if (!ui) return;
    const QVariantMap view = buildStudyRecordView();
    ui->applyStudyLogView(buildStudyLogText(), view,
                          view.value(QStringLiteral("failReason")).toString(),
                          view.value(QStringLiteral("failImpact")).toString());
}
