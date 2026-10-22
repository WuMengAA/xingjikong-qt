// 星集控 · 内置模块：VoiceHub 联动（2026-10-17 · 用户点单「voicehub联动插件」）
//
// 做什么：把 VoiceHub 服务端的「权威广播状态」**只读**地搬进星集控被控端 ——
//   · 灵动岛展开态一行「正在播放 / 进度 / 下一首 / 收听人数」；
//   · 控制面板·设置页一节配置（服务地址 / 轮询 / 开播提醒 / 控制开关）；
//   · 管理端经动作 `voicehub_broadcast` 拿 JSON 快照（信息展示）。
//   控制类能力（点歌 / 歌词 / 广播下发）**留桩默认关**（见 execAction）。
//
// 数据契约（2026-10-17 实测线上 voicehub.245959623.xyz + 参考实现
// Stelarith-voicehub-component/Services/BroadcastStateClient.cs 同口径）：
//   · SSE 主通道：GET {base}/api/music/websocket（**匿名**，text/event-stream）。
//       建连即下发 connection_established + broadcast_state；之后状态变化推送。
//       broadcast_state 的 data = BroadcastState 对象或 null（null=停播）；
//       nextUp / listeners / baselineReleased 挂在**同一个 data 对象**上
//       （老服务端没有这些字段 ⇒ 缺省 null/0/false，天然向后兼容）。
//       旧版 HarmonyOS 通道 music_state_update / song_change / playlist_update
//       一律忽略（本服务端已不发，防的是中间代理混发）。
//   · 轮询兜底：GET {base}/api/music/broadcast（**匿名**）——
//       {broadcast, nextUp, listeners, baselineReleased, serverTime}。
//   · 写/播控接口（POST queue / authority / listeners）本模块**绝不调用**（只读红线）。
//   · Open API（/api/open/*，x-api-key + songs:request 等权限）只留给控制桩。
//
// 状态机（与参考实现同口径，教室机网络差也要稳）：
//   · SSE 断 ⇒ 指数退避重连（1s→2s→4s…封顶 30s），期间轮询兜底；
//   · 进度**本地 tick**（锚点 + 墙上钟外推，每秒一次），绝不每秒发请求；
//   · 过期自愈：isPlaying 且超过 staleSec（默认 95s）没有新快照 ⇒ 当停播
//     （播控人掉线/服务端挂了的场景，不能卡在一首旧歌上）；
//   · 任何网络错 / 404 / 403 ⇒ 当「没有广播」，**不污染**其它状态、不进错误态。
//
// ⚠️ AUTOMOC：本头被 voicehubmodule.cpp 与 main.cpp include ⇒ 会单独编译成 TU，
//    头里不引用 main.cpp 任何符号；QNetworkAccessManager 等 Qt 类型可以出现在头里
//    （与 usbmodule.h 的 windows.h 纪律不同——那边是防 windows.h 宏污染）。
//    本头 + .cpp 必须**显式列入 CMakeLists 的 add_executable**（否则 LNK2001，
//    scripts/test-module-registry.mjs 盯着）。

#pragma once

#include "modulehost.h"

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

class VoiceHubModule : public AgentModule
{
    Q_OBJECT

    // ── QML 展示属性（岛展开态行 + 设置页；快照+信号模式，QML 只读）──
    Q_PROPERTY(bool    active       READ active       NOTIFY stateChanged)
    Q_PROPERTY(QString title        READ title        NOTIFY stateChanged)
    Q_PROPERTY(QString artist       READ artist       NOTIFY stateChanged)
    Q_PROPERTY(QString cover        READ cover        NOTIFY stateChanged)
    Q_PROPERTY(double  positionSec  READ positionSec  NOTIFY stateChanged)
    Q_PROPERTY(double  durationSec  READ durationSec  NOTIFY stateChanged)
    Q_PROPERTY(bool    playing      READ playing      NOTIFY stateChanged)
    Q_PROPERTY(QString nextTitle    READ nextTitle    NOTIFY stateChanged)
    Q_PROPERTY(QString nextArtist   READ nextArtist   NOTIFY stateChanged)
    Q_PROPERTY(int     listeners    READ listeners    NOTIFY stateChanged)
    Q_PROPERTY(QString publisher    READ publisher    NOTIFY stateChanged)
    Q_PROPERTY(QString statusText   READ statusText   NOTIFY stateChanged)
    Q_PROPERTY(QString lastError    READ lastError    NOTIFY stateChanged)

    // ── 配置属性（设置页可写；写落 QSettings 的 modules/voicehub/<key>）──
    Q_PROPERTY(QString baseUrl       READ baseUrl       WRITE setBaseUrl       NOTIFY configChanged)
    Q_PROPERTY(int     pollSec       READ pollSec       WRITE setPollSec       NOTIFY configChanged)
    Q_PROPERTY(int     staleSec      READ staleSec      WRITE setStaleSec      NOTIFY configChanged)
    Q_PROPERTY(bool    autoNotify    READ autoNotify    WRITE setAutoNotify    NOTIFY configChanged)
    Q_PROPERTY(bool    controlEnabled READ controlEnabled WRITE setControlEnabled NOTIFY configChanged)

    // ── 排期列表（2026-10-22 加）────────────────────────────────
    // 数据源：GET {base}/api/songs/public（匿名可读）。
    // 每项是 QVariantMap：{ id, sequence, played, isPlaying, broadcastPosition,
    //                      title, artist, cover, durationSeconds, musicPlatform, musicId }
    // 这就是「排期中心」的真源——排期表一直在 VoiceHub 里，不在星集控云端。
    // ⚠️ 匿名访问时 requester 会被服务端遮罩成 "***"，不要把账号当学生姓名用。
    Q_PROPERTY(QVariantList schedule      READ schedule      NOTIFY scheduleChanged)
    Q_PROPERTY(int          scheduleCount READ scheduleCount NOTIFY scheduleChanged)

    // ── 点歌控制（2026-10-22 加）────────────────────────────────
    // 走 VoiceHub 的 Open API（POST {base}/api/open/songs/request，头 x-api-key）。
    // 需要服务端后台建一把带 songs:request 权限的 Key。
    // ⚠️ apiKey 以明文存 QSettings(modules/voicehub/apiKey)：教室机本地可接受，
    //    但不写入任何日志、不进上报、不打印到 statusText。
    Q_PROPERTY(QString apiKey   READ apiKey   WRITE setApiKey   NOTIFY configChanged)
    Q_PROPERTY(bool    canRequest READ canRequest NOTIFY configChanged)
    Q_PROPERTY(QString lastControlError READ lastControlError NOTIFY controlFinished)

public:
    explicit VoiceHubModule(QObject *parent = nullptr);

    ModuleManifest manifest() const override;
    void start() override;
    void stop() override;
    bool execAction(const QString &action, const QJsonObject &params, ModuleResult &out) override;

    // ── QML 读──
    bool    active() const { return m_active; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString cover() const { return m_cover; }
    double  positionSec() const { return m_positionSec; }
    double  durationSec() const { return m_durationSec; }
    bool    playing() const { return m_playing; }
    QString nextTitle() const { return m_nextTitle; }
    QString nextArtist() const { return m_nextArtist; }
    int     listeners() const { return m_listeners; }
    QString publisher() const { return m_publisher; }
    QString statusText() const { return m_statusText; }
    QString lastError() const { return m_lastError; }

    // ── QML 写（配置）──
    QString baseUrl() const { return m_baseUrl; }
    void    setBaseUrl(const QString &v);
    int     pollSec() const { return m_pollSec; }
    void    setPollSec(int v);
    int     staleSec() const { return m_staleSec; }
    void    setStaleSec(int v);
    bool    autoNotify() const { return m_autoNotify; }
    void    setAutoNotify(bool v);
    bool    controlEnabled() const { return m_controlEnabled; }
    void    setControlEnabled(bool v);

    // ── 排期 / 控制（2026-10-22）──
    QVariantList schedule() const { return m_schedule; }
    int          scheduleCount() const { return m_schedule.size(); }
    QString      apiKey() const { return m_apiKey; }
    void         setApiKey(const QString &v);
    bool         canRequest() const { return !m_baseUrl.isEmpty() && !m_apiKey.isEmpty(); }
    QString      lastControlError() const { return m_lastControlError; }

    /** 立刻重拉一次排期（进排期面板时调，别等定时器）。 */
    Q_INVOKABLE void refreshSchedule();

    /**
     * 点歌（写 VoiceHub）。入参就是 Open API 的 ALLOWED_FIELDS 子集。
     * 异步：结果通过 controlFinished(bool, QString) 回来，不阻塞 UI。
     * musicId 为空也可以——服务端会按歌名歌手处理，但有 id 时更准。
     */
    Q_INVOKABLE void requestSong(const QString &title,
                                 const QString &artist,
                                 const QString &musicPlatform = QStringLiteral("netease"),
                                 const QString &musicId = QString());

    /** 当前状态 JSON（管理端 voicehub_broadcast 动作与探针共用）。 */
    QJsonObject stateJson() const;

signals:
    void stateChanged();    // 广播状态/进度变化 → 岛行与设置页刷新
    void configChanged();   // 配置变化
    void scheduleChanged(); // 排期列表变化
    void controlFinished(bool ok, const QString &message);   // 点歌结果

private:
    // ── 配置（QSettings modules/voicehub/*）──
    void loadConfig();
    QJsonValue cfgGet(const QString &key, const QJsonValue &dflt) const;
    void cfgSet(const QString &key, const QJsonValue &v);
    void applyConfigChange();           // 地址/轮询变了 ⇒ 重启客户端

    // ── 传输层 ──
    void startClient();
    void stopClient();
    void connectSse();                  // 建/重建 SSE 长连接
    void abortSse();
    void pollOnce();                    // 轮询兜底发一次
    void onSseReadyRead();
    void onSseFinished();
    void onPollFinished(QNetworkReply *r);
    void handleSseFrame(const QString &frame);
    void scheduleSseReconnect();

    // ── 排期 / 控制（2026-10-22）──
    void fetchSchedule();                          // GET /api/songs/public
    void onScheduleFinished(QNetworkReply *r);
    void onRequestFinished(QNetworkReply *r);

    // ── 状态层 ──
    // has=false 或 b 为空对象 ⇒ 「停播」（清状态）。
    // extra：nextUp/listeners/baselineReleased 的宿主对象（SSE 的 data 对象或轮询的消息根）。
    void applyBroadcast(const QJsonObject &b, bool has, const QJsonObject &extra);
    void clearBroadcast();
    void tick();                        // 1s：进度外推 + 过期自愈 + SSE 看门
    void setStatus(const QString &s);
    void notifyNewBroadcast() const;    // 开播提醒（autoNotify 且非考试模式）

    QNetworkAccessManager *m_nam = nullptr;

    // 配置
    QString m_baseUrl;
    int     m_pollSec  = 30;
    int     m_staleSec = 95;
    bool    m_autoNotify = true;
    bool    m_controlEnabled = false;

    // SSE
    QPointer<QNetworkReply> m_sse;
    QByteArray m_sseBuf;
    int  m_sseBackoffSec = 1;
    bool m_sseConnected = false;
    qint64 m_sseLastByteMs = 0;

    // 轮询 / tick
    QTimer m_pollTimer;
    QTimer m_tickTimer;

    // 广播状态
    bool    m_active = false;
    qint64  m_songId = 0;
    QString m_title;
    QString m_artist;
    QString m_cover;
    double  m_positionSec = 0;
    double  m_durationSec = 0;
    bool    m_playing = false;
    QString m_nextTitle;
    QString m_nextArtist;
    int     m_listeners = 0;
    QString m_publisher;
    QString m_statusText = QStringLiteral("未启动");
    QString m_lastError;

    // 进度锚点（本地 tick 外推用；不每秒发请求）
    double   m_anchorPos = 0;
    qint64   m_anchorMs = 0;
    qint64   m_lastSnapshotMs = 0;

    // ── 排期 / 控制（2026-10-22）──
    QTimer       m_scheduleTimer;
    QVariantList m_schedule;
    QString      m_apiKey;            // 明文，见属性注释
    QString      m_lastControlError;
};
