// 远控 H264 画面 QML 图像提供器实现（A 档第④步，2026-10-31）
#include "H264ImageProvider.h"
#include "mfx_h264_decode.h"   // MfxH264Decoder（仅在 .cpp 引入，符合 §10.4）

#include <QMutexLocker>

H264ImageProvider::H264ImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

H264ImageProvider::~H264ImageProvider()
{
    delete m_dec;
}

bool H264ImageProvider::ensureDecoder(int w, int h)
{
    if (m_decReady && m_decW == w && m_decH == h) return true;
    delete m_dec;
    m_dec = new MfxH264Decoder;
    m_decReady = m_dec->init(w, h);
    m_decW = w;
    m_decH = h;
    if (!m_decReady) {
        // 解码器初始化失败：本机沙箱 MF 不可用时会走这里（A 档预期，B 档真机验证）。
        delete m_dec;
        m_dec = nullptr;
    }
    return m_decReady;
}

void H264ImageProvider::feedNal(const QByteArray &nal)
{
    if (nal.isEmpty()) return;
    QMutexLocker l(&m_mutex);
    if (!m_dec) {
        // 首帧到达时按 1280x720 默认尺寸初始化（被控端合成帧标准尺寸）。
        if (!ensureDecoder(1280, 720)) return;
    }
    QImage out;
    if (m_dec->decodeFrame(nal, out) && !out.isNull()) {
        m_img = out;
    }
}

void H264ImageProvider::clear()
{
    QMutexLocker l(&m_mutex);
    m_img = QImage();
}

QImage H264ImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id);
    QMutexLocker l(&m_mutex);
    if (m_img.isNull()) {
        if (size) *size = QSize();
        return QImage();
    }
    if (size) *size = m_img.size();
    QImage out = m_img;
    if (requestedSize.isValid() && !requestedSize.isEmpty())
        out = out.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return out;
}
