#pragma once
// 管理端语音通话播放器（2026-10-09 · 语音通话功能设计 §4.3 独立组件）
//
// 用途：学生端回传的 audio/pcm（16kHz 单声道 16bit）在管理端播放。
// 与被控端 AudioPlayer 同族（winmm waveOut），但独立实现、不依赖被控端代码。
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
// 2026-10-11：Android target 手机 App 无 waveOut，stub 实现（audio_playback_android.cpp）
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
    HWAVEOUT m_waveOut = nullptr;
    WAVEHDR m_header = {};
    QByteArray m_buffer;      // waveOut 正在播的缓冲（播放期间必须保持存活）
#endif
};