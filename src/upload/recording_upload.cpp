// 录像/班级监控上传（2026-10-30 从 main.cpp 整块迁出；逻辑与原地逐字一致，仅改声明位置）。
#include "recording_upload.h"

#include <QCoreApplication>   // qEnvironmentVariable
#include <QDebug>              // qInfo / qWarning
#include <QFile>
#include <QFileInfo>
#include <QLatin1String>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

// g_uid 定义在 main.cpp（本机设备码，register 时定下来）。本模块只读取，
// 沿用 MediaSession.cpp 既有的「跨 TU 共享全局」约定（同处 g_shotDir / g_mediaDir 也这么引）。
extern QString g_uid;

void uploadRecordingAsync(const QString &mp4Path)
{
    if (!QFileInfo::exists(mp4Path)) return;
    const QString wsUrl = qEnvironmentVariable("STE_QT_WS_URL").trimmed();
    if (wsUrl.isEmpty()) { qWarning("[rec] 上传跳过：没有 STE_QT_WS_URL"); return; }
    // ws://host:port/ws/agent → http://host:port/api/recordings/upload?uid=...
    QString httpUrl = wsUrl;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    httpUrl += QStringLiteral("/api/recordings/upload?uid=") + g_uid;

    auto *mgr = new QNetworkAccessManager();
    QNetworkRequest req((QUrl(httpUrl)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    const QString token = qEnvironmentVariable("STE_QT_WS_TOKEN").trimmed();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token.toUtf8());

    QFile *file = new QFile(mp4Path);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning("[rec] 上传失败：打不开 %s", qPrintable(mp4Path));
        delete file; delete mgr;
        return;
    }
    QNetworkReply *reply = mgr->post(req, file);
    QObject::connect(reply, &QNetworkReply::finished, [reply, file, mgr, mp4Path]() {
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 200) {
            qInfo("[rec] 📹 录制已上传云端：%s", qPrintable(mp4Path));
        } else {
            qWarning("[rec] 上传失败（HTTP %d）：%s", code, qPrintable(mp4Path));
        }
        reply->deleteLater();
        file->deleteLater();
        mgr->deleteLater();
    });
}
