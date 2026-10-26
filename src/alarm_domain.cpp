#include "alarm_domain.h"

#include <QApplication>       // QApplication::beep()
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <windows.h>          // LockWorkStation

namespace AlarmDomain {

// TTS 回调（main.cpp 注入）。未注入时静默跳过 —— 闹钟照响、照锁屏，
// 不会因为没接 TTS 就把整条链路断掉。
static std::function<void(const QString &)> g_speakFn;

/* ══════════ 闹钟（2026-10-26 · 产品笔记 P1）══════════════════════════ */
// 定时任务里的「闹钟」：到点响铃 + 可选 TTS 播报 + 可选锁屏；支持重复（星期多选/每天）。
// 与 schedule_shutdown 互不纠缠：独立 alarms.json、独立定时器，不占「单条替换」的关机队列。
QString g_alarmFile;       // %LOCALAPPDATA%\xingjikong\alarms.json（main 里初始化）
QVector<QTimer *> g_alarmTimers;
QHash<QString, QTimer *> g_alarmTimerById;


void saveAlarms(const QVector<AlarmTask> &tasks)
{
    if (g_alarmFile.isEmpty()) {
        qWarning("[agent-qt] FAIL: g_alarmFile 未初始化，闹钟持久化被跳过");
        return;
    }
    QJsonArray arr;
    for (const auto &t : tasks) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), t.id);
        o.insert(QStringLiteral("time"), t.time);
        o.insert(QStringLiteral("text"), t.text);
        o.insert(QStringLiteral("lock"), t.lock);
        o.insert(QStringLiteral("createdMs"), QJsonValue(t.createdMs));
        QJsonArray d;
        for (int day : t.days) d.append(day);
        o.insert(QStringLiteral("days"), d);
        arr.append(o);
    }
    QFile f(g_alarmFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning().noquote() << QStringLiteral("[agent-qt] FAIL: 闹钟持久化失败（写 alarms.json 打不开）");
        return;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVector<AlarmTask> loadAlarms()
{
    QVector<AlarmTask> out;
    if (g_alarmFile.isEmpty()) return out;
    QFile f(g_alarmFile);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return out;
    for (const auto &v : doc.array()) {
        const QJsonObject o = v.toObject();
        AlarmTask t;
        t.id = o.value(QStringLiteral("id")).toString();
        t.time = o.value(QStringLiteral("time")).toString();
        t.text = o.value(QStringLiteral("text")).toString();
        t.lock = o.value(QStringLiteral("lock")).toBool(false);
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

// "HH:MM" → 指定日期该时刻的 ms；非法返回 -1
qint64 alarmTimeToMs(const QString &hhmm, const QDate &date)
{
    const QStringList p = hhmm.split(QLatin1Char(':'));
    if (p.size() != 2) return -1;
    bool okH = false, okM = false;
    const int h = p.at(0).toInt(&okH);
    const int m = p.at(1).toInt(&okM);
    if (!okH || !okM || h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return QDateTime(date, QTime(h, m)).toMSecsSinceEpoch();
}

// 下一次触发时刻（含等于 from 的未来）：days 空 = 最近一个未来时点（当天已过则明天同刻）；
// days 非空 = 从明天起找第一个命中星期（与手机闹钟一致：今天已过不算今天）。
qint64 nextAlarmMs(const AlarmTask &t, qint64 fromMs)
{
    const QDateTime fromDt = QDateTime::fromMSecsSinceEpoch(fromMs);
    const int start = t.days.isEmpty() ? 0 : 1;   // 重复闹钟不看"今天"（已过不追、没过也算今天？——统一从明天算，避免跨周错位）
    for (int i = start; i < 8; ++i) {
        const QDate d = fromDt.date().addDays(i);
        qint64 cand = alarmTimeToMs(t.time, d);
        if (cand <= fromMs) continue;
        if (t.days.isEmpty()) return cand;
        if (t.days.contains(d.dayOfWeek())) return cand;   // Qt dayOfWeek: 1=Mon…7=Sun，与下发约定一致
    }
    return -1;
}

void fireAlarmTask(const QString &id, const QString &time, const QString &text,
                          bool lock, const QVector<int> &days, qint64 createdMs)
{
    qInfo("[agent-qt] ⏰ 闹钟到期 id=%s time=%s", qPrintable(id), qPrintable(time));
    AlarmTask t;
    t.id = id; t.time = time; t.text = text; t.lock = lock; t.days = days; t.createdMs = createdMs;

    // 响铃：系统蜂鸣 ×3（轻量、不依赖音频资产）
    for (int i = 0; i < 3; ++i) QApplication::beep();
    if (!text.trimmed().isEmpty())
        g_speakFn(text.trimmed());

    // 可选锁屏（user32 真实导出名 LockWorkStation，与 action=lock 同款调法）
    if (lock) {
        const BOOL ok = LockWorkStation();
        if (!ok)
            qWarning("[agent-qt] 闹钟锁屏失败：LockWorkStation err=%lu", (unsigned long)GetLastError());
    }

    QVector<AlarmTask> cur = loadAlarms();
    int hit = -1;
    for (int i = 0; i < cur.size(); ++i) if (cur.at(i).id == id) { hit = i; break; }
    if (hit < 0) return;   // 已被取消

    const auto ti = g_alarmTimerById.constFind(id);
    if (ti != g_alarmTimerById.constEnd()) {
        // 单次定时器触发后不自动消失，手动退映射（对象由事件循环回收）
        g_alarmTimerById.erase(ti);
    }

    if (t.days.isEmpty()) {
        // 一次性：响完即清
        cur.removeAt(hit);
        saveAlarms(cur);
        qInfo("[agent-qt] 一次性闹钟 %s 已执行并移除", qPrintable(id));
        return;
    }

    // 重复：重算下次并重挂
    const qint64 next = nextAlarmMs(t, QDateTime::currentMSecsSinceEpoch());
    if (next < 0) {           // 兜底（days 全错的极端情况）
        cur.removeAt(hit);
        saveAlarms(cur);
        return;
    }
    saveAlarms(cur);
    QTimer *tm = new QTimer();
    tm->setParent(qApp);
    tm->setSingleShot(true);
    tm->start((int)qMin<qint64>(next - QDateTime::currentMSecsSinceEpoch(), 2147483647LL));
    QObject::connect(tm, &QTimer::timeout, [id, time, text, lock, days, createdMs] {
        fireAlarmTask(id, time, text, lock, days, createdMs);
    });
    g_alarmTimers.append(tm);
    g_alarmTimerById.insert(id, tm);
    qInfo("[agent-qt] 重复闹钟 %s 已排下次 %s", qPrintable(id),
          qPrintable(QDateTime::fromMSecsSinceEpoch(next).toString(Qt::ISODateWithMs)));
}

void armAlarmTimer(const AlarmTask &t)
{
    const qint64 next = nextAlarmMs(t, QDateTime::currentMSecsSinceEpoch());
    if (next < 0) return;
    QTimer *tm = new QTimer();
    tm->setParent(qApp);
    tm->setSingleShot(true);
    tm->start((int)qMin<qint64>(next - QDateTime::currentMSecsSinceEpoch(), 2147483647LL));
    const QString id = t.id;
    const QString time = t.time;
    const QString text = t.text;
    const bool lock = t.lock;
    const QVector<int> days = t.days;
    const qint64 createdMs = t.createdMs;
    QObject::connect(tm, &QTimer::timeout, [id, time, text, lock, days, createdMs] {
        fireAlarmTask(id, time, text, lock, days, createdMs);
    });
    g_alarmTimers.append(tm);
    g_alarmTimerById.insert(id, tm);
}

// 启动恢复：把未到期闹钟重新挂上（进程崩溃/重启后不丢，与 schedules.json 同款策略）
void rescheduleAlarms()
{
    const QVector<AlarmTask> tasks = loadAlarms();
    if (tasks.isEmpty()) return;
    for (const auto &t : tasks) armAlarmTimer(t);
    qInfo("[agent-qt] 闹钟恢复：载入 %d 条并重挂", (int)tasks.size());
}


bool cancelAlarm(const QString &id)
{
    QVector<AlarmTask> cur = loadAlarms();
    int hit = -1;
    for (int i = 0; i < cur.size(); ++i)
        if (cur.at(i).id == id) { hit = i; break; }
    if (hit < 0) return false;                 // 找不到如实返回，不算"静默成功"
    const auto ti = g_alarmTimerById.constFind(id);
    if (ti != g_alarmTimerById.constEnd() && ti.value()) {
        ti.value()->stop();
        delete ti.value();
        g_alarmTimerById.erase(ti);
    }
    cur.removeAt(hit);
    saveAlarms(cur);
    return true;
}

void init(const QString &file, std::function<void(const QString &)> speakFn)
{
    g_alarmFile = file;
    g_speakFn = speakFn;
}

} // namespace AlarmDomain
