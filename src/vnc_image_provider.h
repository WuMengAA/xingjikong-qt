#pragma once
// 远控画面 QML 图像提供器：VncClient 解出的 QImage 经它交给 QML 的 Image 渲染。
// 渲染管线：libvncclient 帧缓冲 → QImage → 此处 → Image { source: "image://vnc/live" }。
// 绝对禁止 QWidget 画图（约束）。
#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>

class VncImageProvider : public QQuickImageProvider
{
public:
    VncImageProvider();
    // QML 侧 Image { source: "image://vnc/live" } 取图时调用
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    // 主线程调用：推入最新一帧（线程安全）
    void update(const QImage &img);
    void clear();

private:
    QMutex m_mutex;
    QImage m_img;
};
