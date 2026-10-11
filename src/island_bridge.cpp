#include "island_bridge.h"
#include "island.h"

#include <QDebug>

IslandBridge::IslandBridge(IslandOverlay *host, QObject *parent)
    : QObject(parent), m_host(host)
{
    // ⚠️ 不在这里 sync()：构造时 QML 引擎还没起来，发信号没人听。
    //    IslandOverlay 建完桥会自己 sync() 一次。
}

// ── 读侧：全部从 host 现读，不缓存 ───────────────────────────────
// 缓存会造成"两个真源"（C++ 成员 + 桥字段），改一处忘一处就出鬼。
// 岛的状态字段就十来个，每次读的代价远低于维护两份状态的代价。

QString IslandBridge::title() const    { return m_host ? m_host->m_title : QString(); }
QString IslandBridge::desc() const     { return m_host ? m_host->m_desc : QString(); }
QString IslandBridge::icon() const     { return m_host ? m_host->m_icon : QString(); }
QString IslandBridge::severity() const { return m_host ? m_host->m_severity : QString(); }
bool IslandBridge::light() const       { return m_host ? m_host->m_light : false; }
bool IslandBridge::idle() const        { return m_host ? m_host->m_idle : true; }
bool IslandBridge::expanded() const    { return m_host ? m_host->m_expanded : false; }
bool IslandBridge::interactive() const { return m_host ? m_host->m_interactive : false; }

bool IslandBridge::canOpen() const
{
    return m_host && !m_host->m_openPath.isEmpty();
}

QString IslandBridge::form() const
{
    if (!m_host) return QStringLiteral("capsule");
    switch (m_host->m_form) {
    case IslandOverlay::Form::Centered:   return QStringLiteral("centered");
    case IslandOverlay::Form::Fullscreen: return QStringLiteral("fullscreen");
    case IslandOverlay::Form::Capsule:
    default:                              return QStringLiteral("capsule");
    }
}

QVariantList IslandBridge::actions() const
{
    QVariantList out;
    if (!m_host) return out;
    for (int i = 0; i < m_host->m_actions.size(); ++i) {
        QVariantMap m;
        m.insert(QStringLiteral("label"), m_host->m_actions.at(i));
        // 第一枚是主按钮（实心反白），与 paintBigForm() 里 `primary = (i==0)` 同口径
        m.insert(QStringLiteral("role"),
                 i == 0 ? QStringLiteral("primary") : QStringLiteral("secondary"));
        out.append(m);
    }
    return out;
}

QVariantList IslandBridge::states() const
{
    // 被控端当前是「一条内容」模型：showIsland 新内容顶掉旧内容，没有并行多状态。
    // 为了 QML 侧 StateList 能直接用，这里把单条内容包成一个元素的列表。
    //
    // ⚠️ 不在这里凭空造「N 项」：管理端那套聚合是 7 类状态源（指令/文件/告警…）同时在线，
    //    被控端没有那个数据源。硬编一个假多状态 = 界面在骗人。
    //    真要多状态，得先让 showIsland 支持并存（另一个立项），不是在这里补。
    QVariantList out;
    if (!m_host) return out;
    if (m_host->m_idle) return out;                       // 空闲态没有状态行
    if (m_host->m_title.isEmpty()) return out;            // 没内容就不给列表

    QVariantMap m;
    // k：图标名当类别标识，QML 的 iconNameFor() 据此选图标
    m.insert(QStringLiteral("k"), m_host->m_icon);
    m.insert(QStringLiteral("t"), m_host->m_title);
    m.insert(QStringLiteral("d"), m_host->m_desc);
    m.insert(QStringLiteral("p"), -1);                    // -1 = 无进度条
    out.append(m);
    return out;
}

void IslandBridge::sync()
{
    emit changed();
}

// ── QML → C++ ────────────────────────────────────────────────────

void IslandBridge::fireAction(const QString &label)
{
    if (!m_host) return;
    // 与 mouseReleaseEvent() 里点中按钮那段**同一条路径**：
    // 取回调 → 清按钮 → 取消交互 → dismiss → 最后调回调。
    // ⚠️ 顺序不能反：先触发回调的话，回调里可能又 showIsland（比如"需要帮助"要弹出说明），
    //    那时 m_actions 还没清，新内容会带着旧按钮上屏。
    const QString reply = label.isEmpty() ? m_host->m_actions.value(0) : label;
    auto cb = m_host->m_onAction;
    m_host->m_actions.clear();
    m_host->m_onAction = nullptr;
    m_host->setInteractive(false);
    m_host->dismiss();
    sync();
    if (cb) cb(reply);
}

void IslandBridge::requestExpand(bool on)
{
    if (!m_host) return;
    m_host->setExpanded(on);
}
