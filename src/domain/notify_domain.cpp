#include "notify_domain.h"

#include <QCoreApplication>   // QTimer 的 parent（不能用 qApp：那是 QApplication 宏，需 <QApplication>）
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QtGlobal>

namespace NotifyDomain {

// 持久化路径（main.cpp 注入）
static QString g_file;
// 触发回调（main.cpp 注入 → 转 islandShow）。未注入时只记日志，不崩、不静默假成功。
static std::function<void(const NotifyTask &)> g_fireFn;

static QHash<QString, QTimer *> g_timerById;   // id → 定时器
static QHash<QString, int>      g_firedCount;  // id → 已触发次数（进程内，重启重置）

/* ══════════ 持久化 ══════════════════════════════════════════════ */

void saveTasks(const QVector<NotifyTask> &tasks)
{
    if (g_file.isEmpty()) {
        qWarning("[agent-qt] FAIL: NotifyDomain 未初始化，定时通知持久化被跳过");
        return;
    }
    QJsonArray arr;
    for (const auto &t : tasks) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), t.id);
        o.insert(QStringLiteral("time"), t.time);
        o.insert(QStringLiteral("times"), t.times);
        o.insert(QStringLiteral("intervalMin"), t.intervalMin);
        o.insert(QStringLiteral("title"), t.title);
        o.insert(QStringLiteral("content"), t.content);
        o.insert(QStringLiteral("channel"), t.channel);
        o.insert(QStringLiteral("createdMs"), QJsonValue(t.createdMs));
        QJsonArray d;
        for (int day : t.days) d.append(day);
        o.insert(QStringLiteral("days"), d);
        arr.append(o);
    }
    QFile f(g_file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 定时通知持久化失败（notifies.json 打不开）");
        return;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVector<NotifyTask> loadTasks()
{
    QVector<NotifyTask> out;
    if (g_file.isEmpty()) return out;
    QFile f(g_file);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return out;
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        NotifyTask t;
        t.id = o.value(QStringLiteral("id")).toString();
        t.time = o.value(QStringLiteral("time")).toString();
        t.times = o.value(QStringLiteral("times")).toInt(1);
        t.intervalMin = o.value(QStringLiteral("intervalMin")).toInt(0);
        t.title = o.value(QStringLiteral("title")).toString();
        t.content = o.value(QStringLiteral("content")).toString();
        t.channel = o.value(QStringLiteral("channel")).toString();
        t.createdMs = (qint64)o.value(QStringLiteral("createdMs")).toDouble(0);
        const QJsonArray d = o.value(QStringLiteral("days")).toArray();
        for (const auto &dv : d) {
            const int day = dv.toInt(0);
            if (day >= 1 && day <= 7) t.days.append(day);
        }
        if (!t.id.isEmpty() && !t.time.isEmpty()) out.append(t);
    }
    return out;
}

/* ══════════ 时间计算 ═══════════════════════════════════════════ */

// "HH:MM" → 指定日期该时刻的 epoch ms；非法返回 -1
static qint64 timeToMs(const QString &hhmm, const QDate &date)
{
    const QStringList p = hhmm.split(QLatin1Char(':'));
    if (p.size() != 2) return -1;
    bool okH = false, okM = false;
    const int h = p.at(0).toInt(&okH);
    const int m = p.at(1).toInt(&okM);
    if (!okH || !okM || h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return QDateTime(date, QTime(h, m)).toMSecsSinceEpoch();
}

qint64 nextFireMs(const NotifyTask &t, qint64 fromMs, int firedCount)
{
    if (t.time.isEmpty()) return -1;

    // ① 带星期 → 每天一次：从**明天**起找第一个命中星期（与 AlarmDomain 同口径，
    //    避免"今天已过却仍算今天"的跨周错位）。
    if (!t.days.isEmpty()) {
        const QDate today = QDateTime::fromMSecsSinceEpoch(fromMs).date();
        for (int i = 1; i < 8; ++i) {
            const QDate d = today.addDays(i);
            if (!t.days.contains(d.dayOfWeek())) continue;   // Qt: 1=Mon…7=Sun
            return timeToMs(t.time, d);
        }
        return -1;   // days 全非法
    }

    // ② 一次性（可当天重复 times 次）：base = 最近的未来时点，
    //    第 N 次 = base + N × intervalMin 分钟。
    qint64 base = -1;
    const QDateTime from = QDateTime::fromMSecsSinceEpoch(fromMs);
    for (int i = 0; i < 2; ++i) {
        const qint64 cand = timeToMs(t.time, from.date().addDays(i));
        if (cand > fromMs) { base = cand; break; }
    }
    if (base < 0) return -1;

    const int n = qMax(0, firedCount);
    const int interval = qMax(0, t.intervalMin);
    return base + (qint64)n * (qint64)interval * 60000LL;
}

/* ══════════ 定时器 ═════════════════════════════════════════════ */

static void fireTask(const QString &id);

static void armTimerInternal(const NotifyTask &t, int fired, qint64 fromMs)
{
    const qint64 next = nextFireMs(t, fromMs, fired);
    if (next < 0) {
        qInfo("[agent-qt] 定时通知 %s 无下次触发点（days/时间无效），不再挂", qPrintable(t.id));
        return;
    }
    QTimer *tm = new QTimer(QCoreApplication::instance());
    tm->setSingleShot(true);
    const qint64 delay = next - QDateTime::currentMSecsSinceEpoch();
    tm->start((int)qMin<qint64>(qMax<qint64>(delay, 0), 2147483647LL));
    const QString id = t.id;
    QObject::connect(tm, &QTimer::timeout, [id] { fireTask(id); });

    // 同一 id 重挂前先收掉旧定时器，避免"排了两次"（幂等）
    const auto old = g_timerById.constFind(id);
    if (old != g_timerById.constEnd() && old.value()) {
        old.value()->stop();
        old.value()->deleteLater();
    }
    g_timerById.insert(id, tm);
}

static void fireTask(const QString &id)
{
    QVector<NotifyTask> cur = loadTasks();
    int hit = -1;
    for (int i = 0; i < cur.size(); ++i) {
        if (cur.at(i).id == id) { hit = i; break; }
    }
    if (hit < 0) return;            // 已被取消
    const NotifyTask t = cur.at(hit);

    qInfo("[agent-qt] 🔔 定时通知到期 id=%s time=%s title=%s channel=%s",
          qPrintable(t.id), qPrintable(t.time), qPrintable(t.title), qPrintable(t.channel));

    if (g_fireFn) {
        g_fireFn(t);               // main.cpp 侧转 islandShow（带渠道）
    } else {
        qWarning("[agent-qt] 定时通知 %s 到期，但未注入触发回调（只记日志，不假成功）",
                 qPrintable(t.id));
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (!t.days.isEmpty()) {
        // 每天一次：次数归零，重挂下一次命中星期
        g_firedCount.insert(id, 0);
        armTimerInternal(t, 0, now);
        return;
    }

    // 一次性：累加次数；用尽即移除
    int fired = g_firedCount.value(id, 0) + 1;
    const int total = qMax(1, t.times);
    if (fired >= total) {
        g_firedCount.remove(id);
        const auto ti = g_timerById.constFind(id);
        if (ti != g_timerById.constEnd()) g_timerById.erase(ti);
        cur.removeAt(hit);
        saveTasks(cur);
        qInfo("[agent-qt] 定时通知 %s 已完成 %d/%d 次并移除", qPrintable(id), fired, total);
        return;
    }
    g_firedCount.insert(id, fired);
    armTimerInternal(t, fired, now);
}

void armTimer(const NotifyTask &t)
{
    armTimerInternal(t, g_firedCount.value(t.id, 0), QDateTime::currentMSecsSinceEpoch());
}

void reschedule()
{
    const QVector<NotifyTask> tasks = loadTasks();
    if (tasks.isEmpty()) return;
    for (const auto &t : tasks) armTimer(t);
    qInfo("[agent-qt] 定时通知恢复：载入 %d 条并重挂（已触发次数不持久化，重启后从 0 计）",
          (int)tasks.size());
}

bool cancel(const QString &id)
{
    QVector<NotifyTask> cur = loadTasks();
    int hit = -1;
    for (int i = 0; i < cur.size(); ++i) {
        if (cur.at(i).id == id) { hit = i; break; }
    }
    if (hit < 0) return false;      // 找不到如实返回

    const auto ti = g_timerById.constFind(id);
    if (ti != g_timerById.constEnd() && ti.value()) {
        ti.value()->stop();
        ti.value()->deleteLater();
        g_timerById.erase(ti);
    }
    g_firedCount.remove(id);
    cur.removeAt(hit);
    saveTasks(cur);
    return true;
}

bool upsert(const NotifyTask &t, QString *err)
{
    if (t.id.isEmpty()) { if (err) *err = QStringLiteral("缺 id"); return false; }
    if (t.time.isEmpty()) { if (err) *err = QStringLiteral("缺 time（HH:MM）"); return false; }
    if (timeToMs(t.time, QDate::currentDate()) < 0) {
        if (err) *err = QStringLiteral("time 不是合法 HH:MM：%1").arg(t.time);
        return false;
    }
    if (t.title.trimmed().isEmpty()) { if (err) *err = QStringLiteral("缺 title"); return false; }

    QVector<NotifyTask> cur = loadTasks();
    int hit = -1;
    for (int i = 0; i < cur.size(); ++i) {
        if (cur.at(i).id == t.id) { hit = i; break; }
    }
    if (hit >= 0) cur[hit] = t; else cur.append(t);
    saveTasks(cur);

    g_firedCount.insert(t.id, 0);              // 重新排 = 次数重算
    armTimerInternal(t, 0, QDateTime::currentMSecsSinceEpoch());
    return true;
}

void init(const QString &file, std::function<void(const NotifyTask &)> fireFn)
{
    g_file = file;
    g_fireFn = fireFn;
}

} // namespace NotifyDomain
