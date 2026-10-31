#pragma once
// 星集控被控端 · 记录 / 自检 / 插件 / 播放时段 的展示快照（AgentRecordUi）
//
// ── 为什么单独成一个文件（2026-10-31 拆分）────────────────────────────────
// agent_ui.h 已 776 行、达到 scripts/fixtures/oversize-legacy.json 的登记值 ——
// 门禁规则是「白名单内 + 比登记时长 1 行 ⇒ error」，即**一行都不能再加**。
// 而三件新共用组件（AppStatTile / AppErrorState / AppSkeleton）要接到页面，
// C++ 侧必须补结构化数字、错误态、装载态，全都要加属性 —— 于是必须先腾空间。
//
// 搬的是「A-3 第二批：自检 / 插件市场 / 记录 / 播放时段」这一整簇：
// 属性声明、getter、槽、信号、成员四层一次性搬齐（AGENTS.md §10.8「一刀一轮，拆干净」）。
// 选它做第一刀的理由：它是**自成一体的历史批次**（2026-10-16 U5-A3），
// 与其他属性（连接状态 / OTA / 设置页）几乎没有交叉引用，搬动风险最低。
//
// ── ⚠️ 继承式拆分的唯一陷阱：NOTIFY 必须双发 ─────────────────────────────
// 本类与 AgentUi 各自声明一个**同名** `infoChanged`。实测（scripts/_agent_ui_split_probe.py
// 2026-10-31）：`emit infoChanged()` 在派生类里只发出**派生类**那个信号，
// 基类声明的属性**收不到通知** ⇒ QML 绑定不刷新 ⇒ 页面永远显示旧数据（静默失效）。
// ⇒ AgentUi::publish() 必须 `emit infoChanged(); emit AgentRecordUi::infoChanged();`
//    只发一个 = 这一整簇属性在界面上「活着但不会变」。
//
// ── 数据面约定不变（照旧）────────────────────────────────────────────────
// 本类只持**纯内存快照**，getter 不引用任何外部符号（moc 会把本头单独编成 TU，
// 见 agent_ui.h 顶部三连坑说明）。真实读写由 main.cpp 的 agentUiFill 经 friend 直写成员。
//
// ── QML 可见性 ──────────────────────────────────────────────────────────
// QML 拿到的上下文对象是 AgentUi*，基类的 Q_PROPERTY 同样可见（实测 Q1 PASS）。
// 属性名不变，故**所有既有 QML 一行都不用改**。

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class AgentUi;

/**
 * 记录页 / 自检页 / 插件市场 / 播放时段的展示快照。
 *
 * 全部属性都是「C++ 算好、QML 只上屏」的结果值：QML 不分组、不倒序、不统计、
 * 不拼串（AGENTS.md §10.7 / §10.9）。汇总类文案（"共 3 项 · 通过 2"、
 * "共 3 天 · 5,830 条采样"）一律在 apply* 入口里算完。
 */
class AgentRecordUi : public QObject
{
    Q_OBJECT
public:
    explicit AgentRecordUi(QObject *parent = nullptr) : QObject(parent) {}

    // ══ 自检 ═════════════════════════════════════════════════════════════
    Q_PROPERTY(QVariantList selfCheckItems       READ selfCheckItems       NOTIFY infoChanged)
    /** 汇总一句话（"共 4 项 · 通过 3 · 需处理 1"）。QML 不数数。 */
    Q_PROPERTY(QString      selfCheckSummaryText READ selfCheckSummaryText NOTIFY infoChanged)
    /** 是否**全部**通过。空列表时为 false（"没检测过"不等于"通过"）。 */
    Q_PROPERTY(bool         selfCheckAllOk       READ selfCheckAllOk       NOTIFY infoChanged)

    // ══ 插件市场 / 内置模块 ═══════════════════════════════════════════════
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY infoChanged)
    /** 内置模块层（2026-10-16「模块化插件」）：编译进 agent，可启停不可卸载。 */
    Q_PROPERTY(QVariantList modules READ modules NOTIFY infoChanged)

    // ══ 记录（噪音 / 自习）════════════════════════════════════════════════
    // 保留 *LogText 两个属性：托盘侧的历史纯文本口径仍在用，不在本刀删除。
    Q_PROPERTY(QString      noiseLogText     READ noiseLogText     NOTIFY infoChanged)
    Q_PROPERTY(QString      studyLogText     READ studyLogText     NOTIFY infoChanged)
    // 结构化视图（2026-10-31）：summary = 顶部一句话，Records = 每行 {label, value}。
    Q_PROPERTY(QString      noiseSummaryText READ noiseSummaryText NOTIFY infoChanged)
    Q_PROPERTY(QVariantList noiseRecords     READ noiseRecords     NOTIFY infoChanged)
    Q_PROPERTY(QString      studySummaryText READ studySummaryText NOTIFY infoChanged)
    Q_PROPERTY(QVariantList studyRecords     READ studyRecords     NOTIFY infoChanged)

    // ══ 播放时段 ═════════════════════════════════════════════════════════
    Q_PROPERTY(bool    playScheduleEnabled READ playScheduleEnabled WRITE setPlayScheduleEnabled NOTIFY infoChanged)
    Q_PROPERTY(QString playWindowsText     READ playWindowsText     WRITE setPlayWindowsText     NOTIFY infoChanged)
    Q_PROPERTY(QString playStateText       READ playStateText       NOTIFY infoChanged)

    // ── getter（纯成员；moc 的属性系统会引用它们的地址）──
    QVariantList selfCheckItems() const       { return m_selfCheckItems; }
    QString      selfCheckSummaryText() const { return m_selfCheckSummaryText; }
    bool         selfCheckAllOk() const       { return m_selfCheckAllOk; }
    QVariantList plugins() const              { return m_plugins; }
    QVariantList modules() const              { return m_modules; }
    QString      noiseLogText() const         { return m_noiseLogText; }
    QString      studyLogText() const         { return m_studyLogText; }
    QString      noiseSummaryText() const     { return m_noiseSummaryText; }
    QVariantList noiseRecords() const         { return m_noiseRecords; }
    QString      studySummaryText() const     { return m_studySummaryText; }
    QVariantList studyRecords() const         { return m_studyRecords; }
    bool         playScheduleEnabled() const  { return m_playScheduleEnabled; }
    QString      playWindowsText() const      { return m_playWindowsText; }
    QString      playStateText() const        { return m_playStateText; }

    // ── setter（写成员 + 通知；真实持久化在 main.cpp 的 switchChanged 接收端）──
    /**
     * 播放时段总开关。
     *
     * ⚠️ 必须同时发 **switchChanged**，不能只发 infoChanged：
     *   · infoChanged   → QML 重读属性（界面回显正确）
     *   · switchChanged → main.cpp 真正落盘（AgentSettings::setPlayScheduleEnabled）
     * 只发前者 = 界面看起来勾上了、重启就没了（本项目"假勾"事故的同一形态）。
     *
     * 本类无法直接 emit 派生类的 switchChanged，故声明自己的同名信号，
     * 由 AgentUi 构造函数里 connect 转发（见 agent_ui.h 的「信号转发」区）。
     */
    void setPlayScheduleEnabled(bool v)
    {
        if (m_playScheduleEnabled == v) return;
        m_playScheduleEnabled = v;
        emit infoChanged();
        emit switchChanged(QStringLiteral("playScheduleEnabled"), v);
    }
    /**
     * 播放时段文本（每行一个 HH:MM-HH:MM）。
     * 只写成员 + infoChanged：真实保存走 savePlaySchedule() 的显式保存按钮，
     * 逐字输入时不该每一键都落盘。
     */
    void setPlayWindowsText(const QString &v)
    {
        if (m_playWindowsText == v) return;
        m_playWindowsText = v;
        emit infoChanged();
    }

    // ══ 给 main.cpp 的接线 lambda 用的写入口 ══════════════════════════════
    /**
     * 记录页「读数 → 上屏形状」一次性入口。
     * 入参形状见 src/p3log/p3_record_view.h：{ summary, rows:[{label,value}] }。
     */
    void applyNoiseLogView(const QString &text, const QVariantMap &view)
    {
        m_noiseLogText     = text;
        m_noiseSummaryText = view.value(QStringLiteral("summary")).toString();
        m_noiseRecords     = view.value(QStringLiteral("rows")).toList();
    }
    void applyStudyLogView(const QString &text, const QVariantMap &view)
    {
        m_studyLogText     = text;
        m_studySummaryText = view.value(QStringLiteral("summary")).toString();
        m_studyRecords     = view.value(QStringLiteral("rows")).toList();
    }
    /** 自检四项 + 汇总（计数一律在此算，QML 不数数）。 */
    void applySelfCheckItems(const QVariantList &items)
    {
        m_selfCheckItems = items;
        int pass = 0;
        for (const QVariant &v : items) {
            if (v.toMap().value(QStringLiteral("ok")).toBool()) ++pass;
        }
        m_selfCheckAllOk = (!items.isEmpty() && pass == items.size());
        m_selfCheckSummaryText = QStringLiteral("共 %1 项 · 通过 %2 · 需处理 %3")
                                     .arg(items.size()).arg(pass).arg(items.size() - pass);
    }

signals:
    // ── A-3 第二批 ──
    void selfCheckRequested();
    void selfCheckFixRequested();
    void pluginsRefreshRequested();
    void pluginInstallRequested(const QString &id);
    void pluginEnableRequested(const QString &id, bool enable);
    void pluginUninstallRequested(const QString &id);
    void pluginSampleInstallRequested();
    // ── 内置模块层 ──
    void modulesRefreshRequested();
    void moduleEnableRequested(const QString &id, bool enable);
    void noiseFolderRequested();
    void studyFolderRequested();
    void playScheduleSaveRequested();

    /**
     * 开关变化（"playScheduleEnabled" 等）——由 AgentUi 构造函数 connect 转发到它自己的
     * switchChanged。为什么不在本类直接 emit 派生类信号：基类不认识派生类型，
     * 写 `emit AgentUi::x` 需要完整类型，而本头被 moc 单独编译成 TU（见 agent_ui.h 三连坑）。
     */
    void switchChanged(const QString &key, const QVariant &value);

    /** ⚠️ 与 AgentUi::infoChanged **不同** 的同名信号，见文件头陷阱说明。 */
    void infoChanged();

public slots:
    // ── A-3 第二批 ──
    void runSelfCheckCheck() { emit selfCheckRequested(); }
    void selfCheckFix() { emit selfCheckFixRequested(); }
    void pluginsRefresh() { emit pluginsRefreshRequested(); }
    void pluginInstall(const QString &id) { emit pluginInstallRequested(id); }
    void pluginEnable(const QString &id, bool enable) { emit pluginEnableRequested(id, enable); }
    void pluginUninstall(const QString &id) { emit pluginUninstallRequested(id); }
    void pluginInstallSample() { emit pluginSampleInstallRequested(); }
    // ── 内置模块层 ──
    void modulesRefresh() { emit modulesRefreshRequested(); }
    void moduleEnable(const QString &id, bool enable) { emit moduleEnableRequested(id, enable); }
    void openNoiseFolder() { emit noiseFolderRequested(); }
    void openStudyFolder() { emit studyFolderRequested(); }
    void savePlaySchedule() { emit playScheduleSaveRequested(); }

private:
    // ── A-3 第二批 ──
    QVariantList m_selfCheckItems, m_plugins, m_modules;
    QString m_noiseLogText, m_studyLogText, m_playWindowsText, m_playStateText;
    // 2026-10-31：自检汇总 + 记录页结构化行（都由 C++ 算好，QML 零统计）
    QString m_selfCheckSummaryText;
    bool    m_selfCheckAllOk = false;
    QString m_noiseSummaryText, m_studySummaryText;
    QVariantList m_noiseRecords, m_studyRecords;
    bool m_playScheduleEnabled = false;

    // agentUiFill 直接写这些成员（不触发 switchChanged），见 agent_ui.h 的快照写入约定
    friend void agentUiFill(AgentUi *ui);
};
