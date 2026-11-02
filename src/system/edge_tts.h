#pragma once
// 微软 Edge 语音播报（2026-10-08 · 通知体系升级 §6.4 独立模块）
//
// 收听链路：Edge TTS WebSocket → mp3 → ffmpeg 解码 → 16kHz 单声道 16bit PCM → winmm 播放。
//   · 独立 QObject，不依赖 main.cpp：speak() 一把梭（连 → 收 → 解码 → 播），
//     失败走 failed 信号如实报（fail-silent 红线），绝不含糊成"已朗读"。
//   · 音量：默认 75%（需求 §6.4：默认开启 75% 音量），setVolume(0..1) 可调。
//   · ffmpeg 定位与主程序同一约定：STELARITH_AGENT_FFMPEG > STE_QT_FFMPEG > PATH；
//     找不到 → failed(ffmpeg 缺失) —— 不假装朗读成功。
//   · 离线场景（无公网）：发送失败 → failed，由上层回退 SAPI（与需求注记一致）。
//
// 接入（main.cpp）：需要朗读的通知处调 EdgeTts::instance().speak(text)；
// 与现有 speakText(SAPI) 并存，上层按需选择。

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QProcess>
#include <QTimer>

class QWebSocket;

class EdgeTts : public QObject {
    Q_OBJECT
public:
    static EdgeTts &instance();

    // 朗读一段文本（异步）。默认 Edge 中文语音，音量 0.75。
    void speak(const QString &text, const QString &voice = QStringLiteral("zh-CN-XiaoxiaoNeural"), double volume = 0.75);
    // 停止当前朗读（静音）
    void stop();

    bool busy() const { return m_busy; }
    double volume() const { return m_volume; }
    void setVolume(double v) { m_volume = v >= 0.0 && v <= 1.0 ? v : 0.75; }

signals:
    // 朗读完成（或已停止）
    void finished(const QString &text);
    // 失败：reason 含糊不清时也如实报（与 -SAPI 现有纪律一致）
    void failed(const QString &reason);

private:
    explicit EdgeTts(QObject *parent = nullptr);
    ~EdgeTts() override;

    void openSocket();
    void sendSsml(const QString &text);
    void decodeAndPlay();
    void playPcm(const QByteArray &pcm);

    QWebSocket *m_ws = nullptr;
    bool m_busy = false;
    QString m_text;
    QString m_voice;
    double m_volume = 0.75;
    QByteArray m_audio;          // 收到的 mp3 累积
    QProcess m_ffmpeg;           // mp3 -> PCM 解码
    QByteArray m_pcm;
    QTimer m_idle;               // 连接/解码超时护栏
};