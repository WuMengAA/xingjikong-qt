#pragma once
#include <QQuickImageProvider>
#include <QPixmap>

// 2026-10-08 lucide(morphicons 同款 24×24 stroke 风格，ISC) SVG 图标提供器。
// QML 侧：Image { source: "image://svgicon/<name>?color=" + encodeURIComponent(tint) }
// 颜色在请求时按主题染（深浅主题自动适配），SVG 从 qrc 前缀 /icons 读取（编译进 exe）。
class SvgIconProvider : public QQuickImageProvider {
public:
    SvgIconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};
