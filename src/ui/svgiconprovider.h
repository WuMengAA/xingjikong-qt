#pragma once
#include <QQuickImageProvider>
#include <QPixmap>

// 2026-10-11 灵动岛 QML 的 SVG 图标提供器。
// QML 侧：Image { source: "image://svgicon/<name>?color=" + encodeURIComponent(tint) }
// 颜色在请求时按主题染（深浅主题自动适配），SVG 从 qrc 前缀 /icons 读取（编译进 exe）。
//
// ⚠️ 与 Stelarith-viewer-qt/src/svgiconprovider.{h,cpp} 是**同一份实现**的副本。
//    不共享是因为两端是两个独立 git 工程（跨工程 include 会造出反向依赖）。
//    改这里请同步改那边 —— 两边的 QML 都用 svgicon:// 协议，行为必须一致。
class SvgIconProvider : public QQuickImageProvider {
public:
    SvgIconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};
