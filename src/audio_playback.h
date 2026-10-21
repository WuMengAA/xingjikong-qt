#pragma once
// 管理端语音通话播放器（2026-10-09 · 语音通话功能设计 §4.3 独立组件）
//
// 用途：学生端回传的 audio/pcm（16kHz 单声道 16bit）在管理端播放。
// 桌面：与被控端 AudioPlayer 同族（winmm waveOut），独立实现、不依赖被控端代码。
// Android（手机 App 管理端）：无 waveOut，改用 NDK AAudio OUTPUT 流（详见 .cpp 内联分支）。
//
// 设计：
//   · 懒打开：只有收到第一帧才 open 设备（无通话时不占音频设备）
//   · 连续播放：waveOut 不断流（环形缓冲由 waveOut 队列管理）；静默时无帧不响
//   · 会话结束 stop() 关闭设备（释放麦克风/喇叭不让系统挂起）
//   · 线程：主线程 write()；waveOut 系统回调不直接动 Qt
//
// 接入（等 viewerbackend 工作区解禁）：收上游 audio/pcm 帧处调 write()。
// 编译需链 winmm（与 edge_tts 验证壳同配方）。

#include <QObject>
#include <QByteArray>
// 2026-10-21：Android target 手机 App 无 waveOut，改 NDK AAudio OUTPUT 流真实现
// （实现内联在 audio_playback.cpp 的 #ifdef Q_OS_ANDROID 分支，与 audio_capture_android.cpp 同源范式）。
#ifndef Q_OS_ANDROID
#include <windows.h>
#endif

class AudioPlayback : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayback(QObject *parent = nullptr);
    ~AudioPlayback() override;

    // 写一帧 PCM（16kHz/单声道/16bit）。第一帧触发设备打开；未 open 时静默忽略。
    void write(const QByteArray &pcm);

    // 会话结束：关设备、清缓冲
    void stop();

    bool isOpen() const { return m_open; }

private:
    bool openDevice();
    void closeDevice();

    bool m_open = false;
#ifndef Q_OS_ANDROID
    // 多缓冲轮转：kSlots 个独立槽，每帧写进一个空闲槽，播完由驱动置 WHDR_DONE 复用。
    // 与被控端 AudioPlayer 同族同配方（2026-10-13 验证：单缓冲+每帧 Reset 会周期性咔咔）。
    static constexpr int kSlots = 8;   // 8×20ms≈160ms 抖动余量，够吸收调度/网络抖动
    HWAVEOUT m_waveOut = nullptr;
    WAVEHDR m_hdrs[kSlots] = {};
    QByteArray m_bufs[kSlots];         // 每槽独立缓冲（深拷贝进这里，驱动异步读它）
    int m_nextSlot = 0;                // 下一帧优先尝试的槽（轮转起点）
#endif
};