#include "noise_camera_snapshot.h"

#include "../media/CameraActions.h"
#include "noise_desktop_snapshot.h"   // 复用 outputDir()（同一公式，不重复实现）

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJsonObject>

namespace {
int maxEdge() { return 1280; }
}

QString CameraSnapshot::outputDir()
{
    return DesktopSnapshot::outputDir();
}

bool CameraSnapshot::capture(QString *outPath, QString *why)
{
    const auto fail = [why](const QString &msg) {
        if (why) *why = msg;
        return false;
    };

    // 抽 1 帧 PNG（CameraActions 内部已处理设备选择 / 超时 / 错误分类）。
    const ExecOut r = CameraActions::snapshot(QJsonObject());
    if (r.result != QStringLiteral("done")) {
        return fail(r.error.isEmpty()
                        ? QStringLiteral("摄像头抓拍失败（原因未提供）")
                        : r.error);
    }
    const QString src = r.data.value(QStringLiteral("path")).toString();
    if (src.isEmpty()) return fail(QStringLiteral("摄像头抓拍未返回图片路径"));

    QImage img(src);
    if (img.isNull()) {
        QFile::remove(src);
        return fail(QStringLiteral("摄像头帧无法解码"));
    }

    // 长边限 1280（同桌面）：只缩不放，避免把小图放大成糊图。
    if (qMax(img.width(), img.height()) > maxEdge()) {
        img = img.scaled(maxEdge(), maxEdge(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    const QString dst = outputDir()
        + QStringLiteral("/cam-%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());
    if (!img.save(dst, "JPG", 70)) {
        QFile::remove(src);
        return fail(QStringLiteral("摄像头帧转 JPEG 失败"));
    }
    QFile::remove(src);   // 原始 PNG 不留垃圾（Reporter 读的是 dst）
    if (outPath) *outPath = dst;
    return true;
}
