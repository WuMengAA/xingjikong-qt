// 星集控 · 插件宿主实现（被控端 P2）— 见 pluginhost.h 头部说明
#include "pluginhost.h"

#include <QHostAddress>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QCoreApplication>
#include <QJsonValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

const QStringList PluginHost::kKnownPermissions = {
    QStringLiteral("island"),          // 灵动岛展示（island_show）
    QStringLiteral("notify"),          // 通知（notify）
    QStringLiteral("settings"),        // 读/写本地设置
    QStringLiteral("events:noise"),    // 订阅噪音采样
    QStringLiteral("events:schedule"), // 订阅课表变更
    QStringLiteral("cmd:exec"),        // 经 agent 执行本地命令（高危，预留）
};

// ── manifest 解析 ──────────────────────────────────────────────────────────
bool PluginManifest::parse(const QJsonObject &j, PluginManifest &m, QString &err) {
    m.id = j.value(QStringLiteral("id")).toString().trimmed();
    m.name = j.value(QStringLiteral("name")).toString().trimmed();
    m.version = j.value(QStringLiteral("version")).toString().trimmed();
    m.author = j.value(QStringLiteral("author")).toString().trimmed();
    m.description = j.value(QStringLiteral("description")).toString().trimmed();
    m.minAgentVersion = j.value(QStringLiteral("minAgentVersion")).toString().trimmed();
    m.runtime = j.value(QStringLiteral("runtime")).toString().trimmed();
    m.entry = j.value(QStringLiteral("entry")).toString().trimmed();
    const QJsonValue perms = j.value(QStringLiteral("permissions"));
    if (perms.isArray()) {
        for (const QJsonValue &v : perms.toArray()) m.permissions << v.toString();
    }
    const QJsonValue acts = j.value(QStringLiteral("actions"));
    if (acts.isArray()) {
        for (const QJsonValue &v : acts.toArray()) m.actions << v.toString();
    }
    if (m.id.isEmpty()) { err = QStringLiteral("manifest.id 为空"); return false; }
    if (m.version.isEmpty()) { err = QStringLiteral("manifest.version 为空"); return false; }
    if (m.runtime.isEmpty()) m.runtime = QStringLiteral("native");
    if (m.minAgentVersion.isEmpty()) m.minAgentVersion = QStringLiteral("0.0.0");
    return true;
}

// 极简版本比较："x.y.z" 数值比较；a>b 返回 >0
static int cmpVer(const QString &a, const QString &b) {
    auto parts = [](const QString &s) {
        QStringList r;
        for (const QString &p : s.split(QLatin1Char('.'))) r << p;
        while (r.size() < 3) r << QStringLiteral("0");
        return r;
    };
    const QStringList A = parts(a), B = parts(b);
    for (int i = 0; i < 3; ++i) {
        bool ok1 = false, ok2 = false;
        const int x = A[i].toInt(&ok1), y = B[i].toInt(&ok2);
        const int xx = ok1 ? x : 0, yy = ok2 ? y : 0;
        if (xx != yy) return xx - yy;
    }
    return 0;
}

// ── 宿主生命周期 ────────────────────────────────────────────────────────────
PluginHost::PluginHost(QObject *parent) : QObject(parent) {
    QString envDir = qEnvironmentVariable("STE_PLUGIN_DIR");
    m_pluginDir = envDir.isEmpty()
        ? (QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"))
        : envDir;
    QDir().mkpath(m_pluginDir);
}

PluginHost::~PluginHost() {
    for (auto *p : m_procs) { if (p) p->kill(); }
    if (m_srv) m_srv->close();
}

bool PluginHost::start(quint16 port) {
    if (port == 0) {
        bool ok = false;
        const int envPort = qEnvironmentVariableIntValue("STE_PLUGIN_PORT", &ok);
        port = ok && envPort > 0 ? quint16(envPort) : quint16(26789);
    }
    m_srv = new QWebSocketServer(QStringLiteral("StelarithPlugin"),
                                 QWebSocketServer::NonSecureMode, this);
    if (!m_srv->listen(QHostAddress::LocalHost, port)) {
        qWarning("[plugin] 本地插件服务启动失败：%s", qPrintable(m_srv->errorString()));
        delete m_srv; m_srv = nullptr;
        return false;
    }
    connect(m_srv, &QWebSocketServer::newConnection, this, &PluginHost::onNewConnection);
    m_started = true;
    qInfo("[plugin] 本地插件服务已起：ws://127.0.0.1:%d（目录 %s）", port, qPrintable(m_pluginDir));
    return true;
}

void PluginHost::onNewConnection() {
    while (m_srv->hasPendingConnections()) {
        QWebSocket *ws = m_srv->nextPendingConnection();
        auto *c = new Conn();
        c->sock = ws;
        m_conns.insert(ws, c);
        connect(ws, &QWebSocket::textMessageReceived, this, &PluginHost::onTextMessage);
        connect(ws, &QWebSocket::disconnected, this, &PluginHost::onSocketDisconnected);
    }
}

void PluginHost::onSocketDisconnected() {
    auto *ws = qobject_cast<QWebSocket *>(sender());
    if (!ws) return;
    Conn *c = m_conns.value(ws, nullptr);
    if (!c) return;
    // 清理该插件注册的所有 action
    for (auto it = m_handlers.begin(); it != m_handlers.end();) {
        if (it.value() == ws) it = m_handlers.erase(it); else ++it;
    }
    if (!c->manifest.id.isEmpty()) {
        m_byId.remove(c->manifest.id);
        for (auto &ids : m_eventSubs) ids.removeAll(c->manifest.id);
        if (m_procs.contains(c->manifest.id)) {
            QProcess *p = m_procs.take(c->manifest.id);
            if (p) p->deleteLater();
        }
        qInfo("[plugin] 插件断开：%s", qPrintable(c->manifest.id));
        emit storeChanged();
    }
    m_conns.remove(ws);
    delete c;
    ws->deleteLater();
}

void PluginHost::onTextMessage(const QString &msg) {
    QWebSocket *ws = qobject_cast<QWebSocket *>(sender());
    if (!ws) return;
    Conn *c = m_conns.value(ws, nullptr);
    if (!c) return;
    const QJsonObject j = QJsonDocument::fromJson(msg.toUtf8()).object();
    const QString type = j.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("hello")) handleHello(c, j);
    else if (type == QStringLiteral("register")) handleRegister(c, j);
    else if (type == QStringLiteral("api")) handleApi(c, j);
    else if (type == QStringLiteral("result")) handleResult(c, j);
    // 未知类型忽略
}

void PluginHost::handleHello(Conn *c, const QJsonObject &j) {
    PluginManifest m;
    QString err;
    if (!PluginManifest::parse(j.value(QStringLiteral("manifest")).toObject(), m, err)) {
        QJsonObject w; w.insert(QStringLiteral("type"), QStringLiteral("welcome"));
        w.insert(QStringLiteral("ok"), false); w.insert(QStringLiteral("error"), err);
        c->sock->sendTextMessage(QJsonDocument(w).toJson(QJsonDocument::Compact));
        c->sock->close(); return;
    }
    if (cmpVer(m.minAgentVersion, m_agentVersion) > 0) {
        QJsonObject w; w.insert(QStringLiteral("type"), QStringLiteral("welcome"));
        w.insert(QStringLiteral("ok"), false);
        w.insert(QStringLiteral("error"),
                 QStringLiteral("需要 agent >= %1，当前 %2").arg(m.minAgentVersion, m_agentVersion));
        c->sock->sendTextMessage(QJsonDocument(w).toJson(QJsonDocument::Compact));
        c->sock->close(); return;
    }
    // 权限取交集（未知权限直接不授予，不拒绝连接）
    QStringList granted;
    for (const QString &p : m.permissions)
        if (kKnownPermissions.contains(p)) granted << p;
    c->manifest = m;
    c->granted = granted;
    c->welcomed = true;
    m_byId.insert(m.id, c);
    QJsonObject w; w.insert(QStringLiteral("type"), QStringLiteral("welcome"));
    w.insert(QStringLiteral("ok"), true);
    w.insert(QStringLiteral("agentVersion"), m_agentVersion);
    w.insert(QStringLiteral("granted"), QJsonArray::fromStringList(granted));
    c->sock->sendTextMessage(QJsonDocument(w).toJson(QJsonDocument::Compact));
    qInfo("[plugin] 插件接入：%s v%s（作者 %s，授予 %s）",
          qPrintable(m.id), qPrintable(m.version), qPrintable(m.author),
          qPrintable(granted.join(QLatin1Char(','))));
    emit storeChanged();
}

void PluginHost::handleRegister(Conn *c, const QJsonObject &j) {
    if (!c->welcomed) { c->sock->close(); return; }
    const QStringList acts = j.value(QStringLiteral("actions")).toVariant().toStringList();
    for (const QString &a : acts) {
        if (a.isEmpty()) continue;
        m_handlers.insert(a, c->sock);
        if (!c->actions.contains(a)) c->actions << a;
    }
    QJsonObject r; r.insert(QStringLiteral("type"), QStringLiteral("registered"));
    r.insert(QStringLiteral("ok"), true);
    r.insert(QStringLiteral("actions"), QJsonArray::fromStringList(acts));
    c->sock->sendTextMessage(QJsonDocument(r).toJson(QJsonDocument::Compact));
}

void PluginHost::handleApi(Conn *c, const QJsonObject &j) {
    const QString call = j.value(QStringLiteral("call")).toString();
    const QJsonObject args = j.value(QStringLiteral("args")).toObject();
    QJsonObject r; r.insert(QStringLiteral("type"), QStringLiteral("api_result"));
    r.insert(QStringLiteral("id"), j.value(QStringLiteral("id")));
    const auto need = [&](const QString &perm) { return c->granted.contains(perm); };
    const auto deny = [&](const QString &p) {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), QStringLiteral("权限不足：%1").arg(p));
    };
    if (call == QStringLiteral("island_show")) {
        if (!need(QStringLiteral("island")) && !need(QStringLiteral("notify"))) deny(QStringLiteral("island"));
        else {
            if (m_api.islandShow)
                m_api.islandShow(args.value(QStringLiteral("title")).toString(),
                                 args.value(QStringLiteral("desc")).toString(),
                                 args.value(QStringLiteral("icon")).toString(),
                                 args.value(QStringLiteral("form")).toInt(1), false,
                                 args.value(QStringLiteral("durationMs")).toInt(5000));
            r.insert(QStringLiteral("ok"), true);
        }
    } else if (call == QStringLiteral("notify")) {
        if (!need(QStringLiteral("notify")) && !need(QStringLiteral("island"))) deny(QStringLiteral("notify"));
        else { if (m_api.notify) m_api.notify(args); r.insert(QStringLiteral("ok"), true); }
    } else if (call == QStringLiteral("settings_get")) {
        if (!need(QStringLiteral("settings"))) deny(QStringLiteral("settings"));
        else {
            QJsonValue v = m_api.settingsGet ? m_api.settingsGet(args.value(QStringLiteral("key")).toString())
                                             : QJsonValue();
            r.insert(QStringLiteral("ok"), true);
            r.insert(QStringLiteral("value"), v);
        }
    } else if (call == QStringLiteral("settings_set")) {
        if (!need(QStringLiteral("settings"))) deny(QStringLiteral("settings"));
        else {
            if (m_api.settingsSet) m_api.settingsSet(args.value(QStringLiteral("key")).toString(),
                                                     args.value(QStringLiteral("value")));
            r.insert(QStringLiteral("ok"), true);
        }
    } else if (call == QStringLiteral("events_subscribe")) {
        const QStringList evs = args.value(QStringLiteral("events")).toVariant().toStringList();
        bool okPerm = true;
        for (const QString &e : evs) {
            if (e == QStringLiteral("noise_sample") && !need(QStringLiteral("events:noise"))) okPerm = false;
            if (e == QStringLiteral("schedule_change") && !need(QStringLiteral("events:schedule"))) okPerm = false;
        }
        if (!okPerm) deny(QStringLiteral("events"));
        else {
            for (const QString &e : evs) {
                m_eventSubs[e].removeAll(c->manifest.id);
                m_eventSubs[e] << c->manifest.id;
                if (!c->subscribed.contains(e)) c->subscribed << e;
            }
            if (m_api.subscribeEvents) m_api.subscribeEvents(c->manifest.id, evs);
            r.insert(QStringLiteral("ok"), true);
        }
    } else {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), QStringLiteral("未知 api：%1").arg(call));
    }
    if (c->sock) c->sock->sendTextMessage(QJsonDocument(r).toJson(QJsonDocument::Compact));
}

void PluginHost::handleResult(Conn *c, const QJsonObject &j) {
    if (c->pendingActive && j.value(QStringLiteral("id")).toString() == c->pendingReqId) {
        c->pendingResult = j;
        c->pendingActive = false;
        if (c->pendingLoop) c->pendingLoop->quit();
    }
}

bool PluginHost::dispatch(const QString &action, const QJsonObject &params, PluginResult &out) {
    QWebSocket *sock = m_handlers.value(action);
    if (!sock || !m_conns.contains(sock)) return false;
    Conn *c = m_conns.value(sock);
    if (!c || !c->welcomed) return false;
    const QString reqId = QString::number(c->nextReq++);
    QJsonObject msg; msg.insert(QStringLiteral("type"), QStringLiteral("action"));
    msg.insert(QStringLiteral("id"), reqId);
    msg.insert(QStringLiteral("action"), action);
    msg.insert(QStringLiteral("params"), params);
    c->pendingReqId = reqId;
    c->pendingActive = true;
    c->pendingResult = QJsonObject();
    QEventLoop loop;
    c->pendingLoop = &loop;
    sock->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    QTimer t; t.setSingleShot(true);
    connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    t.start(5000);
    loop.exec();
    c->pendingLoop = nullptr;
    const bool got = !c->pendingActive;   // 超时则仍 active
    c->pendingActive = false;
    if (!got) {
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("插件 %1 调用超时（5s）").arg(c->manifest.id);
        return true;
    }
    out.result = c->pendingResult.value(QStringLiteral("result")).toString(QStringLiteral("failed"));
    out.error = c->pendingResult.value(QStringLiteral("error")).toString();
    out.data = c->pendingResult.value(QStringLiteral("data")).toObject();
    return true;
}

void PluginHost::forwardEvent(const QString &name, const QJsonObject &payload) {
    const QStringList ids = m_eventSubs.value(name);
    if (ids.isEmpty()) return;
    QJsonObject e; e.insert(QStringLiteral("type"), QStringLiteral("event"));
    e.insert(QStringLiteral("name"), name); e.insert(QStringLiteral("payload"), payload);
    const QByteArray buf = QJsonDocument(e).toJson(QJsonDocument::Compact);
    for (const QString &id : ids) {
        Conn *c = m_byId.value(id);
        if (c && c->sock) c->sock->sendTextMessage(buf);
    }
}

// ── 进程生命周期 ────────────────────────────────────────────────────────────
QString PluginHost::manifestPath(const QString &id) const {
    return m_pluginDir + QLatin1Char('/') + id + QStringLiteral("/manifest.json");
}

QStringList PluginHost::installedPlugins() const {
    QStringList out;
    QDir dir(m_pluginDir);
    for (const QString &sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFile::exists(m_pluginDir + QLatin1Char('/') + sub + QStringLiteral("/manifest.json")))
            out << sub;
    }
    return out;
}

bool PluginHost::launch(const QString &id) {
    const QString dir = m_pluginDir + QLatin1Char('/') + id;
    QFile f(manifestPath(id));
    if (!f.open(QIODevice::ReadOnly)) { qWarning("[plugin] 读 manifest 失败：%s", qPrintable(id)); return false; }
    QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    PluginManifest man; QString err;
    if (!PluginManifest::parse(m, man, err)) { qWarning("[plugin] manifest 非法 %s：%s", qPrintable(id), qPrintable(err)); return false; }
    QString program, entry = dir + QLatin1Char('/') + man.entry;
    QStringList args;
    if (man.runtime == QStringLiteral("python")) { program = QStringLiteral("python"); args << entry; }
    else if (man.runtime == QStringLiteral("dotnet")) { program = QStringLiteral("dotnet"); args << entry; }
    else { program = entry; }   // native
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(dir);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, id](int, QProcess::ExitStatus) {
                if (m_procs.contains(id)) m_procs.take(id)->deleteLater();
                emit storeChanged();
            });
    proc->start(program, args);
    if (!proc->waitForStarted(5000)) {
        qWarning("[plugin] 启动失败：%s（%s）", qPrintable(id), qPrintable(proc->errorString()));
        proc->deleteLater();
        return false;
    }
    m_procs.insert(id, proc);
    qInfo("[plugin] 已启动：%s（pid %lld）", qPrintable(id), (long long)proc->processId());
    return true;
}

void PluginHost::kill(const QString &id) {
    QProcess *p = m_procs.value(id);
    if (p) { p->kill(); p->waitForFinished(3000); m_procs.remove(id); }
}

void PluginHost::loadEnabled() {
    QSettings s;
    for (const QString &id : installedPlugins()) {
        if (s.value(QStringLiteral("plugin/") + id + QStringLiteral("/enabled"), false).toBool())
            launch(id);
    }
}

void PluginHost::installAndLaunch(const QString &id) {
    QSettings s;
    s.setValue(QStringLiteral("plugin/") + id + QStringLiteral("/enabled"), true);
    launch(id);
    emit storeChanged();
}

void PluginHost::enable(const QString &id, bool on) {
    QSettings s;
    s.setValue(QStringLiteral("plugin/") + id + QStringLiteral("/enabled"), on);
    if (on) launch(id); else kill(id);
    emit storeChanged();
}

void PluginHost::uninstall(const QString &id) {
    kill(id);
    QDir(m_pluginDir + QLatin1Char('/') + id).removeRecursively();
    emit storeChanged();
}

QJsonArray PluginHost::snapshot() const {
    QSettings s;
    QJsonArray arr;
    for (const QString &id : installedPlugins()) {
        QFile f(manifestPath(id));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        QJsonObject o;
        o.insert(QStringLiteral("id"), id);
        o.insert(QStringLiteral("name"), m.value(QStringLiteral("name")));
        o.insert(QStringLiteral("version"), m.value(QStringLiteral("version")));
        o.insert(QStringLiteral("author"), m.value(QStringLiteral("author")));
        o.insert(QStringLiteral("description"), m.value(QStringLiteral("description")));
        o.insert(QStringLiteral("permissions"), m.value(QStringLiteral("permissions")));
        o.insert(QStringLiteral("enabled"), s.value(QStringLiteral("plugin/") + id + QStringLiteral("/enabled"), false).toBool());
        QProcess *p = m_procs.value(id);
        o.insert(QStringLiteral("running"), p && p->state() == QProcess::Running);
        arr.append(o);
    }
    return arr;
}
