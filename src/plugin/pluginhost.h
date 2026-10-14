// 星集控 · 插件宿主（被控端 P2）
// ───────────────────────────────────────────────────────────────────────────
// 设计要点（2026-10-14 用户拍板）：
//   · 第三方/社区插件＝**进程外**执行：插件是独立进程，经本地 WebSocket 与 agent 通信
//     （JSON-RPC 风格）。C++ / C# / Python 通吃，agent 不被第三方原生码拖崩，天然沙箱边界。
//   · 本地 WS 仅绑 127.0.0.1，端口默认 26789（env STE_PLUGIN_PORT 可改）。
//   · 权限模型：manifest 声明 permissions，agent 在「api 调用 / 事件订阅」处强制校验。
//   · 命令扩展点：插件 register 的 action 进入 m_handlers；executeAction 在查内建指令前
//     先查插件表，命中则经本地 WS 同步调用（嵌套 QEventLoop + 5s 超时）回填 PluginResult。
//
// 进程外意味着插件无法直调 agent 内部符号——它只能通过下方 PluginAgentApi 暴露的能力。
// 该结构体由 main.cpp 注入（islandShow / notify / settings / 事件订阅），本文件不反向依赖 main.cpp。
#pragma once

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStringList>
#include <QPointer>
#include <QEventLoop>
#include <QTimer>

// 插件调用 agent 能力后回填的结果（与 main.cpp 的 ExecOut 解耦，避免循环依赖）
struct PluginResult {
    QString result;
    QString error;
    QJsonObject data;
};

// manifest 解析结果（由插件进程在 hello 时上报）
struct PluginManifest {
    QString id;
    QString name;
    QString version;
    QString author;
    QString description;
    QString minAgentVersion;
    QStringList permissions;   // 子集须 ∈ kKnownPermissions
    QString runtime;           // native | python | dotnet（首片实现 native/python）
    QString entry;             // 相对插件目录的可执行文件或脚本
    QStringList actions;       // 本插件处理的远端 action
    static bool parse(const QJsonObject &j, PluginManifest &m, QString &err);
};

// agent 暴露给插件的能力（main.cpp 注入具体实现）
struct PluginAgentApi {
    // island_show：form 0=Capsule 1=Centered 2=Fullscreen
    void (*islandShow)(const QString &title, const QString &desc, const QString &icon,
                       int form, bool fromAdmin, int durationMs) = nullptr;
    void (*notify)(const QJsonObject &params) = nullptr;
    QJsonValue (*settingsGet)(const QString &key) = nullptr;
    void (*settingsSet)(const QString &key, const QJsonValue &v) = nullptr;
    void (*subscribeEvents)(const QString &pluginId, const QStringList &events) = nullptr;
};

class PluginHost : public QObject {
    Q_OBJECT
public:
    explicit PluginHost(QObject *parent = nullptr);
    ~PluginHost() override;

    bool start(quint16 port = 0);              // port 0 → 默认 STE_PLUGIN_PORT
    bool isStarted() const { return m_started; }
    void setAgentApi(const PluginAgentApi &api) { m_api = api; }
    void setAgentVersion(const QString &v) { m_agentVersion = v; }
    QString agentVersion() const { return m_agentVersion; }
    QString pluginDir() const { return m_pluginDir; }

    // 插件是否认领了该 action；认领则同步填 out 并返回 true
    bool dispatch(const QString &action, const QJsonObject &params, PluginResult &out);

    // 进程生命周期
    void loadEnabled();                        // 扫描 plugins/，启动已启用插件
    void installAndLaunch(const QString &id); // 新安装后启动
    void enable(const QString &id, bool on);   // 启/停（写 QSettings + 起/杀进程）
    void uninstall(const QString &id);         // 删除插件目录
    QStringList installedPlugins() const;      // 返回插件 id 列表
    QJsonArray snapshot() const;               // UI 用：id/name/version/author/enabled/running/permissions

    // agent 侧向插件转发事件（如 noise_sample）；订阅了同名事件的插件才会收到
    void forwardEvent(const QString &name, const QJsonObject &payload);

    static const QStringList kKnownPermissions;

signals:
    void storeChanged();                       // UI 刷新用

private slots:
    void onNewConnection();
    void onSocketDisconnected();
    void onTextMessage(const QString &msg);

private:
    struct Conn {
        QPointer<QWebSocket> sock;
        PluginManifest manifest;
        QStringList granted;                   // 实际授予（manifest ∩ known）
        QStringList actions;
        QStringList subscribed;                // 已订阅事件名
        bool welcomed = false;
        quint32 nextReq = 1;
        // dispatch 同步调用挂起态
        QString pendingReqId;
        bool pendingActive = false;
        QJsonObject pendingResult;
        QEventLoop *pendingLoop = nullptr;
    };

    void handleHello(Conn *c, const QJsonObject &j);
    void handleRegister(Conn *c, const QJsonObject &j);
    void handleApi(Conn *c, const QJsonObject &j);
    void handleResult(Conn *c, const QJsonObject &j);
    bool launch(const QString &id);            // 起进程，返回是否成功
    void kill(const QString &id);
    QString manifestPath(const QString &id) const;

    QWebSocketServer *m_srv = nullptr;
    QHash<QWebSocket *, Conn *> m_conns;        // socket → Conn
    QHash<QString, QWebSocket *> m_handlers;    // action → plugin socket
    QHash<QString, Conn *> m_byId;             // pluginId → Conn
    QHash<QString, QProcess *> m_procs;         // pluginId → 进程
    QHash<QString, QStringList> m_eventSubs;    // event → pluginIds
    PluginAgentApi m_api;
    QString m_pluginDir;
    QString m_agentVersion;
    bool m_started = false;
};
