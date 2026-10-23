#include "class_thumb_provider.h"
#include "class_service.h"

ClassThumbProvider::ClassThumbProvider(ClassService *svc)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_svc(svc)
{
}

QImage ClassThumbProvider::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    // id 形如 "<uid>" 或 "<uid>?t<rev>"：剥掉 query（缓存破除戳）再查。
    const QString uid = id.section(QLatin1Char('?'), 0, 0);
    QImage img;
    if (m_svc && !uid.isEmpty()) {
        img = m_svc->thumbnail(uid);
    }
    // 还没有第一帧（刚订阅 / 该机器还没上报）：交透明占位图而不是空图 ——
    // 空图会让 Qt 刷 "Failed to get image from provider"（一台设备一条，50 台
    // 就是 50 行，看起来像崩了），且近黑占位会显脏（FrameImageProvider 同款教训）。
    if (img.isNull()) {
        QSize ph = (requested.isValid() && !requested.isEmpty()) ? requested : QSize(160, 100);
        if (ph.width() <= 0 || ph.height() <= 0) ph = QSize(160, 100);
        QImage placeholder(ph, QImage::Format_ARGB32);
        placeholder.fill(Qt::transparent);
        if (size) *size = ph;
        return placeholder;
    }
    if (size) *size = img.size();
    return img;
}
