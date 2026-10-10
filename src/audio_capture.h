// 语音对讲 · 管理端音频采集（设计文档 3.3 第一版，2026-10-07）
//
// 选型：winmm waveIn（Windows 系统自带，零第三方依赖）。
// Qt 6.8.1 未装 Multimedia 模块（QAudioSource 不可用），故用系统 API。
// 采集 16kHz 单声道 16bit PCM，每 20ms 一帧（640 字节），通过 WebSocket 二进制发出。
//
// 职责边界：只负责"开麦→采集→吐 PCM 帧→关麦"，不碰协议/UI。
// 与云端/WS 的衔接：本类把每帧发到 WebSocket（由 ViewerBackend 注入发送回调）。
//
// 注意（设计文档 3.3.6 安全）：老师端默认静音，只有点"开始讲话"才采集；
// 不采集不录音不存储。停止即完全静音。

#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <QObject>
#include <QByteArray>
#include <functional>

// 2026-10-11：手机 App 并入管理端（Android target）→ 音频采集是 winmm 专属。
// Android 无 waveIn，用空实现 stub（startCapture 返回 false），保持接口不破。
#ifndef Q_OS_ANDROID
#include <windows.h>
#endif

class AudioCapture : public QObject
{
    Q_OBJECT
public:
    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    /** 是否正在采集（麦克风开）。 */
    bool active() const { return m_active; }

    /** 开始采集（16kHz 单声道 16bit）。失败返回 false 并设置 lastError。 */
    bool startCapture();
    /** 停止采集（关麦）。 */
    void stopCapture();

    /** 采集到的 PCM 帧回调（每 20ms 一帧 640 字节）。由外部（ViewerBackend）连接。 */
    std::function<void(const QByteArray &pcm)> onFrame;
    /** 错误回调（如设备被拔）。 */
    std::function<void(const QString &err)> onError;

    QString lastError() const { return m_lastError; }

    /** WIM_DATA 回调触发：当前缓冲采满 → 吐帧 → 重新入队。
     *  公开是因为 waveInProc 是匿名 namespace 的全局 C 回调（friend 无法跨 TU），
     *  这是它的唯一调用入口；QML/外部不会碰它。 */
    void handleBuffer();

private:
    bool m_active = false;
    QString m_lastError;
#ifndef Q_OS_ANDROID
    HWAVEIN m_waveIn = nullptr;
    WAVEHDR m_header = {};
    QByteArray m_buffer;
#endif
};

#endif // AUDIO_CAPTURE_H
