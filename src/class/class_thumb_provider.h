#pragma once
#include <QQuickImageProvider>
#include <QSize>

class ClassService;

// 班级缩略图图像提供器（#460 · 2026-10-23）
//
// QML 用法：Image { source: "image://classthumb/" + uid + "?t" + thumbRev }
//   · uid 后的 "?t<rev>" 是缓存破除戳：source 字符串不变 Qt 不会重取图，
//     而缩略图 15s 一换 —— rev 由 ClassDeviceModel.thumbRev 给（每次新图 +1）。
//   · id 的 query 部分必须剥掉再查 uid（与 image://frames 同款处理）。
//
// 为什么另开一个 provider 而不搭 image://frames 的 uid 通道：frames 那个通道
// 服务的是旧多班缩略图墙（subscribeThumbnails 走按设备订阅的 JPEG 轮询帧），
// 班级域缩略图走 class-event{kind:thumbnail}（独立小尺寸抓帧 + 云端闸门）——
// 两条链路的数据源、节奏、权限语境都不同，混一个通道迟早互相覆盖。
class ClassThumbProvider : public QQuickImageProvider
{
public:
    explicit ClassThumbProvider(ClassService *svc);

    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;

private:
    ClassService *m_svc;
};
