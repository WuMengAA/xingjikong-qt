// 星集控 · 广播社成员模块（2026-10-23 · 用户点单「广播站功能移交管理端，独立身份」）
//
// 做什么：在管理端（被控端控制面板）里给广播社社团成员一个**独立身份登录通道**，
// 登录后只能读自己归属 station 的排期、并重排顺序。账号体系在云端自管
// （Stelarith-cloud/src/broadcast-club.js），本模块只负责：
//   · login(username,password)  → POST /api/broadcast-club/login，拿成员令牌
//   · refreshSchedules()        → GET  /api/broadcast-club/schedules（Bearer <token>）
//   · reorder(pairs)            → POST /api/broadcast-club/schedules/reorder（Bearer <token>）
// 令牌存 QSettings(modules/broadcastclub/token) + station；重启后免登录。
//
// ⚠️ 与 VoiceHub 联动模块的关系：两套独立身份/后端。
//   voicehub 直连 VoiceHub 的 Open API（x-api-key）；本模块走云端 broadcast-club
//   代理（成员令牌），密钥云端持有、不下发。成员**不碰** voicehub 控制能力。
//
// ⚠️ AUTOMOC：本头 + .cpp 必须显式列入 CMakeLists 的 add_executable（否则 LNK2001）。
//   头里不引用 main.cpp 任何符号（base URL 由 main.cpp 经 setBaseUrl 注入）。

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariantList>

class BroadcastClubModule : public QObject
{
    Q_OBJECT

    // ── 鉴权态（QML 读，登录/登出时变化）──
    Q_PROPERTY(QString      token     READ token     NOTIFY authChanged)
    Q_PROPERTY(QString      station   READ station   NOTIFY authChanged)
    Q_PROPERTY(bool         loggedIn  READ loggedIn  NOTIFY authChanged)

    // ── 排期（GET /api/broadcast-club/schedules 透传 VoiceHub 公开排期）──
    // 字段同 voicehub.schedule：id sequence played isPlaying broadcastPosition
    //   playDate title artist cover durationSeconds musicPlatform musicId songId
    Q_PROPERTY(QVariantList schedule      READ schedule      NOTIFY scheduleChanged)
    Q_PROPERTY(int          scheduleCount READ scheduleCount NOTIFY scheduleChanged)

    Q_PROPERTY(QString lastError         READ lastError         NOTIFY loginFinished)
    Q_PROPERTY(QString lastReorderError  READ lastReorderError  NOTIFY reorderFinished)

public:
    explicit BroadcastClubModule(QObject *parent = nullptr);

    QString      token() const { return m_token; }
    QString      station() const { return m_station; }
    bool         loggedIn() const { return m_loggedIn; }
    QVariantList schedule() const { return m_schedule; }
    int          scheduleCount() const { return m_schedule.size(); }
    QString      lastError() const { return m_lastError; }
    QString      lastReorderError() const { return m_lastReorderError; }

    /** 由 main.cpp 注入云端 base（siteBaseFromWsUrl(STE_QT_WS_URL)，ws→http）。 */
    Q_INVOKABLE void setBaseUrl(const QString &v);

    /** 成员登录：POST /api/broadcast-club/login。结果经 loginFinished(ok,msg)。 */
    Q_INVOKABLE void login(const QString &username, const QString &password);
    /** 登出：清本地令牌，回到登录态。 */
    Q_INVOKABLE void logout();

    /** 拉取排期（需已登录）。结果经 scheduleChanged。 */
    Q_INVOKABLE void refreshSchedules();
    /** 重排已发布排期顺序（写云端代理）。pairs 每项 {id,sequence}，按新视觉顺序 1..N。
     *  结果经 reorderFinished(ok,msg)。 */
    Q_INVOKABLE void reorder(const QVariantList &pairs);

signals:
    void authChanged();
    void scheduleChanged();
    void loginFinished(bool ok, const QString &message);
    void reorderFinished(bool ok, const QString &message);

private:
    void loadToken();
    void saveToken(const QString &token, const QString &station);
    void clearToken();
    void onLoginFinished(QNetworkReply *r);
    void onScheduleFinished(QNetworkReply *r);
    void onReorderFinished(QNetworkReply *r);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl;
    QString m_token;
    QString m_station;
    bool    m_loggedIn = false;
    QVariantList m_schedule;
    QString m_lastError;
    QString m_lastReorderError;
};
