#pragma once
// 16 位频谱显示（2026-10-08 · 通知体系升级 §6.3 语音对讲联动独立组件）
//
// 独立 QWidget：接收一小段 PCM 数据，实时算 16 个频带的能量，画成条形。
//   · 与 main.cpp 解耦：谁收到 audio 帧谁调 pushPcm()，纯渲染不碰通知状态机
//   · 静音 / 无数据 → 自动归零（不残影）
//   · 主题两档（浅色/深色），由 setLightMode 切；默认浅色
//
// 接入（main.cpp）：在对讲帧的解码处把 PCM buffer 调 pushPcm。

#include <QWidget>
#include <QVector>
#include <QByteArray>
#include <QElapsedTimer>

class QPaintEvent;

class SpectrumWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget* parent = nullptr);

    // 喂一段 PCM（s16le 或 f32le，按 sampleRate/bits 自动处理）；看不懂的格式忽略
    void pushPcm(const QByteArray& data, int sampleRate = 16000, int bits = 16);
    // 对讲停止 → 频谱归零隐藏
    void stop();
    // 主题：true 浅色 / false 深色（默认浅色）
    void setLightMode(bool light) { m_light = light; update(); }

    static constexpr int kBands = 16;   // 16 位频谱（需求 §6.3）

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void decay();   // 无新数据时条高缓慢回落（每 30ms 一次）

    float m_bands[kBands] = {0.f};   // 归一化 0..1
    bool m_light = true;
    bool m_hasData = false;
    QElapsedTimer m_lastData;        // 最近一次有数据的时刻（判定静音）
    QTimer* m_decayTimer = nullptr;
};