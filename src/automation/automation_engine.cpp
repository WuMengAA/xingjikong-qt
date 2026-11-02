// 星集控被控端 · 定时自动化规则引擎（2026-10-27 · Phase 3）
// 实现见 automation_engine.h 的设计边界说明。

#include "automation_engine.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTime>
#include <QDate>
#include <QMap>
#include <QUuid>
#include <QDir>

AutomationEngine::AutomationEngine(QObject *parent) : QObject(parent)
{
    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &AutomationEngine::onTick);
    m_tick->start();
    loadFromDisk();
}

AutomationEngine::~AutomationEngine() = default;

// ── 持久化 ───────────────────────────────────────────────────────
QString AutomationEngine::dataFile() const
{
    QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/AppData/Local");
    return base + QStringLiteral("/xingjikong/automation.json");
}

void AutomationEngine::loadFromDisk()
{
    const QString f = dataFile();
    QFile file(f);
    if (!file.exists()) { m_ready = true; emit readyChanged(); return; }
    if (!file.open(QIODevice::ReadOnly)) {
        emit engineLog(QStringLiteral("自动化：无法读取 %1").arg(f));
        m_ready = true; emit readyChanged(); return;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        emit engineLog(QStringLiteral("自动化：JSON 解析失败，已忽略旧文件"));
        m_ready = true; emit readyChanged(); return;
    }
    m_rules.clear();
    for (const QJsonValue &v : doc.array())
        if (v.isObject()) m_rules.append(ruleFromJson(v.toObject()));
    recomputeAllSchedules();
    m_ready = true;
    emit readyChanged();
}

void AutomationEngine::saveToDisk()
{
    const QString f = dataFile();
    QFile file(f);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit engineLog(QStringLiteral("自动化：无法写入 %1").arg(f));
        return;
    }
    QJsonArray arr;
    for (const Rule &r : m_rules) arr.append(ruleToJson(r));
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    file.close();
}

// ── 规则结构编解码 ───────────────────────────────────────────────
AutomationEngine::Rule AutomationEngine::ruleFromJson(const QJsonObject &o) const
{
    Rule r;
    QString id = o.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.id = id;
    r.name = o.value(QStringLiteral("name")).toString();
    r.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    r.trigger = o.value(QStringLiteral("trigger")).toObject();
    r.condition = o.value(QStringLiteral("condition")).toObject();
    r.action = o.value(QStringLiteral("action")).toObject();
    r.nextRunMs = 0;
    r.lastRunMs = 0;
    return r;
}

QJsonObject AutomationEngine::ruleToJson(const Rule &r) const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), r.id);
    o.insert(QStringLiteral("name"), r.name);
    o.insert(QStringLiteral("enabled"), r.enabled);
    o.insert(QStringLiteral("trigger"), r.trigger);
    o.insert(QStringLiteral("condition"), r.condition);
    o.insert(QStringLiteral("action"), r.action);
    return o;
}

QVariantList AutomationEngine::ruleSummaries() const
{
    QVariantList out;
    for (const Rule &r : m_rules) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), r.id);
        m.insert(QStringLiteral("name"), r.name);
        m.insert(QStringLiteral("enabled"), r.enabled);
        m.insert(QStringLiteral("triggerText"), summarizeTrigger(r));
        m.insert(QStringLiteral("actionText"), summarizeAction(r));
        m.insert(QStringLiteral("status"), !r.enabled ? QStringLiteral("已停用")
                       : (r.lastError.isEmpty() ? QStringLiteral("正常") : QStringLiteral("错误")));
        m.insert(QStringLiteral("lastRunText"), r.lastRunMs > 0
                     ? QDateTime::fromMSecsSinceEpoch(r.lastRunMs).toString(QStringLiteral("M/d HH:mm"))
                     : QStringLiteral("从未"));
        m.insert(QStringLiteral("nextRunText"), r.nextRunMs > 0
                     ? QDateTime::fromMSecsSinceEpoch(r.nextRunMs).toString(QStringLiteral("M/d HH:mm"))
                     : QStringLiteral("—"));
        m.insert(QStringLiteral("lastError"), r.lastError);
        out.append(m);
    }
    return out;
}

QString AutomationEngine::summarizeTrigger(const Rule &r) const
{
    const QString type = r.trigger.value(QStringLiteral("type")).toString().toLower();
    if (type == QStringLiteral("time")) {
        const QString t = r.trigger.value(QStringLiteral("time")).toString();
        return QStringLiteral("每天 %1").arg(t.isEmpty() ? QStringLiteral("??:??") : t);
    } else if (type == QStringLiteral("interval")) {
        const int im = r.trigger.value(QStringLiteral("intervalMin")).toInt(0);
        return QStringLiteral("每 %1 分钟").arg(im > 0 ? QString::number(im) : QStringLiteral("?"));
    } else if (type == QStringLiteral("event")) {
        const QString e = r.trigger.value(QStringLiteral("event")).toString();
        static const QMap<QString, QString> map = {
            { QStringLiteral("boot"), QStringLiteral("开机") },
            { QStringLiteral("idle"), QStringLiteral("空闲") },
            { QStringLiteral("classStart"), QStringLiteral("上课") },
            { QStringLiteral("classEnd"), QStringLiteral("下课") },
            { QStringLiteral("screenLock"), QStringLiteral("锁屏") },
            { QStringLiteral("screenUnlock"), QStringLiteral("解锁") },
        };
        return QStringLiteral("事件 · %1").arg(map.value(e, e));
    }
    return QStringLiteral("未知触发");
}

QString AutomationEngine::summarizeAction(const Rule &r) const
{
    const QString kind = r.action.value(QStringLiteral("kind")).toString().toLower();
    if (kind == QStringLiteral("notify"))
        return QStringLiteral("通知：%1").arg(r.action.value(QStringLiteral("title")).toString());
    if (kind == QStringLiteral("set_volume"))
        return QStringLiteral("音量 → %1%").arg(r.action.value(QStringLiteral("value")).toInt(50));
    if (kind == QStringLiteral("mute")) return QStringLiteral("静音");
    if (kind == QStringLiteral("unmute")) return QStringLiteral("取消静音");
    if (kind == QStringLiteral("lock")) return QStringLiteral("锁屏");
    if (kind == QStringLiteral("screenshot")) return QStringLiteral("截图");
    if (kind == QStringLiteral("launch_app"))
        return QStringLiteral("启动 %1").arg(r.action.value(QStringLiteral("target")).toString());
    return QStringLiteral("未知动作");
}

// ── 调度 ─────────────────────────────────────────────────────────
void AutomationEngine::recomputeSchedule(Rule &r)
{
    const QString type = r.trigger.value(QStringLiteral("type")).toString().toLower();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (type == QStringLiteral("time")) {
        const QTime t = QTime::fromString(r.trigger.value(QStringLiteral("time")).toString(),
                                          QStringLiteral("HH:mm"));
        if (!t.isValid()) { r.nextRunMs = 0; return; }
        QDateTime dt(QDate::currentDate(), t);
        if (dt.toMSecsSinceEpoch() <= now) dt = dt.addDays(1);
        r.nextRunMs = dt.toMSecsSinceEpoch();
    } else if (type == QStringLiteral("interval")) {
        const int im = r.trigger.value(QStringLiteral("intervalMin")).toInt(0);
        if (im <= 0) { r.nextRunMs = 0; return; }
        const qint64 base = (r.lastRunMs > 0) ? r.lastRunMs : now;
        const qint64 step = qint64(im) * 60000;
        qint64 nxt = base + step;
        while (nxt <= now) nxt += step;     // 跳过已过去的多个周期
        r.nextRunMs = nxt;
    } else {
        r.nextRunMs = 0;                   // 事件型：不靠时间
    }
}

void AutomationEngine::recomputeAllSchedules()
{
    for (Rule &r : m_rules) recomputeSchedule(r);
}

void AutomationEngine::onTick()
{
    if (!m_ready) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool scheduleChanged = false;
    for (Rule &r : m_rules) {
        if (!r.enabled) continue;
        const QString type = r.trigger.value(QStringLiteral("type")).toString().toLower();
        if (type == QStringLiteral("time")) {
            if (r.nextRunMs == 0) { recomputeSchedule(r); continue; }
            if (now < r.nextRunMs) continue;                 // 还没到
            if (now > r.nextRunMs + 59000) {                 // 错过本窗口 → 跳到下次，不补发
                recomputeSchedule(r); scheduleChanged = true; continue;
            }
            if (evaluateCondition(r)) fireRule(r);           // 在窗口内 → 触发
            recomputeSchedule(r); scheduleChanged = true;    // 推到明天
        } else if (type == QStringLiteral("interval")) {
            if (r.nextRunMs == 0) { recomputeSchedule(r); continue; }
            if (now >= r.nextRunMs) {
                if (evaluateCondition(r)) fireRule(r);
                recomputeSchedule(r); scheduleChanged = true;
            }
        }
        // event 型不在这里处理（走 onEvent）
    }
    if (scheduleChanged) emit rulesChanged();
}

// ── 条件 / 点火 ──────────────────────────────────────────────────
bool AutomationEngine::evaluateCondition(const Rule &r) const
{
    const QJsonObject c = r.condition;
    // 星期（1=周一 .. 7=周日）
    if (c.contains(QStringLiteral("days"))) {
        const QJsonArray days = c.value(QStringLiteral("days")).toArray();
        const int today = QDate::currentDate().dayOfWeek();
        bool matched = false;
        for (const QJsonValue &v : days)
            if (v.toInt() == today) { matched = true; break; }
        if (!matched) return false;
    }
    // 时间窗 [after, before)
    const QString after = c.value(QStringLiteral("after")).toString();
    const QString before = c.value(QStringLiteral("before")).toString();
    if (!after.isEmpty() || !before.isEmpty()) {
        const QTime now = QTime::currentTime();
        const QTime aT = QTime::fromString(after, QStringLiteral("HH:mm"));
        const QTime bT = QTime::fromString(before, QStringLiteral("HH:mm"));
        const bool hasA = aT.isValid();
        const bool hasB = bT.isValid();
        if (hasA || hasB) {
            if (hasA && hasB) {
                if (aT <= bT) { if (!(now >= aT && now < bT)) return false; }
                else { if (!(now >= aT || now < bT)) return false; }   // 跨午夜
            } else if (hasA) { if (!(now >= aT)) return false; }
            else { if (!(now < bT)) return false; }
        }
    }
    return true;
}

QJsonObject AutomationEngine::buildAction(const Rule &r) const
{
    const QJsonObject a = r.action;
    const QString kind = a.value(QStringLiteral("kind")).toString().toLower();
    QJsonObject out;
    if (kind == QStringLiteral("notify")) {
        const QString t = a.value(QStringLiteral("title")).toString().trimmed();
        if (t.isEmpty()) return QJsonObject();          // 缺标题 = 非法
        out.insert(QStringLiteral("action"), QStringLiteral("notify"));
        out.insert(QStringLiteral("title"), t);
        out.insert(QStringLiteral("content"), a.value(QStringLiteral("content")).toString());
        out.insert(QStringLiteral("seconds"), a.value(QStringLiteral("seconds")).toInt(5));
        out.insert(QStringLiteral("from"), QStringLiteral("automation"));
        // 2026-11-01（第 2 步 · 提供方接线）：显式声明来源，设置页关掉「自动化」后不再弹。
        out.insert(QStringLiteral("provider"), QStringLiteral("automation"));
    } else if (kind == QStringLiteral("set_volume")) {
        out.insert(QStringLiteral("action"), QStringLiteral("set_volume"));
        out.insert(QStringLiteral("value"), qBound(0, a.value(QStringLiteral("value")).toInt(50), 100));
    } else if (kind == QStringLiteral("mute")) {
        out.insert(QStringLiteral("action"), QStringLiteral("set_volume"));
        out.insert(QStringLiteral("value"), 0);
    } else if (kind == QStringLiteral("unmute")) {
        out.insert(QStringLiteral("action"), QStringLiteral("set_volume"));
        out.insert(QStringLiteral("value"), qBound(0, a.value(QStringLiteral("value")).toInt(75), 100));
    } else if (kind == QStringLiteral("lock")) {
        out.insert(QStringLiteral("action"), QStringLiteral("lock"));
    } else if (kind == QStringLiteral("screenshot")) {
        out.insert(QStringLiteral("action"), QStringLiteral("screenshot"));
    } else if (kind == QStringLiteral("launch_app")) {
        const QString tgt = a.value(QStringLiteral("target")).toString().trimmed();
        if (tgt.isEmpty()) return QJsonObject();        // 缺 target = 非法
        out.insert(QStringLiteral("action"), QStringLiteral("launch_app"));
        out.insert(QStringLiteral("target"), tgt);
    } else {
        return QJsonObject();                           // 未知 kind = 非法
    }
    return out;
}

bool AutomationEngine::fireRule(Rule &r)
{
    const QJsonObject act = buildAction(r);
    if (act.isEmpty()) {
        r.lastError = QStringLiteral("动作无效（缺 title / target 或未知 kind）");
        emit rulesChanged();
        return false;
    }
    r.lastRunMs = QDateTime::currentMSecsSinceEpoch();
    r.lastError.clear();
    emit actionRequested(act);
    emit engineLog(QStringLiteral("[自动化] 触发「%1」→ %2").arg(r.name, summarizeAction(r)));
    return true;
}

// ── 对外 API ─────────────────────────────────────────────────────
bool AutomationEngine::addRule(const QJsonObject &rule)
{
    Rule r = ruleFromJson(rule);
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].id == r.id) {        // 同 id = 覆盖
            m_rules[i] = r;
            recomputeSchedule(m_rules[i]);
            saveToDisk(); emit rulesChanged(); return true;
        }
    }
    m_rules.append(r);
    recomputeSchedule(m_rules.last());
    saveToDisk(); emit rulesChanged(); return true;
}

bool AutomationEngine::updateRule(const QJsonObject &rule)
{
    const QString id = rule.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) return false;
    Rule r = ruleFromJson(rule);
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].id == id) {
            m_rules[i] = r;
            recomputeSchedule(m_rules[i]);
            saveToDisk(); emit rulesChanged(); return true;
        }
    }
    return false;
}

bool AutomationEngine::removeRule(const QString &id)
{
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].id == id) {
            m_rules.removeAt(i);
            saveToDisk(); emit rulesChanged(); return true;
        }
    }
    return false;
}

bool AutomationEngine::toggleRule(const QString &id, bool enabled)
{
    for (Rule &r : m_rules) {
        if (r.id == id) {
            r.enabled = enabled;
            saveToDisk(); emit rulesChanged(); return true;
        }
    }
    return false;
}

QJsonArray AutomationEngine::listRules() const
{
    QJsonArray arr;
    for (const Rule &r : m_rules) arr.append(ruleToJson(r));
    return arr;
}

bool AutomationEngine::upsertFromRemote(const QJsonObject &rule)
{
    // 管理端下发必须带 id（便于幂等覆盖）；其余交给 addRule 的通用去重逻辑。
    if (rule.value(QStringLiteral("id")).toString().isEmpty()) return false;
    return addRule(rule);
}

void AutomationEngine::runNow(const QString &id)
{
    for (Rule &r : m_rules) {
        if (r.id == id) {
            if (evaluateCondition(r)) fireRule(r);
            else { r.lastError = QStringLiteral("条件不满足（时间窗 / 星期）"); emit rulesChanged(); }
            return;
        }
    }
}

void AutomationEngine::onEvent(const QString &eventName)
{
    for (Rule &r : m_rules) {
        if (!r.enabled) continue;
        if (r.trigger.value(QStringLiteral("type")).toString().toLower() != QStringLiteral("event")) continue;
        if (r.trigger.value(QStringLiteral("event")).toString() != eventName) continue;
        if (evaluateCondition(r)) fireRule(r);
    }
}
