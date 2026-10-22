// 实现：2026-10-22 逐字搬自 main.cpp（AGENTS.md §10 第二刀）。

#include "ProcessUtil.h"

#include <QProcess>
#include <QEventLoop>
#include <QTimer>
#include <QObject>

namespace ProcessUtil {

// waitFinished：保持"同步等到退出"的函数语义（调用方代码一行不改），但等待在
// QEventLoop 里转 —— 期间心跳照发、指令照收、定时器照跑。
// 重入说明：等待期间云端再派同一动作（如连发两次抓拍）时，各用独立 QProcess/独立临时
// 文件名（毫秒戳），无共享态，安全；云端本来也要等本动作回执后才标记完成。
bool waitFinished(QProcess &p, int timeoutMs)
{
    if (p.state() == QProcess::NotRunning) return true;
    QEventLoop loop;
    QTimer t;
    t.setSingleShot(true);
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&p, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&p, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    t.start(timeoutMs);
    loop.exec();
    return p.state() == QProcess::NotRunning;
}

} // namespace ProcessUtil
