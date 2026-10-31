#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

// 噪音超阈提醒器（2026-10-31 · 补上 P0-1 缺的一环：判定信号没人接）
//
// P0-1 判定链路：连续 N 次超阈 → NoiseMonitor::thresholdReached(hits, db)。
// 但这根信号**从未被 connect**（grep 实证：全仓只有声明与 emit，零消费方）——
// 超阈发生了老师毫无感知，只有存证那条线（NoiseIncidentPipeline）在跑。
// 本对象把该信号翻译成两个提醒动作：
//   ① voiceRequested → 教室本地 TTS 播报（"请注意保持安静"）
//   ② eventRequested → 云端事件（warn，管理端可见提醒）
//
// 定位：应用/装配层"提醒执行器"。判定逻辑仍在 NoiseMonitor（本件不判定、不改阈值），
// 与 NoiseIncidentPipeline（存证）、CloudNoiseTransport（传输）同层级、各管一段。
// 信号槽收口（voiceRequested/eventRequested），由装配层（noise_assembly.cpp）决定
// 具体动作（speakText / sendEvent），本件不直接知道网络与音视频实现 → 可单测。
//
// 适配：
//   · QSettings 可关语音 / 换文案：noiseAnnounceVoice(bool，默认 true) /
//     noiseAnnounceText(string，默认「请注意保持安静，教室噪音超标」)；
//   · 默认只播一次/每节次 —— NoiseMonitor 的 m_triggered 锁已保证不重复轰炸
//     （同统计周期内 thresholdReached 只发一次，resetTriggerCounter 随节次复位）；
//   · 语音通道由 tts 层决定（EdgeTTS，SAPI 兜底，已在 tts_speaker 里适配）。
class NoiseAnnouncer : public QObject
{
    Q_OBJECT

public:
    explicit NoiseAnnouncer(QObject *parent = nullptr);

    void setVoiceEnabled(bool on);               // 供配置/测试绕过 QSettings
    void setAnnounceText(const QString &text);   // 供配置/测试

public slots:
    /** 3 次超阈触发（hits=累计超阈次数，db=本次判定分贝）。 */
    void onNoiseThreshold(int hits, double db);

signals:
    void voiceRequested(const QString &text);
    void eventRequested(const QString &level, const QString &msg, const QJsonObject &extra);

private:
    bool m_voiceEnabled = true;
    QString m_text = QStringLiteral("请注意保持安静，教室噪音超标");
};