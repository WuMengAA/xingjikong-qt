// 星集控 · 内置模块层 · 宿主实现（2026-10-16）
// 说明与 AUTOMOC 约束见 modulehost.h 顶部。

#include "modulehost.h"

#include <QCoreApplication>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>

// ── AgentModule 默认实现 ───────────────────────────────────────────────────
bool AgentModule::execAction(const QString &action, const QJsonObject &params, ModuleResult &out)
{
    Q_UNUSED(action); Q_UNUSED(params); Q_UNUSED(out);
    return false;      // 默认不认领任何指令
}

void AgentModule::onEvent(const QString &name, const QJsonObject &payload)
{
    Q_UNUSED(name); Q_UNUSED(payload);
}

const ModuleAgentApi &AgentModule::api() const
{
    // 未挂载宿主时给一份空 api（离屏探针/单测场景）：调用即 no-op，不崩。
    static const ModuleAgentApi kEmpty;
    return m_host ? m_host->api() : kEmpty;
}

// ── ModuleHost ─────────────────────────────────────────────────────────────
ModuleHost::ModuleHost(QObject *parent) : QObject(parent) {}

ModuleHost *ModuleHost::instance()
{
    static ModuleHost *s_inst = nullptr;
    if (!s_inst) s_inst = new ModuleHost(qApp);
    return s_inst;
}

QString ModuleHost::settingsKey(const QString &id) const
{
    return QStringLiteral("modules/") + id + QStringLiteral("/enabled");
}

void ModuleHost::registerModule(AgentModule *m)
{
    if (!m) return;
    const ModuleManifest mf = m->manifest();
    if (mf.id.isEmpty()) {
        qWarning("[module-host] 登记失败：模块 manifest 缺 id");
        delete m;
        return;
    }
    if (m_byId.contains(mf.id)) {
        qWarning("[module-host] 模块 id 重复，忽略：%s", qPrintable(mf.id));
        delete m;
        return;
    }
    m->m_host = this;
    m->setParent(this);
    m_byId.insert(mf.id, m);
    m_order.append(m);
    m_running.insert(mf.id, false);
}

void ModuleHost::startEnabled()
{
    for (AgentModule *m : m_order) {
        const QString id = m->manifest().id;
        const bool on = isEnabled(id);
        applyEnabled(m, on);
    }
    emit changed();
}

void ModuleHost::stopAll()
{
    for (AgentModule *m : m_order) {
        const QString id = m->manifest().id;
        if (m_running.value(id, false)) {
            m->stop();
            m_running.insert(id, false);
        }
    }
    emit changed();
}

void ModuleHost::applyEnabled(AgentModule *m, bool on)
{
    const QString id = m->manifest().id;
    const bool running = m_running.value(id, false);
    if (on == running) return;
    if (on) {
        m->start();
        m_running.insert(id, true);
        qInfo("[module-host] 模块已启用：%s", qPrintable(id));
    } else {
        m->stop();
        m_running.insert(id, false);
        qInfo("[module-host] 模块已停用：%s", qPrintable(id));
    }
}

bool ModuleHost::dispatch(const QString &action, const QJsonObject &params, ModuleResult &out)
{
    for (AgentModule *m : m_order) {
        const ModuleManifest mf = m->manifest();
        if (!mf.actions.contains(action)) continue;
        if (!m_running.value(mf.id, false)) {
            // 认领了但没启用：给一条明确的"去启用"回执，而不是默默落到内置指令
            // （否则"装了模块却没开"会表现成莫名其妙的 failed）。
            out = ModuleResult{ false, QStringLiteral("failed"),
                                QStringLiteral("内置模块 %1 未启用（插件市场里打开它）").arg(mf.id), {} };
            return true;
        }
        out = ModuleResult{};
        const bool handled = m->execAction(action, params, out);
        if (handled) return true;
    }
    return false;
}

void ModuleHost::forwardEvent(const QString &name, const QJsonObject &payload)
{
    for (AgentModule *m : m_order) {
        const ModuleManifest mf = m->manifest();
        if (!mf.events.contains(name)) continue;
        if (!m_running.value(mf.id, false)) continue;
        m->onEvent(name, payload);
    }
}

void ModuleHost::setEnabled(const QString &id, bool on)
{
    AgentModule *m = m_byId.value(id, nullptr);
    if (!m) return;
    QSettings().setValue(settingsKey(id), on);
    applyEnabled(m, on);
    emit changed();
}

bool ModuleHost::isEnabled(const QString &id) const
{
    AgentModule *m = m_byId.value(id, nullptr);
    const bool dflt = m ? m->manifest().enabledByDefault : false;
    return QSettings().value(settingsKey(id), dflt).toBool();
}

bool ModuleHost::isRunning(const QString &id) const
{
    return m_running.value(id, false);
}

AgentModule *ModuleHost::module(const QString &id) const
{
    return m_byId.value(id, nullptr);
}

QStringList ModuleHost::moduleIds() const
{
    QStringList out;
    for (AgentModule *m : m_order) out << m->manifest().id;
    return out;
}

QJsonArray ModuleHost::snapshot() const
{
    QJsonArray arr;
    for (AgentModule *m : m_order) {
        const ModuleManifest mf = m->manifest();
        QJsonObject o;
        o.insert(QStringLiteral("id"), mf.id);
        o.insert(QStringLiteral("name"), mf.name);
        o.insert(QStringLiteral("version"), mf.version);
        o.insert(QStringLiteral("description"), mf.description);
        o.insert(QStringLiteral("builtin"), true);
        o.insert(QStringLiteral("enabled"), isEnabled(mf.id));
        o.insert(QStringLiteral("running"), m_running.value(mf.id, false));
        QJsonArray perms; for (const QString &p : mf.permissions) perms.append(p);
        o.insert(QStringLiteral("permissions"), perms);
        QJsonArray acts;  for (const QString &a : mf.actions) acts.append(a);
        o.insert(QStringLiteral("actions"), acts);
        arr.append(o);
    }
    return arr;
}
