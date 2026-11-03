#include "boot_trace.h"

#include <QLoggingCategory>
#include <QMetaObject>

namespace {

QElapsedTimer &timer()
{
    static QElapsedTimer t;
    return t;
}

qint64 &lastMark()
{
    static qint64 v = 0;
    return v;
}

}   // namespace

void BootTrace::mark(const char *stage)
{
    QElapsedTimer &t = timer();
    if (!t.isValid())
        t.start();                          // 首次调用即起点（见头注释：t0 只能在这里）

    const qint64 now = t.elapsed();
    qInfo("[boot] %-22s 累计 %5lld ms（本段 %lld ms）",
          stage ? stage : "?", static_cast<long long>(now),
          static_cast<long long>(now - lastMark()));
    lastMark() = now;
}

qint64 BootTrace::elapsedMs()
{
    QElapsedTimer &t = timer();
    return t.isValid() ? t.elapsed() : 0;
}
