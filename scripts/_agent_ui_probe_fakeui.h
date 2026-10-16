// 星集控 · 探针用假 AgentUi（_agent_ui_probe_fakeui.h，随仓库提交）
// ───────────────────────────────────────────────────────────────────────────
// 为什么单独拆一个头：Q_OBJECT 类要交给 moc，而 moc 对「.cpp 输入」生成的 TU 里
// **不含原文件**（它只认头文件输入才会 #include 回来）。拆成头 ⇒ moc 头 ⇒ 独立
// 编译，探针 .cpp 只 include 它（与生产 agent_ui.h 的 header-only 形态也一致）。
//
// ⚠️ 属性/方法**照 src/agent_ui.h 的真实签名**写（bulkList 教训：假上下文失真
//    会污染判断）。引用面由 `grep -rho "agentUi\.[A-Za-z_]*" qml/shell/` 全量收集。
// ⚠️ 不放匿名 namespace：moc 对匿名 namespace 里的 QObject 类支持不全。

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

// ⚠️ 不放匿名 namespace：moc 对匿名 namespace 里的 QObject 类支持不全。
class FakeAgentUi : public QObject
{
    Q_OBJECT
#define PROP(type, name) \
    Q_PROPERTY(type name READ name CONSTANT) \
    type m_##name; \
public: \
    type name() const { return m_##name; } \
    void set_##name(const type &v) { m_##name = v; }
    PROP(QString, appVersion)
    PROP(QString, uid)
    PROP(QString, cloudUrl)
    PROP(QString, connState)
    PROP(QString, dataDir)
    PROP(QString, logPath)
    PROP(QString, latestText)
    PROP(QString, updateNotes)
    PROP(QString, otaModeText)
    PROP(QString, otaAutoText)
    PROP(QString, otaLastText)
    PROP(QString, otaNextText)
    PROP(QString, noiseLogText)
    PROP(QString, studyLogText)
    PROP(QString, playWindowsText)
    PROP(QString, playStateText)
    PROP(bool, hasNewer)
    PROP(bool, canUpgrade)
    PROP(bool, otaPausedNow)
    PROP(bool, playScheduleEnabled)
    PROP(bool, notifyEnabled)
    PROP(bool, voiceEnabled)
    PROP(bool, noiseEnabled)
    PROP(bool, islandIdle)
    PROP(bool, lightMode)
    PROP(bool, autostart)
    PROP(bool, desktopShortcut)
    PROP(QVariantList, selfCheckItems)
    PROP(QVariantList, plugins)
    PROP(QVariantList, modules)
#undef PROP

public:
    QStringList calls;   // 被页调过的 invokable（验页真的实例化了）

    Q_INVOKABLE void refresh()               { calls << QStringLiteral("refresh"); }
    Q_INVOKABLE void openLogDir()            { calls << QStringLiteral("openLogDir"); }
    Q_INVOKABLE void openDataDir()           { calls << QStringLiteral("openDataDir"); }
    Q_INVOKABLE void openNoiseFolder()       { calls << QStringLiteral("openNoiseFolder"); }
    Q_INVOKABLE void openStudyFolder()       { calls << QStringLiteral("openStudyFolder"); }
    Q_INVOKABLE void upgradeNow()            { calls << QStringLiteral("upgradeNow"); }
    Q_INVOKABLE void toggleOtaPause()        { calls << QStringLiteral("toggleOtaPause"); }
    Q_INVOKABLE void pluginsRefresh()        { calls << QStringLiteral("pluginsRefresh"); }
    Q_INVOKABLE void pluginInstallSample()   { calls << QStringLiteral("pluginInstallSample"); }
    Q_INVOKABLE void pluginInstall(const QString &)      { calls << QStringLiteral("pluginInstall"); }
    Q_INVOKABLE void pluginUninstall(const QString &)    { calls << QStringLiteral("pluginUninstall"); }
    Q_INVOKABLE void pluginEnable(const QString &, bool) { calls << QStringLiteral("pluginEnable"); }
    Q_INVOKABLE void modulesRefresh()        { calls << QStringLiteral("modulesRefresh"); }
    Q_INVOKABLE void moduleEnable(const QString &id, bool on)
    { calls << QStringLiteral("moduleEnable:%1=%2").arg(id, on ? 1 : 0); }
    Q_INVOKABLE void runSelfCheckCheck()     { calls << QStringLiteral("runSelfCheckCheck"); }
    Q_INVOKABLE void selfCheckFix()          { calls << QStringLiteral("selfCheckFix"); }
    Q_INVOKABLE void savePlaySchedule()      { calls << QStringLiteral("savePlaySchedule"); }
};
