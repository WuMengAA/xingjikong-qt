#include "loop_reminder.h"

LoopReminder::LoopReminder(QObject* parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &LoopReminder::fire);
    m_total.invalidate();
}

void LoopReminder::start(const QString& id, const QString& title, const QString& body, const Config& cfg)
{
    m_id = id;
    m_title = title;
    m_body = body;
    m_cfg = cfg;
    m_rounds = 0;
    m_active = true;
    m_total.restart();
    fire();                     // 首次立即展示
}

void LoopReminder::fire()
{
    if (!m_active) return;
    // 上限检查：次数 或 总时长（8h 硬顶）
    if (m_cfg.maxRounds > 0 && m_rounds >= m_cfg.maxRounds) {
        m_active = false;
        emit expired(m_id, QStringLiteral("达到次数上限"));
        return;
    }
    if (m_total.isValid() && m_cfg.maxMs > 0 && m_total.elapsed() >= m_cfg.maxMs) {
        m_active = false;
        emit expired(m_id, QStringLiteral("超过总时长上限"));
        return;
    }
    ++m_rounds;
    emit requestShow(m_id, m_title, m_body, m_rounds);
    scheduleNext();
}

void LoopReminder::scheduleNext()
{
    if (!m_active) return;
    m_timer.start(qMax(1, m_cfg.intervalSec) * 1000);
}

void LoopReminder::confirm(const QString& id)
{
    if (!m_active || m_id != id) return;
    m_active = false;
    m_timer.stop();
    // 确认是正常退出：不发 expired
}

void LoopReminder::cancel(const QString& id)
{
    if (m_id != id) return;
    m_active = false;
    m_timer.stop();
    emit expired(id, QStringLiteral("已取消"));
}