// 语音对讲 · 被控端播放（设计文档 3.3 第一版，2026-10-07）
//
// winmm waveOut 播放 PCM（16kHz 单声道 16bit），系统自带零依赖。
// 学生端只收不采：收到音频帧 → 播放；无帧则静音。不做录音（3.3.6 安全）。
//
// 线程模型：waveOut 是系统回调；本类在主线程调用 write() 逐帧写入。
// 多缓冲轮转（2026-10-13 改）：原来是「单缓冲 + 每帧 waveOutReset 再写」—— 每 20ms
//   把正在播的上一帧尾巴砍掉，语音必然周期性「咔咔」，且 m_buffer 重分配与异步播放
//   争同一块内存。现在开 kSlots 个独立槽轮转：每帧写进一个**空闲槽**，播完由驱动置
//   WHDR_DONE，下次轮到该槽即复用。槽全占＝播放端比音频节奏慢 ⇒ 丢这一帧
//   （语音实时性优先，不积压、不阻塞、不砍前一帧）。

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
    // 轮转槽数：8 槽 × 20ms ≈ 160ms 抖动余量，够吸收调度/网络抖动而不明显增延迟。
    static constexpr int kSlots = 8;

    bool m_open = false;
    QString m_lastError;
    HWAVEOUT m_waveOut = nullptr;
    WAVEHDR m_hdrs[kSlots] = {};   // 每槽独立头，各自 prepare/write
    QByteArray m_bufs[kSlots];     // 每槽独立缓冲（深拷贝进这里，驱动异步读它）
    int m_nextSlot = 0;            // 下一帧优先尝试的槽（轮转起点）
};

#endif // AUDIO_PLAYER_H
