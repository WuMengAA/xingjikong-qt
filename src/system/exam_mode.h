// 考试模式（《全量设计文档》3.7 第一版实现，2026-10-06；2026-10-09 进程管控改黑名单）
//
// 定位：防君子不防小人的"考试辅助"，不是严格监考系统。
// 做四件事：
//   1. 全屏置顶无边框窗口：拦截鼠标键盘输入，显示倒计时与提示；
//   2. **黑名单**进程轮询：每 2 秒比对进程列表，**只杀黑名单里点名的**；
//   3. 计时：被控端本地倒计时（分钟），到点自动结束考试模式；
//   4. 结束：恢复输入、停止轮询、关闭窗口。
// 后置：防火墙禁网、USB 禁用、屏幕录制 —— 需要真机验证且破坏性强。
//
// ── 为什么是黑名单，不是白名单（2026-10-09 事故后改，这是**架构方向**不是参数调优）──
//   白名单意味着"除了名单上的，**其余一律杀掉**"。要让它安全，就必须穷举出
//   Windows 上所有安全进程 —— 这是不可能的：服务对象之间有 svchost 的多个实例、
//   有杀毒软件、有机房管控软件、有驱动宿主，任何一个是 critical 进程，杀掉即
//   CRITICAL_PROCESS_DIED（0xEF）蓝屏。2026-10-09 学校机房就是这样被反复重启的。
//   黑名单反过来：**默认谁都不动，只杀老师点名的几个**。猜错最多是"作弊软件没杀掉"
//   （fail-safe），绝不可能把系统杀崩（fail-dangerous）。
//   这条是铁律，别再改回白名单，哪怕有人觉得"白名单更严格"。
//
// 线程模型：所有窗口/QTimer 都在主线程（Qt Widgets 要求）。窗口用成员指针持有，
// 由 ExamMode::start / ExamMode::stop 创建/销毁；黑名单轮询 QTimer 每 2 秒触发。
// 与云端指令的衔接在 main.cpp：exam_mode / exam_mode_stop 两个 action 走这里。

#ifndef EXAM_MODE_H
#define EXAM_MODE_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QStringList>

class QWidget;

class ExamMode : public QObject
{
    Q_OBJECT
public:
    static ExamMode &inst();

    /**
     * 启动考试模式。minutes>0 表示倒计时；blacklist 为**要杀掉**的进程名
     * （不带 .exe，大小写不敏感，如 "chrome" / "steam"）。
     *
     * @return 是否真的启用**进程终止**（黑名单非空才启用）。返回 false 表示只做了全屏拦截，
     *         一个进程都没杀 —— 这绝不是"启动失败"，但回执必须把 reason() 的原因带上，
     *         否则老师会以为考试模式全程硬拦（2026-10-07 蓝屏事故后定下的规矩）。
     */
    bool start(int minutes, const QStringList &blacklist);
    /** 上次 start() 没启用进程终止的原因（启用过就为空）。 */
    QString reason() const { return m_reason; }
    /** 结束考试模式（手动/到点），返回是否真的结束过。 */
    bool stop(const QString &reason = QString());
    /** 当前是否处于考试模式。 */
    bool active() const { return m_active; }
    /** 剩余秒数（未激活时返回 -1）。 */
    int remainingSec() const;
    /** 到点自动结束（考试结束，学生恢复使用）—— 供窗口回调/定时器用。 */
    void onTimeout();

private:
    QString m_reason;      // 上次 start() 未启用进程终止的原因
    bool m_enforcing = false;   // 上次 start() 是否真的启用进程终止

signals:
    /** 状态变化（active/remaining 显示用）；timeout=true 表示到点自动结束。 */
    void stateChanged(bool active, int remainingSec, bool timeout);

private:
    ExamMode();
    QWidget *m_window = nullptr;
    QTimer *m_ticker = nullptr;
    QTimer *m_watchdog = nullptr;
    QElapsedTimer m_clock;
    int m_durationSec = 0;
    QStringList m_blacklist;   // 要终止的进程名（已归一化：小写、无 .exe）
    bool m_active = false;

    void createWindow(int minutes);
    void destroyWindow();
    void killBlacklisted();
    void updateTitle();
};

#endif // EXAM_MODE_H
