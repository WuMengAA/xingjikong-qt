// 远控画面 QML 图像提供器实现
#include "vnc_image_provider.h"

#include <QMutexLocker>

VncImageProvider::VncImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage VncImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
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

void VncImageProvider::update(const QImage &img)
{
    if (img.isNull()) return;
    QMutexLocker l(&m_mutex);
    m_img = img;
}

void VncImageProvider::clear()
{
    QMutexLocker l(&m_mutex);
    m_img = QImage();
}
