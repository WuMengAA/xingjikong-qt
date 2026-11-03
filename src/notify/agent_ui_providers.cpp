// 星集控 · AgentUi 的设置页方法体（2026-11-01 · 顶替 ClassIsland 第 2 步）
//
// ── 为什么单独一个 .cpp ────────────────────────────────────────────────
// 与 island_bridge_settings.cpp 同款理由：agent_ui.h 已 722 行（红线 300，属既有超线债），
// 再往里塞实现只会让"改一处要滚半天"。C++ 允许同一个类的方法分散在多个 TU 定义，
// 声明仍留在 agent_ui.h（Q_PROPERTY / Q_INVOKABLE 必须在类里），这里只放实现。
//
// ── 职责边界 ──────────────────────────────────────────────────────────
// 只做「把注册表的结果值原样交给 QML」+「把 QML 的改动转给注册表」。
//   不算优先级、不拼文案、不判开关 —— 那些在 notify::NotifyProviders 里。
//   每次改动后统一发 notifyProvidersChanged()，让设置页用 C++ 的值重画
//   （不做 QML 侧乐观更新，避免"勾了但没生效"的假勾）。

#include "../agent_ui.h"
#include "../policy/quit_guard.h"   // 退出守卫（2026-11-03）：setQuitPassword 的落盘口
#include "provider_registry.h"

QVariantList AgentUi::notifyProviders() const
{
    return notify::NotifyProviders::instance().settingsModel();
}

void AgentUi::setNotifyProviderEnabled(const QString &id, bool on)
{
    notify::NotifyProviders::instance().setEnabled(id, on);
    emit notifyProvidersChanged();
}

void AgentUi::setNotifyChannelEnabled(const QString &id, const QString &channelId, bool on)
{
    notify::NotifyProviders::instance().setChannelEnabled(id, channelId, on);
    emit notifyProvidersChanged();
}

void AgentUi::moveNotifyProvider(const QString &id, int delta)
{
    if (delta < 0)      notify::NotifyProviders::instance().moveUp(id);
    else if (delta > 0) notify::NotifyProviders::instance().moveDown(id);
    emit notifyProvidersChanged();
}

void AgentUi::resetNotifyProviders()
{
    notify::NotifyProviders::instance().resetToDefaults();
    emit notifyProvidersChanged();
}

// ── 设置页分类清单（2026-11-01 从 agent_ui.h 内联实现搬来）──────────────
// 搬迁动机：本批给 AgentUi 加了「通知提供者」5 个成员，头文件涨到 722 行触发
// file-size 门禁（登记 707 → 现在 722）。按「方法体分散到 TU」的既有范式搬出 64 行，
// 净减、不是上调台账；行为逐字不变。
// 设置页分类清单（2026-10-28 模块三：界面按参考重做）。
//   · 图标名必须是 resources/icons 里**真实存在**的文件名 —— 写不存在的名字会静默空白
//     （项目踩过一次："grid"/"users" 在 viewer 那边根本不存在）。
//   · active 由 C++ 定：QML 直接绑 `active`，不写 `key === current` 这种比较。
//   · 分类与内容的一一对应关系见各 tabXxx 布尔 —— 七块内容只显示一块。
QVariantList AgentUi::settingsSections() const
{
    QVariantList out;
    auto add = [&out, this](const QString &key, const QString &label, const QString &icon) {
        const bool on = (key == m_settingsTab);
        QVariantMap m;
        m.insert(QStringLiteral("key"), key);
        m.insert(QStringLiteral("label"), label);
        m.insert(QStringLiteral("icon"), icon);
        m.insert(QStringLiteral("active"), on);
        // 选中态样式也由 C++ 定 —— QML 里不能写 `active ? A : B`（那是三元配色判断）。
        // 2026-10-30 用户点单「不要紫色」：原值取自 MD3 的 cSecondaryCont
        // (#E8DEF8/#4A4458 —— 淡紫/紫灰)，现改为 shadcn 锌灰中性色：
        //   rowBg 选中  = #E4E4E7 / #3F3F46（zinc-200 / zinc-700）
        //   fg    选中  = #09090B / #FAFAFA（= Theme.color.textPrimary）
        //   fg    未选  = #71717A / #A1A1AA（= Theme.color.textSecondary）
        // ⚠️ 为什么 rowBg 不用 Theme.color.surfaceVariant(#F4F4F5/#27272A)：
        //    侧栏自身底色就是 ash.muted(#27272A，深色) ⇒ 选中行会与底色**撞成一片**，
        //    选中态肉眼消失（2026-10-30 截图实证）。故往亮处再提一档。
        // ⚠️ 这几组 Hex 是共享库调色板的镜像，改 Theme/ash 色板时要同步（C++ 取不到 QML 单例）。
        // ⚠️ 不要改回紫色：被控端主色一律中性，品牌紫只属于管理端。
        m.insert(QStringLiteral("rowBg"),
                 on ? (m_lightMode ? QStringLiteral("#E4E4E7") : QStringLiteral("#3F3F46"))
                    : QStringLiteral("transparent"));
        m.insert(QStringLiteral("fg"),
                 on ? (m_lightMode ? QStringLiteral("#09090B") : QStringLiteral("#FAFAFA"))
                    : (m_lightMode ? QStringLiteral("#71717A") : QStringLiteral("#A1A1AA")));
        // QFont::Normal=50 / Medium=57，与 QML 的 Font.Normal/Font.Medium 同值
        m.insert(QStringLiteral("weight"), on ? int(QFont::Medium) : int(QFont::Normal));
        out.append(m);
    };
    // 七个分类对齐用户蓝图（通用/主界面/通知与音量/自动化/更新/插件/关于）。
    // 「关于」并进「诊断」：配置自检、噪音记录、自习记录是原托盘右键的能力，
    // 蓝图七类里没有独立位置，挂在「关于」下比新造第八类更贴近参考。
    add(QStringLiteral("general"),    QStringLiteral("通用"),       QStringLiteral("sliders"));
    add(QStringLiteral("interface"),  QStringLiteral("主界面"),     QStringLiteral("layout-grid"));
    add(QStringLiteral("notify"),     QStringLiteral("通知与音量"), QStringLiteral("bell"));
    add(QStringLiteral("automation"), QStringLiteral("自动化"),     QStringLiteral("clock"));
    add(QStringLiteral("update"),     QStringLiteral("更新"),       QStringLiteral("download"));
    add(QStringLiteral("plugin"),     QStringLiteral("插件"),       QStringLiteral("rocket"));
    add(QStringLiteral("about"),      QStringLiteral("关于与诊断"), QStringLiteral("info"));
    return out;
}

void AgentUi::setSettingsTab(const QString &key)
{
    // 只认上面那张表里的 key：乱传一律忽略，不留"点空"的假状态。
    static const QStringList kKeys{
        QStringLiteral("general"),    QStringLiteral("interface"),
        QStringLiteral("notify"),     QStringLiteral("automation"),
        QStringLiteral("update"),     QStringLiteral("plugin"),
        QStringLiteral("about"),
    };
    if (!kKeys.contains(key)) return;
    if (m_settingsTab == key) return;
    m_settingsTab = key;
    emit settingsTabChanged();
}

/* ── 退出守卫（2026-11-03 · 用户笔记序号 2）─────────────────────────────
 *  为什么在这份 TU 而不在 agent_panel.cpp：能 include policy/quit_guard.h，
 *  且这份 TU 已被设置页探针链（_agent_ui_probe.py），改了会被覆盖到。
 *  agent_panel.cpp 依赖 main.cpp 一堆全局，探针链不了 ⇒ 写在那儿等于没验。
 *  写完**重读快照再 publish**，与全部开关同一纪律：界面不会"显示成功但没落盘"。 */
void AgentUi::setQuitPassword(const QString &plain)
{
    QuitGuard::setPassword(plain.trimmed());   // 空串 = 清除
    m_quitPasswordEnabled = QuitGuard::hasPassword();
    setActionResultText(m_quitPasswordEnabled
        ? QStringLiteral("退出口令已保存：从托盘「退出被控端」需要输入口令。")
        : QStringLiteral("退出口令已清除：退出被控端不再需要口令。"));
    qInfo("[agent-shell] 退出口令 → %s", m_quitPasswordEnabled ? "已启用" : "已清除");
    publish();
}
