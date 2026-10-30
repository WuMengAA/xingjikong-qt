#include "noise_desktop_snapshot.h"

#include "../screen_capture.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QStandardPaths>
#include <QtGlobal>

namespace {

/// 抽样判"全黑"：桌面被锁 / Session 0 无桌面 / 显示器休眠时 BitBlt 不报错但给一片黑。
/// 与 ThumbnailReporter 的同名检查同口径（64 点全零），但**不引用它** ——
/// 那是另一个模块的内部实现，为省 6 行代码去暴露它不值当。
bool allBlack(const uint8_t *bgrx, int w, int h)
{
    if (!bgrx || w <= 0 || h <= 0) return true;
    int zero = 0;
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            const int x = qBound(0, (w * i) / 7, w - 1);
            const int y = qBound(0, (h * j) / 7, h - 1);
            const uint8_t *p = bgrx + (static_cast<size_t>(y) * w + x) * 4;
            if (p[0] == 0 && p[1] == 0 && p[2] == 0) ++zero;
        }
    }
    return zero == 64;
}

/// 编码：JPEG q70；超 400KB 再降 q45（一帧 1280 长边通常 90~150KB）。
/// 上限存在的意义：云端 WS 帧上限 4MB，base64 后 ×1.34 —— 不设闸门迟早撞上。
QByteArray encodeJpeg(const QImage &img)
{
    const int qualities[2] = {70, 45};
    for (int q : qualities) {
        QByteArray out;
        QBuffer buf(&out);
        if (!buf.open(QIODevice::WriteOnly)) return {};
        if (!img.save(&buf, "JPG", q)) return {};
        buf.close();
        if (out.size() <= 400 * 1024) return out;
    }
    return {};
}

} // namespace

QString DesktopSnapshot::outputDir()
{
    // 与 main.cpp 的 dataRoot() 同一族（AppDataLocation/Stelarith/<子目录>），
    // 只是 main 里那份是 TU 内 static，这里独立算一遍（同一公式，不复制业务）。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/Stelarith/noise_evidence");
    QDir().mkpath(dir);
    return dir;
}

int DesktopSnapshot::maxEdge()
{
    return 1280;
}

bool DesktopSnapshot::capture(QString *outPath, QString *why)
{
    const auto fail = [why](const QString &msg) {
        if (why) *why = msg;
        return false;
    };

#ifndef _WIN32
    return fail(QStringLiteral("非 Windows 平台无桌面抓帧实现"));
#else
    // 新建实例 = 走 firstFrame 分支（100ms 等合成器），见头注释。
    ScreenCapture cap;
    if (!cap.init(0, 0)) return fail(QStringLiteral("抓屏后端初始化失败（无 D3D11 且 GDI 亦失败）"));

    int w = 0, h = 0;
    const uint8_t *frame = cap.grab(w, h);
    if (!frame || w <= 0 || h <= 0) return fail(QStringLiteral("抓屏未取得帧（桌面会话不可用？）"));
    if (allBlack(frame, w, h)) return fail(QStringLiteral("抓到全黑画面（锁屏或无人桌面会话）"));

    // BGRX 紧凑缓冲的小端布局 = QImage::Format_RGB32，零转换直读（与缩略图同款）。
    QImage img(frame, w, h, w * 4, QImage::Format_RGB32);
    if (img.isNull()) return fail(QStringLiteral("帧缓冲转 QImage 失败"));

    // 长边限 1280：只缩不放（小屏抓出来比 1280 小就保持原样，别放大成糊图）。
    const int edge = maxEdge();
    if (qMax(w, h) > edge) {
        img = img.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    const QString path = outputDir()
        + QStringLiteral("/desk-%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());
    const QByteArray jpeg = encodeJpeg(img);
    if (jpeg.isEmpty()) return fail(QStringLiteral("JPEG 编码失败"));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return fail(QStringLiteral("写文件失败：%1").arg(path));
    if (f.write(jpeg) != jpeg.size()) {
        f.close();
        QFile::remove(path);   // 半截文件不许留着冒充存证
        return fail(QStringLiteral("写文件不完整：%1").arg(path));
    }
    f.close();
    if (outPath) *outPath = path;
    return true;
#endif
}
