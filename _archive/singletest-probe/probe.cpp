// 单实例守卫探针（无界面，控制台程序）
//
// 为什么要有它：被控端是 WIN32_EXECUTABLE(GUI 子系统) 的托盘程序，在自动化/无桌面环境里
// 起不来也无法判断它走到了哪一步（日志都写不出）。但"防多进程"这件事的核心逻辑在
// src/singleinstance.{h,cpp} 里，与界面无关 —— 用这个控制台探针直接压它，才能拿到硬证据。
//
// 三个用例：
//   ① 首次 acquire  → 期望 ACQUIRED
//   ② ①还活着时再 acquire → 期望 BLOCKED（gate 会告诉你是 MUTEX 还是 LOCK）
//   ③ ①退出后再 acquire → 期望 ACQUIRED（证明锁真的释放了，不是僵尸锁）

#include "singleinstance.h"

#include <QCoreApplication>
#include <QThread>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // 与真 exe 同名，保证 QStandardPaths 算出的锁目录一致
    QCoreApplication::setApplicationName(QStringLiteral("stelarith-agent-qt"));

    SingleInstanceGuard guard;
    QString why;
    const bool ok = guard.acquire(L"StelarithAgentQt_Singleton", L"agent.lock", &why);

    std::printf("%s  pid=%lu  reason=%s\n",
                ok ? "ACQUIRED" : "BLOCKED",
                (unsigned long)GetCurrentProcessId(),
                why.isEmpty() ? "-" : why.toUtf8().constData());
    std::fflush(stdout);

    if (!ok) return 2;                    // 被拦下：立刻退出，模拟第二个实例

    const int holdMs = (argc > 1) ? QString::fromLocal8Bit(argv[1]).toInt() : 15000;
    QThread::sleep(static_cast<unsigned long>(holdMs / 1000));   // 占着锁，等别人来撞
    guard.release();
    std::printf("RELEASED pid=%lu\n", (unsigned long)GetCurrentProcessId());
    std::fflush(stdout);
    return 0;
}
