// 星集控被控端 · Local API 鉴权实现（2026-10-31）
#include "localapi_auth.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>

// 常量时间比较（防时序侧信道）：长度不同立即假；否则逐字节异或累加。
// 绝不能用 == 或 QString::compare（可能短路提前返回，泄露 token 前缀信息）。
static bool constantTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

LocalApiAuth::LocalApiAuth(const QString &configDir)
    : m_configDir(configDir),
      m_tokenPath(configDir + QStringLiteral("/localapi.token")),
      m_auditPath(configDir + QStringLiteral("/localapi-audit.log"))
{
}

QString LocalApiAuth::loadOrCreateToken()
{
    QDir().mkpath(m_configDir);

    // ① 已存在且形态正确（64 hex）→ 直接用，不覆盖（覆盖会踢掉已发出去的 token）。
    QFile f(m_tokenPath);
    if (f.open(QIODevice::ReadOnly)) {
        m_token = QString::fromLatin1(f.readAll()).trimmed();
        f.close();
        if (m_token.size() == 64 && !m_token.isEmpty())
            return m_token;
    }

    // ② 生成 32 字节随机 → 64 hex。
    QByteArray raw(32, 0);
    for (int i = 0; i < raw.size(); ++i)
        raw[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    m_token = QString::fromLatin1(raw.toHex());

    // ③ 落盘。失败：进程内保留 token 可用，但重启后丢失（记不了审计也读不回，
    //    下一轮 loadOrCreateToken 会再生成一份）。仍返回 m_token，不 fail。
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(m_token.toLatin1());
        f.close();
    }
    return m_token;
}

bool LocalApiAuth::validate(const QString &provided) const
{
    if (m_token.isEmpty()) return false;
    return constantTimeEquals(provided.toLatin1(), m_token.toLatin1());
}

bool LocalApiAuth::allow(const QString &clientIp)
{
    constexpr qint64 kWindowMs = 60000;   // 60s 窗口
    constexpr int kMaxPerWindow = 120;    // 120 req / 窗口
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Window &w = m_windows[clientIp];
    if (now - w.startMs >= kWindowMs) {
        w.startMs = now;
        w.count = 0;
    }
    if (w.count >= kMaxPerWindow) return false;
    ++w.count;
    return true;
}

void LocalApiAuth::audit(const QString &clientIp, const QString &methodPath, const QString &reason)
{
    QFile f(m_auditPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    const QString line = QDateTime::currentDateTime().toString(Qt::ISODate)
        + QStringLiteral(" | ") + methodPath
        + QStringLiteral(" | from ") + clientIp
        + QStringLiteral(" | reason=") + reason
        + QStringLiteral("\n");
    f.write(line.toUtf8());
    f.close();
}
