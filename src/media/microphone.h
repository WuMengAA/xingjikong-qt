#pragma once
// 被控端麦克风采集（2026-10-09 · 语音通话功能设计 §4.1 独立组件）
//
// 语音对讲升级为双向通话：学生端需要 mic 会话内采集，回传给老师。
// 现被控端「只收不采」（3.3.6 安全），本组件为**受控放开**：
//   · start()/stop() 显式开关（只有通话会话才开）
//   · 硬上限（默认 120s）自动停——防挂起成窃听器
//   · 采集帧 16kHz 单声道 16bit PCM → pcmReady(QByteArray) 信号，
//     由上层发回云端（单播发起老师）
//   · winmm waveIn（与被控端 waveOut 播放同族；noise_monitor 的 WASAPI
//     骨架留作将来替换）
//
// 接入（等 main.cpp 工作区解禁）：audio.start{duplex:true} → start()；
// audio.stop / 超时 / 考试模式(NotificationGate) → stop()。

#include <QObject>
#include <QByteArray>
#include <QTimer>
#include <windows.h>

class Microphone : public QObject
{
    Q_OBJECT
public:
    explicit Microphone(QObject *parent = nullptr);
    ~Microphone() override;

    // 开始采集（幂等）。maxMs 为硬上限（默认 120s），到点自动 stop + emit hardStop。
    bool start(int maxMs = 120000);
    void stop();

    bool running() const { return m_running; }
    int elapsedMs() const { return m_elapsedMs; }

signals:
    // 每帧 PCM（16kHz 单声道 16bit）
    void pcmReady(const QByteArray &pcm);
    // 达到硬上限自动停（上层应报告"通话超时结束"）
    void hardStop();

private:
    bool openDevice();
    void closeDevice();
    void pump();           // 定时读缓冲

    bool m_running = false;
    int m_maxMs = 120000;
    int m_elapsedMs = 0;
    QTimer m_tick;         // 采集节拍（20ms/帧）
    QTimer m_watch;        // 硬上限看门狗

    HWAVEIN m_waveIn = nullptr;
    WAVEHDR m_header = {};
    QByteArray m_buffer;   // 采集缓冲
};