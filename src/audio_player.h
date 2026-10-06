// 语音对讲 · 被控端播放（设计文档 3.3 第一版，2026-10-07）
//
// winmm waveOut 播放 PCM（16kHz 单声道 16bit），系统自带零依赖。
// 学生端只收不采：收到音频帧 → 播放；无帧则静音。不做录音（3.3.6 安全）。
//
// 线程模型：waveOut 是系统回调；本类在主线程调用 write() 逐帧写入。
// 单缓冲 + 不追帧：迟到的帧直接丢弃（语音实时性优先，不卡缓冲）。

#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <QObject>
#include <QByteArray>
#include <windows.h>

class AudioPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    /** 打开播放设备（16kHz 单声道 16bit）。失败返回 false 并设置 lastError。 */
    bool open();
    /** 关闭播放设备（静音）。 */
    void closePlayer();

    /** 写入一帧 PCM（应每 20ms 一帧 640 字节）。未 open 则忽略。 */
    void write(const QByteArray &pcm);

    bool isOpen() const { return m_open; }
    QString lastError() const { return m_lastError; }

private:
    bool m_open = false;
    QString m_lastError;
    HWAVEOUT m_waveOut = nullptr;
    WAVEHDR m_header = {};
    QByteArray m_buffer;
};

#endif // AUDIO_PLAYER_H
