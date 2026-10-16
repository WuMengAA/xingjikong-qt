// 星集控 · 内置模块层（2026-10-16 · 用户点单「模块化插件」）
// ───────────────────────────────────────────────────────────────────────────
// 为什么要有**第二条**扩展通道（与 src/plugin 的进程外插件并列）：
//   进程外插件（独立进程 + 本地 WS）能拿到 agent 的能力，但拿不到 agent 的**本体**：
//     · 收不到 Windows 原生窗口消息（U盘插拔 WM_DEVICECHANGE）
//     · 改不了灵动岛本体的界面（岛是 agent 进程内的 QWidget + QQuickWidget）
//   而本轮三件事（U盘插件 / VoiceHub 联动 / 灵动岛多界面）**两件半**都在这条线外。
//   所以内置模块层 = 编译进 agent 的模块，与进程外插件共用同一份形状与门控：
//     · manifest 形状、权限词表、action 认领 —— 与 PluginManifest 同口径
//     · dispatch 顺序：**内置模块优先于进程外插件**（内置是官方能力，先认领）
//     · 启停落 QSettings 的 modules/<id>/enabled，插件市场页可列可切（不可卸载）
//
// ⚠️⚠️ AUTOMOC 三连坑（与 src/agent_ui.h 同款，已实测）：
//   1. 本头被 modulehost.cpp / 各模块 .cpp / main.cpp include ⇒ 会被**单独编译成 TU**
//      ⇒ 头里**不能引用 main.cpp 的任何符号**。agent 能力一律走 ModuleAgentApi 注入。
//   2. 头里不能 `#include "modulehost.moc"`（C1083）。
//   3. 本头 + 各模块头必须**显式列入 CMakeLists 的 add_executable**，否则不生成 moc
//      产物 ⇒ LNK2001。scripts/test-module-registry.mjs 盯着这条。

#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <functional>

// 模块声明（形状与进程外插件的 PluginManifest 对齐）
struct ModuleManifest {
    QString id;               // 全小写、全局唯一（与插件 id 同一命名空间）
    QString name;             // 展示名
    QString version;
    QString description;      // 插件市场页一行说明
    QStringList permissions;  // ⊆ PluginHost::kKnownPermissions（同一份词表）
    QStringList actions;      // 认领的远端 action（executeAction 里前置路由）
    QStringList events;       // 关心的本地事件名（forwardEvent 按名投喂）
    bool enabledByDefault = false;   // 首次运行默认开/关
};

// 模块执行结果（与 PluginResult 同形状，main.cpp 转成 ExecOut）
struct ModuleResult {
    bool ok = false;
    QString result;           // "done" | "failed"
    QString error;
    QJsonObject data;
};

// agent 暴露给模块的能力（main.cpp 注入；模块不许直接调 agent 内部符号）
// ⚠️ form 用 int：0=Capsule 1=Centered 2=Fullscreen（对齐 IslandOverlay::Form），
//    这样本头不必 include island.h（岛是本进程内的具体件，模块只认编号）。
struct ModuleAgentApi {
    // 岛：弹一条提示
    std::function<void(const QString &title, const QString &desc, const QString &icon,
                       int form, int durationMs)> islandShow;
    // 岛：弹一条**带按钮**的提示（点了哪枚回调 onAction(文案)，durationMs<=0 = 等点击）
    std::function<void(const QString &title, const QString &desc, const QString &icon, int form,
                       const QStringList &actions,
                       const std::function<void(const QString &)> &onAction,
                       int durationMs)> islandAsk;
    // 通知（走 notifyFromParams 同链路）
    std::function<void(const QJsonObject &params)> notify;
    // 全局设置（白名单：noiseEnabled/voiceEnabled/notifyEnabled/islandIdle/lightMode/examActive…）
    std::function<QJsonValue(const QString &key)> settingsGet;
    std::function<void(const QString &key, const QJsonValue &v)> settingsSet;
    // 模块私有设置（落 QSettings 的 modules/<moduleId>/<key>，模块之间互不干扰）
    std::function<QJsonValue(const QString &moduleId, const QString &key, const QJsonValue &dflt)> moduleGet;
    std::function<void(const QString &moduleId, const QString &key, const QJsonValue &v)> moduleSet;
    // 上报云端（与岛的 CloudReporter 同一条旁路；未接云端即静默）
    std::function<void(const QString &type, const QJsonObject &payload)> reportCloud;
    // 打开一个**文件夹**（U盘/收文件那类；只开目录，绝不打开/运行文件本身）
    std::function<void(const QString &path)> openFolder;
    // 考试模式是否在生效（模块据此静默 —— 上课/考试时不打扰）
    std::function<bool()> examActive;
};

class ModuleHost;

// ── 模块基类 ───────────────────────────────────────────────────────────────
// 模块只实现自己关心的钩子；没实现的钩子走默认空实现（不是纯虚）。
class AgentModule : public QObject
{
    Q_OBJECT
public:
    explicit AgentModule(QObject *parent = nullptr) : QObject(parent) {}

    /** 声明（id/权限/认领的 action…）。必须实现。 */
    virtual ModuleManifest manifest() const = 0;

    /** 被启用时调用一次（宿主已注入 api）。可以装监听器 / 起定时器。 */
    virtual void start() {}
    /** 被停用时调用（卸载监听器 / 停定时器 / 撤掉岛上的面）。 */
    virtual void stop() {}

    /** 远端指令：认领了就返回 true 并填 out；不认领返回 false 让别人接。 */
    virtual bool execAction(const QString &action, const QJsonObject &params, ModuleResult &out);
    /** 本地事件（manifest.events 里点过名的才会收到）。 */
    virtual void onEvent(const QString &name, const QJsonObject &payload);

protected:
    const ModuleAgentApi &api() const;   // 定义在 .cpp（从宿主取，避免头依赖）
    ModuleHost *host() const { return m_host; }

private:
    friend class ModuleHost;
    ModuleHost *m_host = nullptr;
};

// ── 模块宿主（进程内单例）─────────────────────────────────────────────────
class ModuleHost : public QObject
{
    Q_OBJECT
public:
    /** 单例（定义在 .cpp；main.cpp 只拿指针，不持有）。 */
    static ModuleHost *instance();

    void setAgentApi(const ModuleAgentApi &api) { m_api = api; }
    const ModuleAgentApi &api() const { return m_api; }

    /** 登记一个模块（宿主接管所有权）。重复 id 直接忽略并告警。 */
    void registerModule(AgentModule *m);
    /** 按 QSettings 的启停位启动所有模块。 */
    void startEnabled();
    void stopAll();

    /** 远端指令分派：认领的模块接住（同步），未认领返回 false。 */
    bool dispatch(const QString &action, const QJsonObject &params, ModuleResult &out);
    /** 本地事件投喂：只给 manifest.events 点过名的模块。 */
    void forwardEvent(const QString &name, const QJsonObject &payload);

    void setEnabled(const QString &id, bool on);
    bool isEnabled(const QString &id) const;
    bool isRunning(const QString &id) const;
    AgentModule *module(const QString &id) const;
    QStringList moduleIds() const;
    /** UI 用快照：id/name/version/description/enabled/running/permissions/actions */
    QJsonArray snapshot() const;

signals:
    void changed();     // 启停变化 → 插件市场页刷新

private:
    explicit ModuleHost(QObject *parent = nullptr);
    QString settingsKey(const QString &id) const;
    void applyEnabled(AgentModule *m, bool on);

    QList<AgentModule *> m_order;                 // 登记顺序（dispatch 顺序）
    QHash<QString, AgentModule *> m_byId;
    QHash<QString, bool> m_running;
    ModuleAgentApi m_api;
};
