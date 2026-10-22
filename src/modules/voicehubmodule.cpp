// 星集控 · 内置模块：VoiceHub 联动 · 实现（2026-10-17）
// 设计说明、数据契约与状态机口径见 voicehubmodule.h 顶部。

#include "voicehubmodule.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <memory>

// ── 构造 / 清单 ───────────────────────────────────────────────────────────

VoiceHubModule::VoiceHubModule(QObject *parent)
    : AgentModule(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_pollTimer.setSingleShot(false);
    m_pollTimer.setInterval(30 * 1000);
    connect(&m_pollTimer, &QTimer::timeout, this, &VoiceHubModule::pollOnce);

    m_tickTimer.setSingleShot(false);
    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, &VoiceHubModule::tick);

    // 排期列表：比广播状态慢得多（一天变几次），30s 足够，跟随 pollSec。
    m_scheduleTimer.setSingleShot(false);
    m_scheduleTimer.setInterval(30 * 1000);
    connect(&m_scheduleTimer, &QTimer::timeout, this, &VoiceHubModule::fetchSchedule);
}

ModuleManifest VoiceHubModule::manifest() const
{
    ModuleManifest m;
    m.id = QStringLiteral("voicehub");
    m.name = QStringLiteral("VoiceHub 联动");
    m.version = QStringLiteral("0.1.0");
    m.description = QStringLiteral("在灵动岛展示 VoiceHub 广播的「正在播放 / 下一首 / 收听人数」（只读）。控制类能力（点歌 / 歌词 / 广播下发）默认关闭，留桩待 Open API 接线。");
    m.permissions = { QStringLiteral("island"), QStringLiteral("notify"), QStringLiteral("settings") };
    // 这四件事原来是 main.cpp kThirdPartyCaps 的「需要安装插件」友好提示分支；
    // 本模块认领后由 dispatch（内置优先）接到这里：broadcast 给快照，其余三个是留桩回执。
    m.actions = {
        QStringLiteral("voicehub_broadcast"), QStringLiteral("song_request"),
        QStringLiteral("lyrics"), QStringLiteral("broadcast_audio")
    };
    m.events = {};
    m.enabledByDefault = true;
    return m;
}

// ── 启停 ──────────────────────────────────────────────────────────────────

void VoiceHubModule::start()
{
    loadConfig();
    startClient();
}

void VoiceHubModule::stop()
{
    stopClient();
    clearBroadcast();
    setStatus(QStringLiteral("已停用"));
}

// ── 配置 ──────────────────────────────────────────────────────────────────

void VoiceHubModule::loadConfig()
{
    // ⚠️ 2026-10-22：默认地址**改成空**。
    //    原来默认 https://voicehub.245959623.xyz —— 那个 Vercel 部署已被停用
    //    （HTTP 402 DEPLOYMENT_DISABLED），等于给每台教室机发了个死地址：
    //    永远显示"没有广播"，还不报错。宁可显示"未配置"。
    //    部署时用 agent.env 的 STE_VOICEHUB_BASE 注入（script 侧不用点 UI）。
    QString dflt = qEnvironmentVariable("STE_VOICEHUB_BASE").trimmed();
    m_baseUrl   = cfgGet(QStringLiteral("baseUrl"), QJsonValue(dflt)).toString();
    // 环境变量优先级最高（现场排障时改 env 比改注册表快）
    if (!dflt.isEmpty())
        m_baseUrl = dflt;
    m_pollSec   = qBound(5, cfgGet(QStringLiteral("pollSec"), 30).toInt(), 300);
    m_staleSec  = qBound(10, cfgGet(QStringLiteral("staleSec"), 95).toInt(), 600);
    m_autoNotify = cfgGet(QStringLiteral("autoNotify"), true).toBool();
    m_controlEnabled = cfgGet(QStringLiteral("controlEnabled"), false).toBool();
    m_apiKey    = cfgGet(QStringLiteral("apiKey"), QString()).toString();
    if (m_apiKey.isEmpty())
        m_apiKey = qEnvironmentVariable("STE_VOICEHUB_APIKEY").trimmed();
    m_pollTimer.setInterval(m_pollSec * 1000);
    m_scheduleTimer.setInterval(m_pollSec * 1000);
    emit configChanged();
}

QJsonValue VoiceHubModule::cfgGet(const QString &key, const QJsonValue &dflt) const
{
    const ModuleAgentApi &a = api();
    if (!a.moduleGet) return dflt;
    return a.moduleGet(manifest().id, key, dflt);
}

void VoiceHubModule::cfgSet(const QString &key, const QJsonValue &v)
{
    const ModuleAgentApi &a = api();
    if (a.moduleSet) a.moduleSet(manifest().id, key, v);
}

void VoiceHubModule::setBaseUrl(const QString &v)
{
    QString u = v.trimmed();
    while (u.endsWith(QLatin1Char('/'))) u.chop(1);
    if (u == m_baseUrl) return;
    m_baseUrl = u;
    cfgSet(QStringLiteral("baseUrl"), u);
    applyConfigChange();
    emit configChanged();
}

void VoiceHubModule::setPollSec(int v)
{
    v = qBound(5, v, 300);
    if (v == m_pollSec) return;
    m_pollSec = v;
    cfgSet(QStringLiteral("pollSec"), v);
    m_pollTimer.setInterval(v * 1000);
    emit configChanged();
}

void VoiceHubModule::setStaleSec(int v)
{
    v = qBound(10, v, 600);
    if (v == m_staleSec) return;
    m_staleSec = v;
    cfgSet(QStringLiteral("staleSec"), v);
    emit configChanged();
}

void VoiceHubModule::setAutoNotify(bool v)
{
    if (v == m_autoNotify) return;
    m_autoNotify = v;
    cfgSet(QStringLiteral("autoNotify"), v);
    emit configChanged();
}

void VoiceHubModule::setControlEnabled(bool v)
{
    if (v == m_controlEnabled) return;
    m_controlEnabled = v;
    cfgSet(QStringLiteral("controlEnabled"), v);
    emit configChanged();
}

void VoiceHubModule::applyConfigChange()
{
    // 地址/轮询变化：整条客户端链路重启（SSE 重连 + 轮询重新计时），
    // 比"热切换半个状态"稳——配置不是热路径，重启成本一次请求而已。
    if (m_statusText == QStringLiteral("已停用")) return;
    startClient();
}

// ── 传输层 ────────────────────────────────────────────────────────────────

void VoiceHubModule::startClient()
{
    stopClient();
    if (m_baseUrl.isEmpty()) {
        setStatus(QStringLiteral("未配置服务地址"));
        return;
    }
    setStatus(QStringLiteral("连接中…"));
    connectSse();
    m_pollTimer.start();
    m_tickTimer.start();
    m_scheduleTimer.start();
    pollOnce();       // 先拿一次快照，不必等一个轮询周期
    fetchSchedule();  // 排期也立刻拉一次
}

void VoiceHubModule::stopClient()
{
    abortSse();
    m_pollTimer.stop();
    m_tickTimer.stop();
    m_scheduleTimer.stop();
    m_sseConnected = false;
    m_sseBackoffSec = 1;
}

void VoiceHubModule::connectSse()
{
    abortSse();
    const QUrl url(m_baseUrl + QStringLiteral("/api/music/websocket"));
    if (!url.isValid()) {
        setStatus(QStringLiteral("服务地址无效"));
        return;
    }
    QNetworkRequest req(url);
    req.setRawHeader(QStringLiteral("Accept").toUtf8(),
                     QStringLiteral("text/event-stream").toUtf8());
    req.setRawHeader(QStringLiteral("X-Requested-From").toUtf8(),
                     QStringLiteral("XingJiKong-Agent").toUtf8());
    // SSE 是长连接：不给 QNAM 设传输超时（默认无超时），看门在 tick 里做。
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    m_sse = m_nam->get(req);
    connect(m_sse, &QNetworkReply::readyRead, this, &VoiceHubModule::onSseReadyRead);
    connect(m_sse, &QNetworkReply::finished, this, &VoiceHubModule::onSseFinished);
    m_sseBuf.clear();
    m_sseLastByteMs = QDateTime::currentMSecsSinceEpoch();
}

void VoiceHubModule::abortSse()
{
    if (!m_sse) return;
    m_sse->disconnect(this);
    m_sse->abort();
    m_sse->deleteLater();
    m_sse = nullptr;
}

void VoiceHubModule::pollOnce()
{
    if (m_baseUrl.isEmpty()) return;
    const QUrl url(m_baseUrl + QStringLiteral("/api/music/broadcast"));
    if (!url.isValid()) return;
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] { onPollFinished(r); });
}

void VoiceHubModule::onSseReadyRead()
{
    if (!m_sse) return;
    QByteArray chunk = m_sse->readAll();
    m_sseLastByteMs = QDateTime::currentMSecsSinceEpoch();

    // 行尾归一化：SSE 规范允许 \n / \r\n / \r，而帧分隔是「空行」——
    // 服务端若发 \r\n，帧分隔就是 \r\n\r\n（不含连续两个 \n），不归一就永远分不出帧。
    // 边界保护：上一块停在 \r、这一块始于 \n（一个 \r\n 被劈在两次读之间），先拼上再归一。
    if (!m_sseBuf.isEmpty() && m_sseBuf.endsWith('\r')
        && !chunk.isEmpty() && chunk.startsWith('\n'))
        m_sseBuf.chop(1);
    chunk.replace(QByteArrayLiteral("\r\n"), QByteArrayLiteral("\n"));
    m_sseBuf += chunk;

    // SSE 帧以空行分隔；尾部不完整的帧留在缓冲区等下一次。
    int idx = 0;
    while ((idx = m_sseBuf.indexOf(QByteArrayLiteral("\n\n"))) >= 0) {
        const QByteArray frame = m_sseBuf.left(idx);
        m_sseBuf.remove(0, idx + 2);
        if (!frame.trimmed().isEmpty())
            handleSseFrame(QString::fromUtf8(frame));
    }
}

void VoiceHubModule::onSseFinished()
{
    if (!m_sse) return;
    const bool wasConnected = m_sseConnected;
    const QNetworkReply::NetworkError err = m_sse->error();
    const int httpCode = m_sse->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString errStr = m_sse->errorString();
    abortSse();
    m_sseConnected = false;

    if (m_statusText == QStringLiteral("已停用")) return;   // 主动停用：不重连

    // 404/403（服务端没有 SSE 或禁了匿名）⇒ 不是错误态，轮询兜底继续；
    // 只把原因记进 lastError，状态仍是轮询在撑。
    if (err != QNetworkReply::NoError) {
        m_lastError = errStr;
        if (httpCode == 404 || httpCode == 403)
            setStatus(QStringLiteral("SSE 不可用（轮询兜底中）"));
        else if (wasConnected)
            setStatus(QStringLiteral("连接中断（轮询兜底中）"));
        else
            setStatus(QStringLiteral("连接失败（轮询兜底中）"));
        emit stateChanged();
    }
    scheduleSseReconnect();
}

void VoiceHubModule::scheduleSseReconnect()
{
    if (m_statusText == QStringLiteral("已停用")) return;
    QTimer::singleShot(m_sseBackoffSec * 1000, this, [this] {
        if (m_statusText == QStringLiteral("已停用")) return;
        connectSse();
    });
    m_sseBackoffSec = qMin(m_sseBackoffSec * 2, 30);   // 1→2→4…封顶 30s
}

void VoiceHubModule::onPollFinished(QNetworkReply *r)
{
    r->deleteLater();
    const QNetworkReply::NetworkError err = r->error();
    if (err == QNetworkReply::NoError) {
        const QByteArray body = r->readAll();
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
        if (perr.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            const QJsonValue bv = root.value(QStringLiteral("broadcast"));
            applyBroadcast(bv.isObject() ? bv.toObject() : QJsonObject(),
                           bv.isObject(), root);
            m_lastError.clear();
            if (!m_sseConnected && m_statusText.startsWith(QStringLiteral("连接")))
                setStatus(QStringLiteral("轮询兜底中"));
            return;
        }
        m_lastError = QStringLiteral("广播快照解析失败");
    } else if (err != QNetworkReply::OperationCanceledError) {
        // 网络错/404/403 ⇒ 当「没有广播」处理（参考实现同口径：不进错误态、
        // 不清已有状态——停播与否由 stale 判定，不因一次请求失败闪掉）。
        // 但 2026-10-22 补：**必须把 HTTP 状态写进 lastError**。
        // 原来只存 errorString()（"Connection refused" 之类），线上 402 停用
        // 被当成普通网络错，排障时看不出"服务是活的但被停用"。
        const int httpCode = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpCode == 402) {
            m_lastError = QStringLiteral("VoiceHub 服务已停用（HTTP 402）");
        } else if (httpCode > 0) {
            m_lastError = QStringLiteral("VoiceHub 返回 HTTP %1").arg(httpCode);
        } else {
            m_lastError = r->errorString();
        }
    }
    emit stateChanged();
}

// ── 排期列表（2026-10-22）──────────────────────────────────────────────────
//
// GET {base}/api/songs/public —— 匿名可读，返回**数组**（不是对象）。
// 排期表一直在 VoiceHub 里：星集控云端没有排期实体，别在云端另造一套。
// 匿名访问时服务端会把 requester 遮罩成 "***"（隐私），我们只取歌与顺序。

void VoiceHubModule::refreshSchedule()
{
    fetchSchedule();
}

void VoiceHubModule::fetchSchedule()
{
    if (m_baseUrl.isEmpty()) return;
    const QUrl url(m_baseUrl + QStringLiteral("/api/songs/public"));
    if (!url.isValid()) return;
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    req.setRawHeader(QByteArrayLiteral("X-Requested-From"),
                     QByteArrayLiteral("XingJiKong-Agent"));
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r] { onScheduleFinished(r); });
}

void VoiceHubModule::onScheduleFinished(QNetworkReply *r)
{
    r->deleteLater();
    if (r->error() != QNetworkReply::NoError) {
        if (r->error() != QNetworkReply::OperationCanceledError) {
            const int httpCode = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpCode == 402)
                m_lastError = QStringLiteral("VoiceHub 服务已停用（HTTP 402）");
        }
        return;   // 排期拉不到就保留上一份，不清空（清空会让面板瞬间空白）
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
        // broadcastPosition 可能是 null（未在播）——用 -1 表示，QML 里好判
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

// ── 点歌控制（2026-10-22）──────────────────────────────────────────────────
//
// POST {base}/api/open/songs/request，头 x-api-key。
// 服务端只收 ALLOWED_FIELDS（title/artist/cover/musicPlatform/musicId/…），
// 所以**不需要先在本地查到 songId** —— 有就带、没有就靠歌名歌手。

void VoiceHubModule::setApiKey(const QString &v)
{
    const QString k = v.trimmed();
    if (k == m_apiKey) return;
    m_apiKey = k;
    cfgSet(QStringLiteral("apiKey"), k);
    emit configChanged();
}

void VoiceHubModule::requestSong(const QString &title, const QString &artist,
                                 const QString &musicPlatform, const QString &musicId)
{
    m_lastControlError.clear();

    if (!m_controlEnabled) {
        m_lastControlError = QStringLiteral("点歌通道未开启（设置页里打开控制开关）");
        emit controlFinished(false, m_lastControlError);
        return;
    }
    if (m_baseUrl.isEmpty()) {
        m_lastControlError = QStringLiteral("未配置 VoiceHub 服务地址");
        emit controlFinished(false, m_lastControlError);
        return;
    }
    if (m_apiKey.isEmpty()) {
        m_lastControlError = QStringLiteral("未配置 API Key（VoiceHub 后台 → API 密钥 → 建一把带 songs:request 的）");
        emit controlFinished(false, m_lastControlError);
        return;
    }
    if (title.trimmed().isEmpty()) {
        m_lastControlError = QStringLiteral("歌名不能为空");
        emit controlFinished(false, m_lastControlError);
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("title"), title.trimmed());
    body.insert(QStringLiteral("artist"), artist.trimmed());
    body.insert(QStringLiteral("musicPlatform"),
                musicPlatform.isEmpty() ? QStringLiteral("netease") : musicPlatform);
    if (!musicId.isEmpty())
        body.insert(QStringLiteral("musicId"), musicId);

    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/open/songs/request")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // ⚠️ key 只进请求头，不进日志/状态文本
    req.setRawHeader(QByteArrayLiteral("x-api-key"), m_apiKey.toUtf8());
    QNetworkReply *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r] { onRequestFinished(r); });
}

void VoiceHubModule::reorderSchedule(const QVariantList &pairs)
{
    m_lastControlError.clear();

    if (m_baseUrl.isEmpty()) {
        m_lastControlError = QStringLiteral("未配置 VoiceHub 服务地址");
        emit reorderFinished(false, m_lastControlError);
        return;
    }
    if (m_apiKey.isEmpty()) {
        m_lastControlError = QStringLiteral("未配置 API Key（需具备 schedules:write 权限）");
        emit reorderFinished(false, m_lastControlError);
        return;
    }

    // 组装 { schedules: [{ id, sequence }, ...] }，与 admin/schedule/sequence.post.ts 同契约
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

    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/open/schedules/reorder")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // ⚠️ key 只进请求头，不进日志/状态文本
    req.setRawHeader(QByteArrayLiteral("x-api-key"), m_apiKey.toUtf8());
    QNetworkReply *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r] { onReorderFinished(r); });
}

void VoiceHubModule::onReorderFinished(QNetworkReply *r)
{
    r->deleteLater();
    const int httpCode = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = r->readAll();

    if (r->error() == QNetworkReply::NoError && httpCode >= 200 && httpCode < 300) {
        m_lastControlError.clear();
        emit reorderFinished(true, QStringLiteral("已保存"));
        // 顺序已落地，稍后拉取会让排期与服务端一致（无需立刻刷新打断拖拽手感）
        return;
    }

    QString msg;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (doc.isObject())
        msg = doc.object().value(QStringLiteral("message")).toString();
    if (msg.isEmpty())
        msg = httpCode == 401 ? QStringLiteral("API Key 无效（401）")
            : httpCode == 403 ? QStringLiteral("该 Key 没有 schedules:write 权限（403）")
            : httpCode > 0 ? QStringLiteral("HTTP %1").arg(httpCode)
            : r->errorString();

    m_lastControlError = msg;
    emit reorderFinished(false, msg);
}

void VoiceHubModule::onRequestFinished(QNetworkReply *r)
{
    r->deleteLater();
    const int httpCode = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray raw = r->readAll();

    if (r->error() == QNetworkReply::NoError && httpCode >= 200 && httpCode < 300) {
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
        const QJsonObject o = doc.isObject() ? doc.object() : QJsonObject();
        const QString t = o.value(QStringLiteral("title")).toString();
        m_lastControlError.clear();
        emit controlFinished(true,
            t.isEmpty() ? QStringLiteral("已提交") : QStringLiteral("已提交：%1").arg(t));
        // 排期可能立刻变化，拉一次（服务端可能自动排队）
        fetchSchedule();
        return;
    }

    // 失败：把服务端的 message 原样带出来（"系统未设置当前活跃学期"这种要能看到）
    QString msg;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (doc.isObject())
        msg = doc.object().value(QStringLiteral("message")).toString();
    if (msg.isEmpty())
        msg = httpCode == 401 ? QStringLiteral("API Key 无效（401）")
            : httpCode == 403 ? QStringLiteral("该 Key 没有 songs:request 权限（403）")
            : httpCode > 0 ? QStringLiteral("HTTP %1").arg(httpCode)
            : r->errorString();

    m_lastControlError = msg;
    emit controlFinished(false, msg);
}

void VoiceHubModule::handleSseFrame(const QString &frame)
{
    QString eventName;
    QString data;
    const QStringList lines = frame.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString ln = raw;
        if (ln.endsWith(QLatin1Char('\r'))) ln.chop(1);
        if (ln.startsWith(QStringLiteral("event:"))) {
            eventName = ln.mid(6).trimmed();
        } else if (ln.startsWith(QStringLiteral("data:"))) {
            // 本服务端单行 data；多行 data 语义上是追加，照 SSE 规范拼。
            data += ln.mid(5).trimmed();
        }
        // ':' 开头是注释行（心跳常这么发），忽略。
    }
    if (data.isEmpty()) return;

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject msg = doc.object();

    // 类型取法：优先 SSE 规范 event: 行；线上部署版只在 JSON 里给 type
    // （2026-10-17 实测），两条都认才是真兼容。
    const QString type = eventName.isEmpty() ? msg.value(QStringLiteral("type")).toString()
                                             : eventName;
    if (type == QStringLiteral("connection_established")) {
        m_sseConnected = true;
        m_sseBackoffSec = 1;
        setStatus(QStringLiteral("已连接"));
        emit stateChanged();
        return;
    }
    if (type == QStringLiteral("broadcast_state")) {
        m_sseConnected = true;
        m_sseBackoffSec = 1;
        const QJsonValue dv = msg.value(QStringLiteral("data"));
        const QJsonObject d = dv.isObject() ? dv.toObject() : QJsonObject();
        // nextUp / listeners / baselineReleased 的**取法兼容**（2026-10-17 实测口径）：
        //   · 桥接文档说它们挂在 broadcast_state 的 data 对象上（"同一个对象上"）；
        //   · 轮询端点的形状是与 broadcast **平级**挂在快照根上；
        //   · 线上当前无广播，无法实测推广播帧，两种形状都可能出现。
        // ⇒ 先 data 后消息根，取到即用（缺省 null/0 语义不变）。
        QJsonObject extra = d;
        const char *const kSib[] = { "nextUp", "listeners", "baselineReleased" };
        for (const char *k : kSib) {
            const QString key = QString::fromLatin1(k);
            if (!extra.contains(key) && msg.contains(key)) extra.insert(key, msg.value(key));
        }
        applyBroadcast(d, dv.isObject(), extra);
        if (!m_statusText.startsWith(QStringLiteral("已连接")))
            setStatus(QStringLiteral("已连接"));
        return;
    }
    // music_state_update / song_change / playlist_update：旧版 HarmonyOS 通道，忽略。
}

// ── 状态层 ────────────────────────────────────────────────────────────────

void VoiceHubModule::applyBroadcast(const QJsonObject &b, bool has, const QJsonObject &extra)
{
    m_lastSnapshotMs = QDateTime::currentMSecsSinceEpoch();

    if (!has || b.isEmpty()) {
        // 停播（或播控总开关关闭）：整块标记移除，回到「无广播」。
        clearBroadcast();
        return;
    }

    const qint64 songId = b.value(QStringLiteral("songId")).toVariant().toLongLong();
    const bool isNew = !m_active || songId != m_songId;

    m_active = true;
    m_songId = songId;
    m_title   = b.value(QStringLiteral("title")).toString();
    m_artist  = b.value(QStringLiteral("artist")).toString();
    m_cover   = b.value(QStringLiteral("cover")).toString();
    m_durationSec = b.value(QStringLiteral("duration")).toDouble(0);
    m_positionSec = b.value(QStringLiteral("position")).toDouble(0);
    m_playing     = b.value(QStringLiteral("isPlaying")).toBool();
    m_publisher   = b.value(QStringLiteral("publisherName")).toString();

    // 进度锚点：之后每秒本地外推（墙上钟），不每秒发请求。
    m_anchorPos = m_positionSec;
    m_anchorMs  = m_lastSnapshotMs;

    const QJsonValue nu = extra.value(QStringLiteral("nextUp"));
    if (nu.isObject()) {
        m_nextTitle  = nu.toObject().value(QStringLiteral("title")).toString();
        m_nextArtist = nu.toObject().value(QStringLiteral("artist")).toString();
    } else {
        m_nextTitle.clear();
        m_nextArtist.clear();
    }
    m_listeners = extra.value(QStringLiteral("listeners")).toInt(0);

    if (isNew) notifyNewBroadcast();
    emit stateChanged();
}

void VoiceHubModule::clearBroadcast()
{
    // 连接态文案（连接中/兜底中）不因一次「权威 null」被改成「无广播」——
    // 那条文案讲的是链路，这里讲的是内容，两件事。
    const bool keepStatus = m_statusText.endsWith(QStringLiteral("（轮询兜底中）"))
                            || m_statusText == QStringLiteral("连接中…");
    if (!m_active && m_title.isEmpty() && m_statusText == QStringLiteral("无广播"))
        return;
    m_active = false;
    m_songId = 0;
    m_title.clear();
    m_artist.clear();
    m_cover.clear();
    m_positionSec = 0;
    m_durationSec = 0;
    m_playing = false;
    m_nextTitle.clear();
    m_nextArtist.clear();
    m_listeners = 0;
    m_publisher.clear();
    m_anchorPos = 0;
    m_anchorMs = 0;
    if (!keepStatus) setStatus(QStringLiteral("无广播"));
    emit stateChanged();
}

void VoiceHubModule::tick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // SSE 看门：120s 没有任何字节（半开连接）⇒ 主动断开触发重连。
    if (m_sse && now - m_sseLastByteMs > 120 * 1000) {
        m_lastError = QStringLiteral("SSE 120s 无数据，主动重连");
        abortSse();
        m_sseConnected = false;
        scheduleSseReconnect();
    }

    if (m_active) {
        if (m_playing && m_durationSec > 0) {
            const double p = m_anchorPos + double(now - m_anchorMs) / 1000.0;
            if (p >= m_durationSec) {
                // 本地外推越过总长：冻在结尾等下一帧快照（服务端会纠正或报停）。
                m_positionSec = m_durationSec;
                m_playing = false;
            } else {
                m_positionSec = p;
            }
        }
        // 过期自愈：在播却 95s（staleSec）没有新快照 ⇒ 播控人掉线/服务端挂了，
        // 当停播处理（不能卡在一首旧歌上让岛一直显示"正在播放"）。
        if (m_playing && m_lastSnapshotMs > 0
            && now - m_lastSnapshotMs > qint64(m_staleSec) * 1000) {
            clearBroadcast();
            return;
        }
        emit stateChanged();
    }
}

void VoiceHubModule::setStatus(const QString &s)
{
    if (s == m_statusText) return;
    m_statusText = s;
    emit stateChanged();
}

void VoiceHubModule::notifyNewBroadcast() const
{
    const ModuleAgentApi &a = api();
    if (!m_autoNotify || !a.islandShow) return;
    if (a.examActive && a.examActive()) return;   // 考试模式静默
    QString desc = m_artist.isEmpty() ? m_title : (m_title + QStringLiteral(" · ") + m_artist);
    if (!m_nextTitle.isEmpty())
        desc += QStringLiteral("\n接下来：") + m_nextTitle;
    if (!m_publisher.isEmpty())
        desc += QStringLiteral("\n播控：") + m_publisher;
    a.islandShow(QStringLiteral("VoiceHub 正在播放"), desc,
                 QStringLiteral("music"), /*form=Centered*/ 1, 6000);
}

// ── 动作 ──────────────────────────────────────────────────────────────────

bool VoiceHubModule::execAction(const QString &action, const QJsonObject &params, ModuleResult &out)
{
    Q_UNUSED(params);

    if (action == QStringLiteral("voicehub_broadcast")) {
        // 管理端信息展示：当前广播状态 JSON 快照（只读）。
        out.ok = true;
        out.result = QStringLiteral("done");
        out.data = stateJson();
        return true;
    }

    // 2026-10-22：song_request 从留桩转真实现（走 Open API，x-api-key）。
    // ⚠️ 点歌是**异步**的（QNAM 回调），execAction 必须同步返回，所以这里回
    //    "submitted"（已受理），真实结果靠 controlFinished → reportCloud 事后上报。
    //    不要用 QEventLoop 在这里等 —— 那会把 UI 线程卡住，岛直接僵死。
    if (action == QStringLiteral("song_request")) {
        const QString title = params.value(QStringLiteral("title")).toString();
        const QString artist = params.value(QStringLiteral("artist")).toString();
        const QString platform = params.value(QStringLiteral("musicPlatform"))
                                     .toString(QStringLiteral("netease"));
        const QString musicId = params.value(QStringLiteral("musicId")).toString();

        if (title.trimmed().isEmpty()) {
            out.ok = false;
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("缺少 title");
            return true;
        }
        if (!m_controlEnabled) {
            out.ok = false;
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("点歌通道未开启（VoiceHub 模块设置里打开控制开关）");
            return true;
        }
        if (m_baseUrl.isEmpty() || m_apiKey.isEmpty()) {
            out.ok = false;
            out.result = QStringLiteral("failed");
            out.error = m_baseUrl.isEmpty()
                ? QStringLiteral("未配置 VoiceHub 服务地址")
                : QStringLiteral("未配置 API Key（需要带 songs:request 权限）");
            return true;
        }

        auto once = std::make_shared<QMetaObject::Connection>();
        *once = connect(this, &VoiceHubModule::controlFinished, this,
                        [this, once](bool ok, const QString &msg) {
                            disconnect(*once);
                            if (api().reportCloud) {
                                api().reportCloud(
                                    QStringLiteral("voicehub_song_request_result"),
                                    QJsonObject{ { QStringLiteral("ok"), ok },
                                                 { QStringLiteral("message"), msg } });
                            }
                        });
        requestSong(title, artist, platform, musicId);

        out.ok = true;
        out.result = QStringLiteral("submitted");
        out.data = QJsonObject{ { QStringLiteral("async"), true } };
        return true;
    }

    if (action == QStringLiteral("lyrics")
        || action == QStringLiteral("broadcast_audio")) {
        // 这两个仍是留桩：lyrics 要写/读歌词库、broadcast_audio 要下发音频，
        // 与"点歌"不是同一条链路，口径未定之前不接。
        out.ok = false;
        out.result = QStringLiteral("not-open");
        out.error = QStringLiteral("该能力尚未接线（当前只开了只读展示与点歌）");
        return true;
    }

    return false;
}

QJsonObject VoiceHubModule::stateJson() const
{
    QJsonObject b;
    if (m_active) {
        b[QStringLiteral("songId")] = m_songId;
        b[QStringLiteral("title")] = m_title;
        b[QStringLiteral("artist")] = m_artist;
        b[QStringLiteral("durationSec")] = m_durationSec;
        b[QStringLiteral("positionSec")] = m_positionSec;
        b[QStringLiteral("isPlaying")] = m_playing;
        if (!m_nextTitle.isEmpty()) {
            b[QStringLiteral("nextTitle")] = m_nextTitle;
            b[QStringLiteral("nextArtist")] = m_nextArtist;
        }
        b[QStringLiteral("listeners")] = m_listeners;
        if (!m_publisher.isEmpty()) b[QStringLiteral("publisher")] = m_publisher;
    }
    QJsonObject j;
    j[QStringLiteral("active")] = m_active;
    j[QStringLiteral("broadcast")] = b;
    j[QStringLiteral("status")] = m_statusText;
    if (!m_lastError.isEmpty()) j[QStringLiteral("lastError")] = m_lastError;
    j[QStringLiteral("baseUrl")] = m_baseUrl;
    j[QStringLiteral("controlEnabled")] = m_controlEnabled;
    // 2026-10-22：把排期与点歌能力位也带出去，管理端才能判断"能不能点歌"。
    // ⚠️ 绝不带 apiKey（只报有没有）。
    j[QStringLiteral("canRequest")] = canRequest();
    j[QStringLiteral("hasApiKey")] = !m_apiKey.isEmpty();
    j[QStringLiteral("scheduleCount")] = m_schedule.size();
    QJsonArray sched;
    for (const QVariant &v : m_schedule) {
        const QVariantMap m = v.toMap();
        sched.append(QJsonObject{
            { QStringLiteral("sequence"), m.value(QStringLiteral("sequence")).toInt() },
            { QStringLiteral("played"), m.value(QStringLiteral("played")).toBool() },
            { QStringLiteral("isPlaying"), m.value(QStringLiteral("isPlaying")).toBool() },
            { QStringLiteral("title"), m.value(QStringLiteral("title")).toString() },
            { QStringLiteral("artist"), m.value(QStringLiteral("artist")).toString() },
        });
    }
    j[QStringLiteral("schedule")] = sched;
    return j;
}
