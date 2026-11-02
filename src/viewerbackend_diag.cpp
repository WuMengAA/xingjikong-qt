// 诊断与反馈 + 事件流水（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）
//
// // 搬出理由：诊断只读快照（日志尾部 / 系统信息 / 连接状态）+ 两个 HTTP 拉取，
// // 一次性产物，不改任何业务状态 —— 正是「产出数据」那一侧。

#include "viewerbackend.h"
#include "viewerbackend_shared.h"
#include "oauthlogin.h"
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

// ── 2026-10-11：QML 热更（一源响应式）──
// 手机端定期拉与桌面一致的 QML 树：GET /api/qml-patch → {version,url}，
// 与本地 qml-live/.version 不同 → 下载 zip → QZipReader 解压到 qml-live/
// （下次启动 main.cpp 本地优先加载）。改 UI 不用重出 APK。
// ── 诊断与反馈（2026-10-12 · GUI 子系统后没控制台，出错要看界面）─────────────
QString ViewerBackend::viewerLogTail(int maxLines) const
{
    if (m_logPath.isEmpty()) return QStringLiteral("（未知日志路径）");
    QFile f(m_logPath);
    if (!f.exists()) return QStringLiteral("（日志文件还不存在：%1）").arg(m_logPath);
    if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("（日志打不开：%1）").arg(f.errorString());
    // 只读尾部：日志可能不小，从末尾最多回读 512KB，够几百行。
    const qint64 sz = f.size();
    const qint64 cap = 512 * 1024;
    const qint64 start = qMax<qint64>(0, sz - cap);
    f.seek(start);
    const QByteArray raw = f.readAll();
    f.close();
    QStringList lines = QString::fromLocal8Bit(raw).split(QLatin1Char('\n'));   // 日志以本机编码落盘
    if (start > 0 && !lines.isEmpty()) lines.removeFirst();                      // 首行可能被截断
    if (maxLines > 0 && lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join(QLatin1Char('\n'));
}
QString ViewerBackend::diagnosticsSummary() const
{
    QStringList out;
    out << QStringLiteral("星集控 · 管理端 诊断信息");
    out << QStringLiteral("版本：%1").arg(m_version);
    out << QStringLiteral("时间：%1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    out << QStringLiteral("系统：%1（%2）").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
    out << QStringLiteral("云端地址：%1").arg(m_url);
    out << QStringLiteral("连接：%1 ｜ 鉴权：%2")
               .arg(m_connected ? QStringLiteral("已连接") : QStringLiteral("未连接"),
                    m_authed ? QStringLiteral("已鉴权") : QStringLiteral("未鉴权"));
    out << QStringLiteral("身份：%1").arg(m_role.isEmpty() ? QStringLiteral("（无）") : m_role);
    out << QStringLiteral("设备数：%1（在线 %2）").arg(m_devices.size()).arg(onlineCount());
    out << QStringLiteral("当前设备：%1").arg(m_currentUid.isEmpty() ? QStringLiteral("（未选）") : m_currentUid);
    out << QStringLiteral("日志文件：%1").arg(m_logPath);
    return out.join(QLatin1Char('\n'));
}
void ViewerBackend::copyToClipboard(const QString &text) const
{
    if (QClipboard *cb = QGuiApplication::clipboard()) cb->setText(text);
}
void ViewerBackend::openLogFolder() const
{
    const QString dir = m_logPath.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            : QFileInfo(m_logPath).absolutePath();
    if (!dir.isEmpty()) steOpenUrlExternal(QUrl::fromLocalFile(dir));   // 提权安全（oauthlogin.h）
}
void ViewerBackend::requestDiagFeedback()
{
    emit diagFeedbackRequested();
}
void ViewerBackend::requestTodaySchedule()
{
    emit todayScheduleRequested();
}
// ── 2026-10-11：拉事件流水（/api/events，最近 50 条）→ m_events + eventsChanged ──
// 手机端告警页数据源（桌面 NotifyDialog 已有本地通知，不需要它；仅手机端拉）。
void ViewerBackend::fetchEvents()
{
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    httpUrl += QStringLiteral("/api/events?limit=50");
    QNetworkRequest req{ QUrl(httpUrl) };
    const QByteArray token = !m_cloudTicket.isEmpty()
        ? m_cloudTicket.toUtf8() : m_token.toUtf8();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                m_events = doc.object().value(QStringLiteral("events")).toArray();
                emit eventsChanged();
            }
        } else {
            logf("[viewer] 拉事件流水失败（HTTP %d）", status);
        }
        reply->deleteLater();
    });
}
// ── 2026-10-19：拉动作执行排行（/api/actions/ranking，最近 7 天）→ 概览页排行卡 ──
void ViewerBackend::fetchActionRanking()
{
    // 2026-10-13 审计 2.9：m_nam 已在构造函数创建，这里直接用
    QString httpUrl = m_url;
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"), QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/"));
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    httpUrl += QStringLiteral("/api/actions/ranking?days=7&limit=20");
    QNetworkRequest req{ QUrl(httpUrl) };
    const QByteArray token = !m_cloudTicket.isEmpty()
        ? m_cloudTicket.toUtf8() : m_token.toUtf8();
    if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                m_actionRanking = doc.object().value(QStringLiteral("ranking")).toArray();
                emit actionRankingChanged();
            }
        } else {
            logf("[viewer] 拉动作排行失败（HTTP %d）", status);
        }
        reply->deleteLater();
    });
}
