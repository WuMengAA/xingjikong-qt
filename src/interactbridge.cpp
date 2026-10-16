// 星集控管理端 · 互动审批桥（实现）—— 头文件里有完整边界说明，这里只写"怎么做的"。
//
// 网络：QNetworkAccessManager 走云端 HTTP（与 ViewerBackend 的 admin API 同一条路、
// 同一枚 Bearer 令牌）。⚠️ 三轮请求用 m_pendingReplies 计数收口：任一轮失败都记
// m_error，但**只有全到齐**才把 state 落定 —— 否则面板会在半截数据上闪一下。

#include "interactbridge.h"
#include "viewerbackend.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

InteractBridge::InteractBridge(ViewerBackend *backend, QObject *parent)
    : QObject(parent), m_backend(backend)
{
    m_nam = new QNetworkAccessManager(this);
}

// 管理端主题 → 岛深浅。岛里 light=true 是浅色，管理端 dark 与之相反。
void InteractBridge::setDark(bool dark) { setLight(!dark); }

void InteractBridge::setFilterState(const QString &s)
{
    if (m_filterState == s) return;
    m_filterState = s;
    emit dataChanged();
    reload();                       // 筛选变了 → 按新 state 重拉（云端支持 ?state=）
}

// ── 拉三份数据 ──────────────────────────────────────────────────
void InteractBridge::reload()
{
    if (!m_backend) {
        m_state = QStringLiteral("error");
        m_error = QStringLiteral("内部错误：没有后端引用");
        emit dataChanged();
        return;
    }
    const QString base  = m_backend->httpBaseUrl();
    const QString token = m_backend->viewerToken();
    if (base.isEmpty()) {
        m_state = QStringLiteral("error");
        m_error = QStringLiteral("云端地址未知（管理端还没连上）");
        emit dataChanged();
        return;
    }

    m_state = QStringLiteral("loading");
    m_error.clear();
    m_pendingReplies = 3;
    emit dataChanged();

    auto bearer = [&token](QNetworkRequest &req) {
        req.setRawHeader("Content-Type", "application/json");
        // ⚠️ setRawHeader 收 QByteArray，不收 QString（"Bearer " + QString 会 C2664）
        if (!token.isEmpty())
            req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());
    };

    // ① 点歌待审
    {
        QUrl u(base + QStringLiteral("/api/interact/song-requests"));
        if (!m_filterState.isEmpty()) {
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("state"), m_filterState);
            u.setQuery(q);
        }
        QNetworkRequest req(u);
        bearer(req);
        auto *r = m_nam->get(req);
        connect(r, &QNetworkReply::finished, this, [this, r] {
            r->deleteLater();
            const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code == 200) {
                m_songs = QJsonDocument::fromJson(r->readAll())
                              .object().value(QStringLiteral("items")).toArray().toVariantList();
            } else if (m_error.isEmpty()) {
                m_error = QStringLiteral("点歌队列拉取失败（HTTP %1）").arg(code);
            }
            finishReload();
        });
    }
    // ② 祝福待审
    {
        QUrl u(base + QStringLiteral("/api/interact/blessings"));
        if (!m_filterState.isEmpty()) {
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("state"), m_filterState);
            u.setQuery(q);
        }
        QNetworkRequest req(u);
        bearer(req);
        auto *r = m_nam->get(req);
        connect(r, &QNetworkReply::finished, this, [this, r] {
            r->deleteLater();
            const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code == 200) {
                m_blessings = QJsonDocument::fromJson(r->readAll())
                                  .object().value(QStringLiteral("items")).toArray().toVariantList();
            } else if (m_error.isEmpty()) {
                m_error = QStringLiteral("祝福队列拉取失败（HTTP %1）").arg(code);
            }
            finishReload();
        });
    }
    // ③ 反馈汇总
    {
        QNetworkRequest req(QUrl(base + QStringLiteral("/api/interact/feedback/summary")));
        bearer(req);
        auto *r = m_nam->get(req);
        connect(r, &QNetworkReply::finished, this, [this, r] {
            r->deleteLater();
            const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code == 200) {
                m_feedback = QJsonDocument::fromJson(r->readAll()).object().toVariantMap();
            } else if (m_error.isEmpty()) {
                m_error = QStringLiteral("反馈汇总拉取失败（HTTP %1）").arg(code);
            }
            finishReload();
        });
    }
}

void InteractBridge::finishReload()
{
    if (--m_pendingReplies > 0) return;     // 还有请求在路上
    m_state = m_error.isEmpty() ? QStringLiteral("ok") : QStringLiteral("error");
    emit dataChanged();
}

// ── 审批动作 ────────────────────────────────────────────────────
void InteractBridge::postHandle(const QString &kind, int id, const QString &action)
{
    if (!m_backend) return;
    const QString base  = m_backend->httpBaseUrl();
    const QString token = m_backend->viewerToken();
    if (base.isEmpty()) return;

    QNetworkRequest req(QUrl(base + QStringLiteral("/api/interact/handle")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty())
        req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());

    QJsonObject body;
    body.insert(QStringLiteral("kind"), kind);
    body.insert(QStringLiteral("id"), id);
    body.insert(QStringLiteral("action"), action);
    auto *r = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, kind, id, action] {
        r->deleteLater();
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        const bool ok = (code == 200 && o.value(QStringLiteral("ok")).toBool());
        applyHandleResult(kind, id, action, ok,
                          o.value(QStringLiteral("error")).toString(
                              QStringLiteral("HTTP %1").arg(code)));
    });
}

void InteractBridge::applyHandleResult(const QString &kind, int id, const QString &action,
                                       bool ok, const QString &msg)
{
    if (!ok) {
        m_error = QStringLiteral("审批失败：%1").arg(msg);
        emit dataChanged();
        return;
    }
    // 就地改本地条目状态，**不整表重拉** —— 审批后面板停在原滚动位置，体验更稳。
    QVariantList *list = (kind == QLatin1String("song")) ? &m_songs : &m_blessings;
    for (int i = 0; i < list->size(); ++i) {
        QVariantMap it = list->at(i).toMap();
        if (it.value(QStringLiteral("id")).toInt() == id) {
            it.insert(QStringLiteral("state"),
                      action == QLatin1String("approve") ? QStringLiteral("approved")
                                                         : QStringLiteral("rejected"));
            (*list)[i] = it;
            break;
        }
    }
    // 面板正筛"待审"⇒ 已处理的那条立刻从列表消失（这才是老师期待的反应）。
    if (m_filterState == QLatin1String("pending")) {
        QVariantList kept;
        for (const QVariant &v : *list) {
            if (v.toMap().value(QStringLiteral("state")).toString() == QLatin1String("pending"))
                kept.append(v);
        }
        *list = kept;
    }
    m_error.clear();
    emit dataChanged();
}

void InteractBridge::handleSong(int id, const QString &action) { postHandle(QStringLiteral("song"), id, action); }
void InteractBridge::handleBlessing(int id, const QString &action) { postHandle(QStringLiteral("blessing"), id, action); }

// ── 岛预览 ──────────────────────────────────────────────────────
// 把面板选中的条目渲染成"学生在教室机上会看到的样子"。用户点单原话是
// 「灵动岛使用被控端一模一样的」⇒ 走的确实是同一份 IslandRoot.qml，
// 这里只是把桥数据摆成对应场景。
void InteractBridge::preview(const QString &kind, const QString &title, const QString &sub)
{
    m_previewKind = kind;
    if (kind == QLatin1String("song")) {
        // 点歌通过 ⇒ 学生端岛显示"正在播放 <歌名>"（组件7 的 playing 态）。
        m_title = title.isEmpty() ? QStringLiteral("（未命名）") : title;
        m_desc.clear();
        m_icon = QStringLiteral("music");
        m_mediaActive = true;
        m_mediaPresenting = true;
        m_mediaTitle = m_title;
        m_mediaTotalMs = 269000;          // 演示时长 4:29（与探针同口径，可复现）
        m_mediaElapsedMs = 83000;         // 01:23
        m_mediaNextTitle.clear();
        m_mediaNextCover.clear();
        m_mediaPhase = QStringLiteral("playing");
    } else if (kind == QLatin1String("blessing")) {
        // 祝福通过 ⇒ 学生端走全屏弹幕；岛上是一句通知（弹幕层不在岛里）。
        m_title = title.isEmpty() ? QStringLiteral("（空祝福）") : title;
        m_desc = sub;
        m_icon = QStringLiteral("mic");
        m_mediaActive = m_mediaPresenting = false;
        m_mediaTitle.clear(); m_mediaPhase.clear();
        m_mediaTotalMs = m_mediaElapsedMs = 0;
        m_mediaNextTitle.clear();
    } else if (kind == QLatin1String("feedback")) {
        m_title = title.isEmpty() ? QStringLiteral("课堂反馈") : title;
        m_desc = sub;
        m_icon = QStringLiteral("heart");
        m_mediaActive = m_mediaPresenting = false;
        m_mediaTitle.clear(); m_mediaPhase.clear();
        m_mediaTotalMs = m_mediaElapsedMs = 0;
        m_mediaNextTitle.clear();
    } else {
        clearPreview();
        return;
    }
    m_expanded = true;      // 面板里看的就是展开态（收起态只有一条，预览没意义）
    emit changed();
}

void InteractBridge::clearPreview()
{
    m_previewKind.clear();
    m_title.clear();
    m_desc.clear();
    m_icon.clear();
    m_mediaActive = m_mediaPresenting = false;
    m_mediaTitle.clear();
    m_mediaPhase.clear();
    m_mediaTotalMs = m_mediaElapsedMs = 0;
    m_mediaNextTitle.clear();
    m_mediaNextCover.clear();
    emit changed();
}
