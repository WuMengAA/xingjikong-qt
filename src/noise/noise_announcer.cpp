#include "noise_announcer.h"

#include <QSettings>

NoiseAnnouncer::NoiseAnnouncer(QObject *parent)
    : QObject(parent)
{
    QSettings s;
    m_voiceEnabled = s.value(QStringLiteral("noiseAnnounceVoice"), true).toBool();
    // 固定文案存在则优先（老师自定义，不分级）；空 = 用分级默认
    m_text = s.value(QStringLiteral("noiseAnnounceText"), QString()).toString().trimmed();
}

void NoiseAnnouncer::setVoiceEnabled(bool on) { m_voiceEnabled = on; }
void NoiseAnnouncer::setAnnounceText(const QString &text) { m_text = text.trimmed(); }
void NoiseAnnouncer::setMuted(bool on) { m_muted = on; }

// dBFS 语义：正常说话约 -30~-20，很吵约 -10 以上，安静约 -60 以下。
// 分档只用于"选哪句提醒"，不参与任何判定（判定仍是 NoiseMonitor 的事）。
QString NoiseAnnouncer::pickText(int hits, double db)
{
    Q_UNUSED(hits);
    if (db > -25.0)
        return QStringLiteral("教室现在太吵了，请同学们立即安静下来！");
    if (db > -40.0)
        return QStringLiteral("教室噪音偏高，请同学们小声一点");
    return QStringLiteral("请注意保持安静，教室噪音超标");
}

void NoiseAnnouncer::onNoiseThreshold(int hits, double db)
{
    // ① 教室本地语音（默认开；可关 / 可静音豁免；事件信号面不受这些影响）
    if (m_voiceEnabled && !m_muted) {
        const QString text = !m_text.isEmpty() ? m_text : pickText(hits, db);
        if (!text.isEmpty())
            emit voiceRequested(text);
    }
    // ② 云端事件提醒（管理端可见；不随语音开关/静音豁免关闭 —— 审计与告警不能断）
    emit eventRequested(QStringLiteral("warn"), QStringLiteral("noise_threshold_reached"),
                        QJsonObject{ { QStringLiteral("hits"), hits },
                                     { QStringLiteral("db"), qRound(db * 100) / 100.0 } });
}