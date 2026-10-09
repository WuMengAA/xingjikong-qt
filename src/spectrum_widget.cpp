#include "spectrum_widget.h"

#include <QPainter>
#include <QTimer>
#include <QtMath>
#include <cstring>

namespace {
// 简易 FFT 能量：不引第三方库，按块均值 + 每带取不同采样密度近似频率分带。
// 教室机对讲是语音，16 带照常识映射到 0-4kHz（语音能量带）即可，不追求精确谱。
inline float bandEnergy(const float* pcm, int n, int band)
{
    if (n <= 0) return 0.f;
    // 每带取一段（线性分带 0..n），算 RMS
    const int lo = (band * n) / SpectrumWidget::kBands;
    const int hi = ((band + 1) * n) / SpectrumWidget::kBands;
    double sum = 0.0;
    int c = 0;
    for (int i = lo; i < hi && i < n; ++i) {
        sum += double(pcm[i]) * double(pcm[i]);
        ++c;
    }
    if (c == 0) return 0.f;
    return float(qSqrt(sum / double(c)));
}
} // namespace

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    // 无数据时每 30ms 衰减一档，防止残影
    m_decayTimer = new QTimer(this);
    m_decayTimer->setInterval(30);
    connect(m_decayTimer, &QTimer::timeout, this, &SpectrumWidget::decay);
    m_decayTimer->start();
    m_lastData.invalidate();
    setMinimumSize(96, 28);
}

void SpectrumWidget::pushPcm(const QByteArray& data, int sampleRate, int bits)
{
    if (data.isEmpty()) return;
    QVector<float> frame;
    if (bits == 16 && data.size() >= 2) {
        const qint16* s = reinterpret_cast<const qint16*>(data.constData());
        const int n = data.size() / 2;
        frame.resize(n);
        for (int i = 0; i < n; ++i) frame[i] = float(s[i]) / 32768.f;
    } else if (bits == 32 && data.size() >= 4) {
        const float* f = reinterpret_cast<const float*>(data.constData());
        const int n = data.size() / 4;
        frame.resize(n);
        for (int i = 0; i < n; ++i) frame[i] = f[i];
    } else {
        return; // 不认识的格式，忽略
    }

    m_hasData = true;
    m_lastData.restart();
    // 平滑：当前与上一次 70/30 混合，避免条形乱跳
    for (int b = 0; b < kBands; ++b) {
        const float e = bandEnergy(frame.constData(), frame.size(), b);
        const float target = qMin(1.f, e * 6.f);   // 增益：语音幅度偏小
        m_bands[b] = m_hasData ? 0.7f * m_bands[b] + 0.3f * target : target;
    }
    update();
}

void SpectrumWidget::stop()
{
    m_hasData = false;
    for (int i = 0; i < kBands; ++i) m_bands[i] = 0.f;
    update();
}

void SpectrumWidget::decay()
{
    if (!m_hasData || !m_lastData.isValid()) return;
    // 超过 800ms 没新数据 → 认定静音，归零
    if (m_lastData.elapsed() > 800) {
        m_hasData = false;
        for (int i = 0; i < kBands; ++i) m_bands[i] = 0.f;
        update();
    }
}

void SpectrumWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(0, 0, 0, 0));

    const QColor on = m_light ? QColor(30, 30, 30) : QColor(240, 240, 240);
    const QColor dim = m_light ? QColor(200, 200, 200) : QColor(70, 70, 70);

    const int w = width();
    const int h = height();
    const int gap = 2;
    const int bw = (w - gap * (kBands - 1)) / kBands;

    for (int b = 0; b < kBands; ++b) {
        const int bh = qMax(2, int(m_bands[b] * (h - 2)));
        const int x = b * (bw + gap);
        const QRect r(x, h - bh, bw, bh);
        p.fillRect(r, m_bands[b] > 0.01f ? on : dim);
    }
}