#pragma once
// 循环提醒（2026-10-08 · 通知体系升级 §1.2 loopRemind 独立模块）
//
// 独立 QObject，不依赖 main.cpp：
//   · 收到提醒 → 发 signal 请上层展示（灵动岛/弹窗/托盘由上层定）
//   · 展示超时未确认 → 内部计时 → 间隔后再次触发（默认 60s，可覆盖）
//   · **只有 confirm() 才终止循环**；不点确认就一直循环
//   · 上限防霸屏：默认最多 60 次或 8 小时，之后发 expired 信号停掉
//   · 同时只有一个循环提醒在前台（教室机单屏约束）；新提醒顶掉旧的
//
// 接入（main.cpp）：connect requestShow → 显示通知；needConfirm 回执 → confirm()。

#include <QObject>
#include <QTimer>
#include <QString>
#include <QElapsedTimer>

class LoopReminder : public QObject {
    Q_OBJECT
public:
    struct Config {
        int intervalSec = 60;    // 未确认时的重现间隔
        int maxRounds = 60;      // 循环上限（0 = 不限次数，但 8h 硬顶仍生效）
        qint64 maxMs = 8LL * 3600 * 1000;   // 总时长硬顶 8 小时
    };

    explicit LoopReminder(QObject* parent = nullptr);

    // 开始一条循环提醒；同 id 重复调 = 刷新（重新计时/展示）。
    // title/body 仅透传展示语义，组件不渲染。
    void start(const QString& id, const QString& title, const QString& body, const Config& cfg = Config());

    // 用户确认 → 终止循环（唯一正常退出路径）
    void confirm(const QString& id);

    // 主动取消（如考试模式激活）：终止循环，不视为确认
    void cancel(const QString& id);

    // 当前是否有活动循环
    bool active() const { return m_active; }
    QString currentId() const { return m_id; }
    int roundsSoFar() const { return m_rounds; }

signals:
    // 每次到点/刷新时发（上层请展示）
    void requestShow(const QString& id, const QString& title, const QString& body, int round);
    // 达到上限或取消时发（上层收尾）
    void expired(const QString& id, const QString& reason);

private:
    void fire();
    void scheduleNext();

    QString m_id;
    QString m_title;
    QString m_body;
    Config m_cfg;
    int m_rounds = 0;
    bool m_active = false;
    QTimer m_timer;
    QElapsedTimer m_total;   // 总时长（8h 硬顶）
};