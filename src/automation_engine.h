// 星集控被控端 · 定时自动化规则引擎（2026-10-27 · Phase 3）
//
// 设计边界（关键，决定本文件能改什么、不能改什么）：
// · 本类**纯模型 + 调度**：只持有规则、按 1s 节拍评估触发器、条件通过即"点火"。
// · **不**直接调用任何子系统（音量 / 截图 / 通知 / 启动程序）。点火时发射
//   actionRequested(action)，main.cpp 把它接到既有的 executeAction(action, params)
//   —— 复用整套设备指令词表（launch_app / set_volume / screenshot / lock / notify…）
//   及其安全白名单，零新攻击面。这是"被控端本机不依赖管理端在场就能自己用"的落地。
// · 持久化：%LOCALAPPDATA%/xingjikong/automation.json（与 island_history.json 同族）。
// · 事件触发：onEvent(name) 由被控端各子系统在对应时刻调用
//   （boot / idle / classStart / classEnd / screenLock / screenUnlock）。
// · 线程：创建于主线程（qApp 子对象），QTimer 同线程，故可安全触达 QML 与 UI。
//
// ⚠️ 既有纪律：本文件是**纯增量**，不修改 island.h / bridge / executeAction 任何既有签名；
//   只在 executeAction 里**追加** automation_* 分支、在 main.cpp **新增**引擎接线。

#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QTimer>
#include <QString>
#include <QDateTime>

class AutomationEngine : public QObject
{
    Q_OBJECT
    // QML 岛面只读这一份摘要（与 island_history 同源模式：C++ 算好、不丢给 QML 过滤）。
    Q_PROPERTY(QVariantList ruleSummaries READ ruleSummaries NOTIFY rulesChanged)
    Q_PROPERTY(int ruleCount READ ruleCount NOTIFY rulesChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
    explicit AutomationEngine(QObject *parent = nullptr);
    ~AutomationEngine();

    bool ready() const { return m_ready; }
    int ruleCount() const { return m_rules.size(); }
    QVariantList ruleSummaries() const;

    // ── 规则 CRUD（岛面 / 管理端下发共用同一套）──────────────────────
    Q_INVOKABLE bool addRule(const QJsonObject &rule);
    Q_INVOKABLE bool updateRule(const QJsonObject &rule);
    Q_INVOKABLE bool removeRule(const QString &id);
    Q_INVOKABLE bool toggleRule(const QString &id, bool enabled);
    // 管理端拉取全量（返回规则原貌，供回显 / 校验）。
    Q_INVOKABLE QJsonArray listRules() const;
    // 管理端下发（= 按 id 新增或覆盖；校验不通过返回 false，不写入）。
    Q_INVOKABLE bool upsertFromRemote(const QJsonObject &rule);

    // ── 触发 ─────────────────────────────────────────────────────
    // 手动立即运行：忽略时间触发器，仍判条件（如时间窗 / 星期），通过即点火。
    Q_INVOKABLE void runNow(const QString &id);
    // 事件触发入口（被控端子系统在对应时刻调用）。
    Q_INVOKABLE void onEvent(const QString &eventName);

signals:
    void rulesChanged();
    void readyChanged();
    // 点火：携带一条设备指令（与 executeAction 同形），main.cpp 转发执行。
    void actionRequested(const QJsonObject &action);
    void engineLog(const QString &msg);

private:
    struct Rule {
        QString id;
        QString name;
        bool enabled = true;
        QJsonObject trigger;   // {type:"time"|"interval"|"event", time:"HH:MM",
                                //  intervalMin:N, event:"boot"|"idle"|"classStart"|...}
        QJsonObject condition; // {after:"HH:MM", before:"HH:MM", days:[1..7]}
        QJsonObject action;    // {kind:"notify"|"set_volume"|"mute"|"unmute"|"lock"|
                                //  "screenshot"|"launch_app", ...}
        qint64 nextRunMs = 0;  // 时间类触发器的下一次触发时刻（UTC ms）
        qint64 lastRunMs = 0;
        QString lastError;
    };

    void loadFromDisk();
    void saveToDisk();
    bool evaluateCondition(const Rule &r) const;       // 条件是否通过（时间窗 / 星期）
    bool fireRule(Rule &r);                            // 组装 action → emit actionRequested
    void recomputeSchedule(Rule &r);                   // 计算下一次触发的 nextRunMs
    void recomputeAllSchedules();
    QString dataFile() const;
    QJsonObject ruleToJson(const Rule &r) const;
    Rule ruleFromJson(const QJsonObject &o) const;
    QString summarizeTrigger(const Rule &r) const;
    QString summarizeAction(const Rule &r) const;
    QJsonObject buildAction(const Rule &r) const;      // action.kind → executeAction 形

    void onTick();                                    // 1s 调度节拍

    QVector<Rule> m_rules;
    QTimer *m_tick = nullptr;
    bool m_ready = false;
};
