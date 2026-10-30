#include "noise_reporter.h"

#include <QDateTime>
#include <QFile>
#include <QJsonObject>

namespace {
// 读文件 → base64。读不到返回空（调用方据空判"抓到但落盘后读不出"）。
QByteArray readBase64(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray raw = f.readAll();
    if (raw.isEmpty()) return {};
    return raw.toBase64();
}
} // namespace

NoiseReporter::NoiseReporter(INoiseTransport *transport)
    : m_transport(transport)
{
}

void NoiseReporter::report(const NoiseIncident &inc, const NoiseEvidence &ev)
{
    if (!m_transport) return;

    QJsonObject p;
    p.insert(QStringLiteral("at"), inc.at.toString(Qt::ISODateWithMs));
    p.insert(QStringLiteral("periodKey"), inc.periodKey);
    p.insert(QStringLiteral("hits"), inc.hits);
    p.insert(QStringLiteral("db"), inc.db);
    p.insert(QStringLiteral("baselineDb"), inc.baselineDb);
    p.insert(QStringLiteral("thresholdDb"), inc.thresholdDb);

    // 两张图：抓到就 base64 上传；抓不到就如实把原因带上（绝不假装成功）。
    // 字段名固定为 desktop/camera（图）与 desktopError/cameraError（原因），
    // 与云端 readNoiseEvidence 的读取口径一一对应。
    auto attach = [&](const QString &field, const QString &path, const QString &capErr) {
        if (!path.isEmpty()) {
            const QByteArray b64 = readBase64(path);
            if (!b64.isEmpty()) {
                p.insert(field, QString::fromLatin1(b64));
                return;
            }
            p.insert(field + QStringLiteral("Error"),
                     QStringLiteral("存证图读取失败（已抓到但落盘后读不出）"));
        } else if (!capErr.isEmpty()) {
            p.insert(field + QStringLiteral("Error"), capErr);
        } else {
            p.insert(field + QStringLiteral("Error"), QStringLiteral("未采集"));
        }
    };
    attach(QStringLiteral("desktop"), ev.desktopPath, ev.desktopError);
    attach(QStringLiteral("camera"), ev.cameraPath, ev.cameraError);

    m_transport->sendIncident(p);
    ++m_sent;

    // 本地存证是一次性上传物：云端是唯一保留方（保留 5 分钟），设备侧不再留副本，
    // 否则每节课 3 张会一直堆在 AppData 里。读不出/已发的都清掉。
    for (const QString &path : { ev.desktopPath, ev.cameraPath }) {
        if (!path.isEmpty()) QFile::remove(path);
    }
}
