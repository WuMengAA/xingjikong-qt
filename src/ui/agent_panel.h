#pragma once
#include <QString>
#include <QJsonObject>

// QML 控制面板（U5-A3）数据接线 —— main 拆分 #9（2026-10-30）
// 从 main.cpp 迁出：buildSelfCheckItems / buildPluginList / pluginRegistryUrl /
// s_storeEntries / agentUiFill（AgentUi 的 friend）/ agentUiWire。
//
// ⚠️ agent_ui.h 是 AUTOMOC 单独编译的 Q_OBJECT 头（不能引用 main.cpp 的任何符号）。
//    而 agentUiFill/agentUiWire 依赖的 main.cpp 匿名域函数（内部链接，跨不了 TU）
//    由 main() 经本模块的 g_panelDeps 注入 —— 函数名/参数不变、语义等价（方案 B）。
//
// 状态全局（g_configured/g_registered/g_ws/… 与 kAppVersion）仍驻 main（文件作用域
// 外部链接），本模块按既有 extern 模式复用（同 g_uid / agentEnvPath 模式）。

class AgentUi;

// TrayState：原 main.cpp 匿名域枚举，提升文件作用域（同 g_uid/agentEnvPath 模式），
// 供 agent_panel 与 main.cpp 的 updateTray 共用**同一类型**（否则两个 enum 各是
// 一个类型，跨 TU 传参类型不匹配）。
enum class TrayState { Connected, Disconnected, Unconfigured };

// agentUiFill/agentUiWire 依赖的 main.cpp 匿名域函数（内部链接）经此上下文访问。
// main() 负责填充（同一 TU 才能取匿名域函数地址）；agent_panel.cpp 只经 g_panelDeps
// 调用，不改函数名/参数、语义等价。函数指针保持非空由 main() 的填充点保证。
struct AgentPanelDeps {
    int (*compareVersion)(const QString &a, const QString &b);
    void (*otaSelfCheck)();
    bool (*saveAgentEnv)(const QString &url, const QString &token,
                         const QString &uid, const QString &name);
    void (*openConfigDialog)();
    void (*updateTray)(TrayState st, const QString &detail);
    void (*connectNow)();
    void (*startSelfUpdate)(const QString &id, const QJsonObject &params);
};

// 定义于 main.cpp（文件作用域外部链接）；main() 启动即填充，AgentShell 懒建前必已就绪。
extern AgentPanelDeps *g_panelDeps;

// 由 agent_panel.cpp 提供。AgentShell 构造时经 agent_ui.h 调用 agentUiWire(&m_ui)。
void agentUiFill(AgentUi *ui);   // AgentUi 的 friend（直接改快照成员，不触发 switchChanged）
void agentUiWire(AgentUi *ui);
