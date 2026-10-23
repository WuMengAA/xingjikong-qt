// 星集控 · 广播社成员模块 · 实现（2026-10-23）
// 设计说明与数据契约见 broadcastclubmodule.h 顶部。

#include "broadcastclubmodule.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

BroadcastClubModule::BroadcastClubModule(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    loadToken();
}

void BroadcastClubModule::setBaseUrl(const QString &v)
{
    QString u = v.trimmed();
    while (u.endsWith(QLatin1Char('/'))) u.chop(1);
    m_baseUrl = u;
}

// ── 令牌持久化（QSettings：modules/broadcastclub/*）─────────────────────────
// ⚠️ 令牌是 HMAC 票据（claims 含 broadcast_club 角色 + station），明文存本地可接受
//    （教室机本地），但不写日志、不进上报、不打印。

void BroadcastClubModule::loadToken()
{
    QSettings s;
    m_token   = s.value(QStringLiteral("modules/broadcastclub/token")).toString();
    m_station = s.value(QStringLiteral("modules/broadcastclub/station")).toString();
    m_loggedIn = !m_token.isEmpty();
    if (m_loggedIn) emit authChanged();
}

void BroadcastClubModule::saveToken(const QString &token, const QString &station)
{
    QSettings s;
    s.setValue(QStringLiteral("modules/broadcastclub/token"), token);
    s.setValue(QStringLiteral("modules/broadcastclub/station"), station);
    m_token = token;
    m_station = station;
    m_loggedIn = true;
    emit authChanged();
}

void BroadcastClubModule::clearToken()
{
    QSettings s;
    s.remove(QStringLiteral("modules/broadcastclub/token"));
    s.remove(QStringLiteral("modules/broadcastclub/station"));
    m_token.clear();
    m_station.clear();
    m_loggedIn = false;
    emit authChanged();
}

// ── 登录 ───────────────────────────────────────────────────────────────────

void BroadcastClubModule::login(const QString &username, const QString &password)
{
    m_lastError.clear();
    if (m_baseUrl.isEmpty()) {
        m_lastError = QStringLiteral("未配置云端地址");
        emit loginFinished(false, m_lastError);
        return;
    }
    if (username.trimmed().isEmpty() || password.isEmpty()) {
        m_lastError = QStringLiteral("用户名与密码不能为空");
        emit loginFinished(false, m_lastError);
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("username"), username.trimmed());
    body.insert(QStringLiteral("password"), password);
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/broadcast-club/login")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r] { onLoginFinished(r); });
}

void BroadcastClubModule::onLoginFinished(QNetworkReply *r)
{
    r->deleteLater();
    const int httpCode = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = r->readAll();

    if (r->error() == QNetworkReply::NoError && httpCode >= 200 && httpCode < 300) {
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
        const QJsonObject o = (perr.error == QJsonParseError::NoError && doc.isObject())
                            ? doc.object() : QJsonObject();
        const QString tok = o.value(QStringLiteral("token")).toString();
        const QString stn = o.value(QStringLiteral("station")).toString();
        if (tok.isEmpty()) {
            m_lastError = QStringLiteral("登录响应缺少令牌");
            emit loginFinished(false, m_lastError);
            return;
        }
        saveToken(tok, stn);
        m_lastError.clear();
        refreshSchedules();   // 登录后立刻拉排期
        emit loginFinished(true, QStringLiteral("登录成功"));
        return;
    }

    QString msg;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (doc.isObject()) msg = doc.object().value(QStringLiteral("error")).toString();
    if (msg.isEmpty())
        msg = httpCode == 401 ? QStringLiteral("用户名或密码错误")
            : httpCode > 0   ? QStringLiteral("HTTP %1").arg(httpCode)
            : r->errorString();
    m_lastError = msg;
    emit loginFinished(false, msg);
}

void BroadcastClubModule::logout()
{
    clearToken();
    m_schedule.clear();
    emit scheduleChanged();
}

// ── 拉取排期 ────────────────────────────────────────────────────────────────
// GET {base}/api/broadcast-club/schedules —— 云端代理透传 VoiceHub 公开排期数组。

void BroadcastClubModule::refreshSchedules()
{
    if (!m_loggedIn || m_token.isEmpty() || m_baseUrl.isEmpty()) return;
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/broadcast-club/schedules")));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setRawHeader(QByteArrayLiteral("Authorization"),
                     QByteArrayLiteral("Bearer ") + m_token.toUtf8());
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] { onScheduleFinished(r); });
}

void BroadcastClubModule::onScheduleFinished(QNetworkReply *r)
{
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) {
        const int httpCode = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 401/403 = 令牌失效（被禁用/过期）⇒ 清登录态，回到登录表单
        if (httpCode == 401 || httpCode == 403) clearToken();
        return;   // 拉不到就保留上一份，不清空（避免面板瞬间空白）
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(r->readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        m_lastError = QStringLiteral("排期列表解析失败（期望数组）");
        return;
    }

    QVariantList list;
    const QJsonArray arr = doc.array();
    list.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        const QJsonObject song = o.value(QStringLiteral("song")).toObject();
        QVariantMap m;
        m[QStringLiteral("id")]               = o.value(QStringLiteral("id")).toInt();
        m[QStringLiteral("sequence")]         = o.value(QStringLiteral("sequence")).toInt();
        m[QStringLiteral("played")]           = o.value(QStringLiteral("played")).toBool();
        m[QStringLiteral("isPlaying")]        = o.value(QStringLiteral("isPlaying")).toBool();
        const QJsonValue bp = o.value(QStringLiteral("broadcastPosition"));
        m[QStringLiteral("broadcastPosition")] = bp.isDouble() ? bp.toDouble() : -1.0;
        m[QStringLiteral("playDate")]         = o.value(QStringLiteral("playDate")).toString();
        m[QStringLiteral("title")]            = song.value(QStringLiteral("title")).toString();
        m[QStringLiteral("artist")]           = song.value(QStringLiteral("artist")).toString();
        m[QStringLiteral("cover")]            = song.value(QStringLiteral("cover")).toString();
        m[QStringLiteral("durationSeconds")]  = song.value(QStringLiteral("durationSeconds")).toInt();
        m[QStringLiteral("musicPlatform")]    = song.value(QStringLiteral("musicPlatform")).toString();
        m[QStringLiteral("musicId")]          = song.value(QStringLiteral("musicId")).toString();
        m[QStringLiteral("songId")]           = song.value(QStringLiteral("id")).toInt();
        list.append(m);
    }

    if (list != m_schedule) {
        m_schedule = list;
        emit scheduleChanged();
    }
}

// ── 重排 ────────────────────────────────────────────────────────────────────
// POST {base}/api/broadcast-club/schedules/reorder，Bearer 令牌；
// 云端代理到 VoiceHub 并落审计（密钥云端持有，不下发）。

void BroadcastClubModule::reorder(const QVariantList &pairs)
{
    m_lastReorderError.clear();
    if (!m_loggedIn || m_token.isEmpty()) {
        m_lastReorderError = QStringLiteral("未登录");
        emit reorderFinished(false, m_lastReorderError);
        return;
    }
    if (m_baseUrl.isEmpty()) {
        m_lastReorderError = QStringLiteral("未配置云端地址");
        emit reorderFinished(false, m_lastReorderError);
        return;
    }

    QJsonArray arr;
    for (const QVariant &v : pairs) {
        if (!v.canConvert<QVariantMap>()) continue;
        const QVariantMap m = v.toMap();
        QJsonObject o;
        o.insert(QStringLiteral("id"), m.value(QStringLiteral("id")).toInt());
        o.insert(QStringLiteral("sequence"), m.value(QStringLiteral("sequence")).toInt());
        arr.append(o);
    }
    if (arr.isEmpty()) {
        emit reorderFinished(false, QStringLiteral("没有可保存的排期"));
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("schedules"), arr);

    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/broadcast-club/schedules/reorder")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader(QByteArrayLiteral("Authorization"),
                     QByteArrayLiteral("Bearer ") + m_token.toUtf8());
    QNetworkReply *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r] { onReorderFinished(r); });
}

void BroadcastClubModule::onReorderFinished(QNetworkReply *r)
{
    r->deleteLater();
    const int httpCode = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = r->readAll();

    if (r->error() == QNetworkReply::NoError && httpCode >= 200 && httpCode < 300) {
        m_lastReorderError.clear();
        emit reorderFinished(true, QStringLiteral("已保存"));
        return;
    }

    if (httpCode == 401 || httpCode == 403) clearToken();  // 令牌失效 ⇒ 回登录
    QString msg;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (doc.isObject()) msg = doc.object().value(QStringLiteral("error")).toString();
    if (msg.isEmpty())
        msg = httpCode == 401 ? QStringLiteral("登录已失效，请重新登录")
            : httpCode > 0   ? QStringLiteral("HTTP %1").arg(httpCode)
            : r->errorString();
    m_lastReorderError = msg;
    emit reorderFinished(false, msg);
}
