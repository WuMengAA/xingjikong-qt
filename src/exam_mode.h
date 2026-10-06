// 考试模式（《全量设计文档》3.7 第一版实现，2026-10-06）
//
// 定位：防君子不防小人的"考试辅助"，不是严格监考系统。
// 第一版做四件事（破坏性/依赖外部能力的后置，见 DESIGN 注释）：
//   1. 全屏置顶无边框窗口：拦截鼠标键盘输入，显示倒计时与提示；
//   2. 白名单进程轮询：每 2 秒比对进程列表，不在白名单里的杀掉（递归结束可疑子进程）；
//   3. 计时：被控端本地倒计时（分钟），到点自动结束考试模式；
//   4. 结束：恢复输入、停止轮询、关闭窗口。
// 后置（不在第一版）：防火墙禁网、USB 禁用、屏幕录制 —— 需要真机验证且破坏性强。
//
// 线程模型：所有窗口/QTimer 都在主线程（Qt Widgets 要求）。窗口用成员指针持有，
// 由 ExamMode::start / ExamMode::stop 创建/销毁；白名单轮询 QTimer 每 2 秒触发。
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

    /** 启动考试模式。minutes>0 表示倒计时；whitelist 为允许保留的进程名（无扩展名小写，如 "examclient"）。 */
    void start(int minutes, const QStringList &whitelist);
    /** 结束考试模式（手动/到点），返回是否真的结束过。 */
    bool stop(const QString &reason = QString());
    /** 当前是否处于考试模式。 */
    bool active() const { return m_active; }
    /** 剩余秒数（未激活时返回 -1）。 */
    int remainingSec() const;
    /** 到点自动结束（考试结束，学生恢复使用）—— 供窗口回调/定时器用。 */
    void onTimeout();

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
    QStringList m_whitelist;
    bool m_active = false;

    void createWindow(int minutes);
    void destroyWindow();
    void killNonWhitelisted();
    void updateTitle();
};

#endif // EXAM_MODE_H
