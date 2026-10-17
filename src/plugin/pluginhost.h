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
//
// ── P0-0「第三方推岛治理」（2026-10-17 · docs/灵动岛-升级方案-2026-10-17.md §4.5）────
// 背景：island_show / notify 早在插件通道上，但当时没有任何约束 —— 插件想推多少推多少、
// 想推多长推多长、推来的东西与管理端通知长得一模一样（fromAdmin 恒 false、历史无来源）。
// 本补丁在**插件边界**上补齐四类约束（agent 自身的调用完全不受影响，风险面只在第三方）：
//   ① 长度：title ≤ 120 / desc(正文) ≤ 400 字符（按码点截断，不劈代理对）；
//   ② 频率：每插件令牌桶 5 条/秒、突发 10（超限静默丢弃 + 计数，不回错——回错等于给插件刷屏理由）；
//   ③ 来源上限：插件推的岛一律 ≤ 8 级语义，`island_show` 的 `emergency` 参数强制为 false
//      （9-10 级留给管理端/紧急广播，否则「狼来了」）；
//   ④ 归属：岛标题前缀 `[插件] `（取自 manifest.name），fromAdmin 仍为 false —— 历史可辨、
//      管理端通知排序不被污染；日志写入 manifest.id。
// 另有两个同批配套旋钮：`notify` 直通口是 `island_show` 的超集（能全屏/语音/确认），
// 按「第三方推送开关」一并闸住；单连接报文上限 64KB（防止一条超大 JSON 先吃满内存再被裁）。
#pragma once

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStringList>
#include <QPointer>
#include <QElapsedTimer>
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
    // ⚠️ P0-0：本函数指针**只被插件边界**调用，所以第三方约束全部放在 PluginHost::handleApi
    //    （长度/频率/来源前缀/emergency 钳制）；这里接收的已是治理后的参数。
    void (*islandShow)(const QString &title, const QString &desc, const QString &icon,
                       int form, bool fromAdmin, int durationMs) = nullptr;
    // 同上，但带**发生时刻**（noticeAtMs）。插件推的通知用「插件调用那一刻」，
    // 不用「agent 处理那一刻」——两者之间只差一次签名/分发，但在忙时可能差几百 ms。
    // 为 nullptr 时回落到 islandShow（main.cpp 永远会注入，回落只是防漏配）。
    void (*islandShowAt)(const QString &title, const QString &desc, const QString &icon,
                         int form, bool fromAdmin, int durationMs, qint64 noticeAtMs) = nullptr;
    void (*notify)(const QJsonObject &params) = nullptr;
    QJsonValue (*settingsGet)(const QString &key) = nullptr;
    void (*settingsSet)(const QString &key, const QJsonValue &v) = nullptr;
    void (*subscribeEvents)(const QString &pluginId, const QStringList &events) = nullptr;
    // 第三方推岛开关（设置页「允许第三方推送灵动岛」，main.cpp → AgentSettings）。
    // 为 nullptr 时按**开**处理：漏配宁可放行也不静默关死功能（fail-open 在这里是可接受的，
    // 因为漏配只可能来自开发者本机，且 ①-④ 四道约束仍然生效）。
    bool (*senderPriorityLimit)() = nullptr;   // 当前第三方可用的最高优先级上限（9-10 永远不给）
    // P0-0 沙箱验证：返回岛的状态快照（JSON 文本，见 IslandOverlay::historySnapshotJson）。
    // 只为让插件在**自己收到的回包**里看到"这条到底进没进岛"——同机并存生产实例时，
    // 截图分不清是哪个进程的岛（本轮实测过）。为 nullptr 时回包不带 islandSnapshot 字段。
    QString (*islandSnapshot)() = nullptr;
};

// ── P0-0：插件推岛的治理常量与审计计数（2026-10-17）──────────────────────────
// 为什么是常量而不是配置项：这些是**安全下限**，不该被某个班的老师调松；
// 需要放宽时改代码 + 改门禁（Stelarith-shared-qml/scripts/test-plugin-island-gate.mjs 盯两边同值）。
struct PluginIslandGate {
    static constexpr int kMaxTitleChars = 120;   // BMP 汉字 1 码元 = 1 字；生僻字 2 码元，
                                                 //   所以截断按「码点」循环（见 clipCodepoints）
    static constexpr int kMaxDescChars  = 400;
    static constexpr int kMaxMessageBytes = 64 * 1024; // 单条 WS 报文上限
    static constexpr int kRatePerSec    = 5;     // 每插件令牌桶：每秒补充 5 个
    static constexpr int kRateBurst     = 10;    // 突发上限（桶容量）
    static constexpr int kMaxPriority   = 8;     // 插件可用最高优先级；9-10 只给管理端
    static constexpr int kMinPriority   = 1;

    // 审计计数（只增不减；日志读，用于回答「为什么插件推的东西没出现」）
    int allowed = 0;          // 放行
    int deniedSwitch = 0;     // 第三方推岛开关关闭
    int deniedRate = 0;       // 令牌桶空
    int deniedSize = 0;       // 报文超长
    int trimmed = 0;          // 标题/正文被截断（放行了，但改过内容）
    int clampPriority = 0;    // 优先级/紧急档被钳制
    int rejectedSwitch = 0;   // notify 直通口被插件推岛开关拒绝
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
    // P0-0：第三方推岛治理计数快照（{"allowed":…,"deniedSwitch":…,…}），日志/排障用
    QJsonObject islandGateStats() const;

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
        // ── P0-0 第三方推岛令牌桶（每插件一个，随连接生灭）──
        double tokens = PluginIslandGate::kRateBurst;   // 初始满桶（首个安装即推的插件不被误杀）
        qint64 tokensAtMs = 0;                          // 上次补充时刻；0 = 尚未初始化
    };

    // P0-0 治理入口（handleApi 只调这三个；逻辑集中在实现里，便于门禁逐条比对）
    void handleIslandShow(Conn *c, const QJsonObject &args, QJsonObject &r);
    void handleNotifyAt(Conn *c, const QJsonObject &args, QJsonObject &r);
    bool islandGateOpen();                    // 第三方推岛开关（api.senderPriorityLimit）
    bool takeToken(Conn *c);                  // 令牌桶（按墙钟补充，不依赖定时器）
    static QString clipCodepoints(const QString &s, int maxCodepoints);   // 按码点截断
    static int clampPluginPriority(const QJsonObject &args);              // 1..8，缺省 8

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
    mutable PluginIslandGate m_gateStats;       // P0-0 审计计数（const 快照接口也要能读）
    QString m_pluginDir;
    QString m_agentVersion;
    bool m_started = false;
};
