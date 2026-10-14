// 星集控 · 插件 SDK（官方，C++ 进程外插件用）
// ───────────────────────────────────────────────────────────────────────────
// 这是「官方开源插件 SDK」的一部分：第三方作者写 C++ 插件时 #include 本文件，
// 链接 Qt6::WebSockets，即可与被控端本地插件服务通信（无需任何星集控内部符号）。
//
// 通信模型：插件是独立进程，连 ws://127.0.0.1:26789（端口 env STE_PLUGIN_PORT），
// 先发 hello(manifest)，收到 welcome 后 register(actions)，之后：
//   · agent 收到云端下发的某 action → 经 WS 把 {type:action,...} 推给本插件
//     → 插件用 setActionHandler 注册的回调处理 → 回 {type:result,...}
//   · 插件可主动调 agent 能力（islandShow / notify / settings / 订阅事件），受 manifest 权限约束
//
// 示例（见 plugins/samples/echo/）：
//   PluginClient cli;
//   cli.setManifest(R"({"id":"echo","name":"Echo","version":"1.0.0",
//     "author":"you","permissions":["island"],"runtime":"native",
//     "entry":"echo.exe","actions":["echo"]})"_json);
//   cli.setActionHandler("echo", [](const QJsonObject& p){
//       return QJsonObject{{"result","done"},{"data",{{"echo",p.value("text")}}}};
//   });
//   cli.connectToHost();
//   return qApp->exec();
#pragma once

#include <QObject>
#include <QWebSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QHash>
#include <QTimer>
#include <QEventLoop>
#include <functional>

namespace StelarithPlugin {

constexpr const char *kDefaultUrl = "ws://127.0.0.1:26789";

// agent 暴露的能力（manifest 需声明对应 permission 才被放行）
enum Permission { Island, Notify, Settings, EventsNoise, EventsSchedule, CmdExec };

class PluginClient : public QObject {
    Q_OBJECT
public:
    using ActionHandler = std::function<QJsonObject(const QJsonObject &)>; // 返回 {result,error,data}
    using EventHandler = std::function<void(const QJsonObject &)>;

    explicit PluginClient(QObject *parent = nullptr) : QObject(parent) {
        m_ws = new QWebSocket(QString(), QWebSocket::VersionLatest, this);
        connect(m_ws, &QWebSocket::connected, this, &PluginClient::onConnected);
        connect(m_ws, &QWebSocket::textMessageReceived, this, &PluginClient::onTextMessage);
        connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
                [this](QAbstractSocket::SocketError e) {
                    qWarning("[plugin-sdk] WS 错误：%d", (int)e);
                    if (m_autoReconnect) QTimer::singleShot(2000, this, [this] { m_ws->open(m_url); });
                });
    }

    void setManifest(const QJsonObject &m) { m_manifest = m; }
    void setActionHandler(const QString &action, ActionHandler h) { m_actions[action] = std::move(h); }
    void setEventHandler(const QString &event, EventHandler h) { m_events[event] = std::move(h); }
    void setAutoReconnect(bool v) { m_autoReconnect = v; }

    bool connectToHost(const QString &url = QString(kDefaultUrl)) {
        m_url = url;
        m_ws->open(url);
        return true;
    }

    // ── agent 能力（受权限约束，agent 侧校验）──
    void islandShow(const QString &title, const QString &desc, const QString &icon,
                    int form = 1, int durationMs = 5000) {
        callApi(QStringLiteral("island_show"),
                QJsonObject{{QStringLiteral("title"), title},
                            {QStringLiteral("desc"), desc},
                            {QStringLiteral("icon"), icon},
                            {QStringLiteral("form"), form},
                            {QStringLiteral("durationMs"), durationMs}});
    }
    void notify(const QJsonObject &params) { callApi(QStringLiteral("notify"), params); }
    QJsonValue settingsGet(const QString &key) { return callApiSync(QStringLiteral("settings_get"),
                                                     QJsonObject{{QStringLiteral("key"), key}})
                                                     .value(QStringLiteral("value")); }
    void settingsSet(const QString &key, const QJsonValue &v) {
        callApi(QStringLiteral("settings_set"),
                QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("value"), v}});
    }
    void subscribeEvents(const QStringList &events) {
        callApi(QStringLiteral("events_subscribe"),
                QJsonObject{{QStringLiteral("events"), QJsonArray::fromStringList(events)}});
    }

signals:
    void welcomed(bool ok, const QString &err);

private slots:
    void onConnected() {
        QJsonObject h; h.insert(QStringLiteral("type"), QStringLiteral("hello"));
        h.insert(QStringLiteral("manifest"), m_manifest);
        m_ws->sendTextMessage(QJsonDocument(h).toJson(QJsonDocument::Compact));
    }
    void onTextMessage(const QString &msg) {
        const QJsonObject j = QJsonDocument::fromJson(msg.toUtf8()).object();
        const QString type = j.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("welcome")) {
            const bool ok = j.value(QStringLiteral("ok")).toBool();
            emit welcomed(ok, j.value(QStringLiteral("error")).toString());
            if (ok) {
                QJsonObject r; r.insert(QStringLiteral("type"), QStringLiteral("register"));
                r.insert(QStringLiteral("actions"), QJsonArray::fromStringList(m_actions.keys()));
                m_ws->sendTextMessage(QJsonDocument(r).toJson(QJsonDocument::Compact));
            }
        } else if (type == QStringLiteral("action")) {
            const QString id = j.value(QStringLiteral("id")).toString();
            const QString action = j.value(QStringLiteral("action")).toString();
            QJsonObject res; res.insert(QStringLiteral("type"), QStringLiteral("result"));
            res.insert(QStringLiteral("id"), id);
            auto it = m_actions.find(action);
            if (it != m_actions.end()) {
                const QJsonObject r = it.value()(j.value(QStringLiteral("params")).toObject());
                res.insert(QStringLiteral("result"), r.value(QStringLiteral("result")));
                res.insert(QStringLiteral("error"), r.value(QStringLiteral("error")));
                res.insert(QStringLiteral("data"), r.value(QStringLiteral("data")));
            } else {
                res.insert(QStringLiteral("result"), QStringLiteral("failed"));
                res.insert(QStringLiteral("error"), QStringLiteral("未注册 action：") + action);
            }
            m_ws->sendTextMessage(QJsonDocument(res).toJson(QJsonDocument::Compact));
        } else if (type == QStringLiteral("event")) {
            const QString name = j.value(QStringLiteral("name")).toString();
            auto it = m_events.find(name);
            if (it != m_events.end()) it.value()(j.value(QStringLiteral("payload")).toObject());
        }
        // api_result：异步调用回填（callApiSync 用）
        else if (type == QStringLiteral("api_result")) {
            const QString id = j.value(QStringLiteral("id")).toString();
            if (m_pending.contains(id)) {
                auto &pe = m_pending[id];
                pe.result = j; pe.ok = true;
                if (pe.loop) pe.loop->quit();
            }
        }
    }

private:
    struct PendingApi { QJsonObject result; bool ok = false; QEventLoop *loop = nullptr; };

    void callApi(const QString &call, const QJsonObject &args) {
        QJsonObject m; m.insert(QStringLiteral("type"), QStringLiteral("api"));
        m.insert(QStringLiteral("id"), QString::number(++m_reqSeq));
        m.insert(QStringLiteral("call"), call); m.insert(QStringLiteral("args"), args);
        m_ws->sendTextMessage(QJsonDocument(m).toJson(QJsonDocument::Compact));
    }
    QJsonObject callApiSync(const QString &call, const QJsonObject &args) {
        const QString id = QString::number(++m_reqSeq);
        QJsonObject m; m.insert(QStringLiteral("type"), QStringLiteral("api"));
        m.insert(QStringLiteral("id"), id);
        m.insert(QStringLiteral("call"), call); m.insert(QStringLiteral("args"), args);
        PendingApi pe; QEventLoop loop; pe.loop = &loop; m_pending[id] = pe;
        m_ws->sendTextMessage(QJsonDocument(m).toJson(QJsonDocument::Compact));
        QTimer t; t.setSingleShot(true);
        connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit); t.start(5000);
        loop.exec();
        QJsonObject r = m_pending.take(id).result;
        return r;
    }

    QWebSocket *m_ws = nullptr;
    QJsonObject m_manifest;
    QHash<QString, ActionHandler> m_actions;
    QHash<QString, EventHandler> m_events;
    QHash<QString, PendingApi> m_pending;
    QString m_url = QString(kDefaultUrl);
    quint32 m_reqSeq = 0;
    bool m_autoReconnect = true;
};

} // namespace StelarithPlugin
