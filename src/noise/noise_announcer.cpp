#include "noise_announcer.h"

#include <QSettings>

NoiseAnnouncer::NoiseAnnouncer(QObject *parent)
    : QObject(parent)
{
    // 适配：QSettings 可关语音 / 换文案（未配置走默认；配置值在运行中可改，重启生效）
    QSettings s;
    m_voiceEnabled = s.value(QStringLiteral("noiseAnnounceVoice"), true).toBool();
    m_text = s.value(QStringLiteral("noiseAnnounceText"), m_text).toString();
}

void NoiseAnnouncer::setVoiceEnabled(bool on) { m_voiceEnabled = on; }
void NoiseAnnouncer::setAnnounceText(const QString &text) { m_text = text; }

void NoiseAnnouncer::onNoiseThreshold(int hits, double db)
{
    // ① 教室本地语音（可选，默认开；QSettings noiseAnnounceVoice=false 可静默化）
    if (m_voiceEnabled)
        emit voiceRequested(m_text);
    // ② 云端事件提醒（管理端可见；不随语音开关关闭，这是审计与告警的信号面）
    emit eventRequested(QStringLiteral("warn"), QStringLiteral("noise_threshold_reached"),
                        QJsonObject{ { QStringLiteral("hits"), hits },
                                     { QStringLiteral("db"), qRound(db * 100) / 100.0 } });
}