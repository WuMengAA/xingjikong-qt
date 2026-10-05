// 单实例守卫的 Windows 实现（管理端 / 被控端**同一份**，见 singleinstance.h 顶部注释）。
//
// 改这个文件的规矩：改完必须把 singleinstance.h/.cpp 原样复制一份到另一个工程的 src/，
// 两端字节一致（用户 2026-10-05 明确要求"两个项目都要搞特殊功能同步"）。

#include "singleinstance.h"

#include <QDir>
#include <QLockFile>
#include <QStandardPaths>

#include <windows.h>

#include <cstdio>

namespace {

/**
 * 开一个命名互斥体，顺带把结果说清楚。
 *
 * 先看 Global\（跨会话，Session 0 与交互会话算同一台机器）—— 这是上一版漏掉的那道；
 * 没有 SeCreateGlobalPrivilege（普通受限上下文 CreateMutex 会 ERROR_ACCESS_DENIED）
 * 就降级到 Local\，总比什么都不加强。
 *
 * 注意：CreateMutexW 创建**已存在**的互斥体也返回成功，
 * 靠 GetLastError()==ERROR_ALREADY_EXISTS 判断"别人在跑"；此时 h 仍有效，调用方负责关。
 */
struct MutexTry
{
    HANDLE h = nullptr;
    bool alreadyExists = false;
};

MutexTry openMutex(const wchar_t *name)
{
    MutexTry r;
    wchar_t full[192];

    swprintf_s(full, 192, L"Global\\%s", name);
    r.h = CreateMutexW(nullptr, TRUE, full);
    if (r.h) {
        if (GetLastError() == ERROR_ALREADY_EXISTS) r.alreadyExists = true;
        return r;                                   // 权限够：管它已存在还是新创建，交给调用方裁决
    }

    const DWORD e = GetLastError();
    if (e != ERROR_ACCESS_DENIED && e != ERROR_INVALID_PARAMETER) {
        // 别的错误（配额/介质之类）：不强推，交给文件锁兜底，别把程序挡在门外
        r.h = nullptr;
        return r;
    }

    swprintf_s(full, 192, L"Local\\%s", name);
    r.h = CreateMutexW(nullptr, TRUE, full);
    if (!r.h) { r.h = nullptr; return r; }          // 连 Local\ 都起不来 → 只靠文件锁
    if (GetLastError() == ERROR_ALREADY_EXISTS) r.alreadyExists = true;
    return r;
}

} // namespace

bool SingleInstanceGuard::acquire(const wchar_t *mutexName, const wchar_t *lockFile, QString *reason)
{
    if (!mutexName) {
        if (reason) *reason = QStringLiteral("没给互斥体名");
        return false;
    }

    /* ── ① 命名互斥体 ────────────────────────────────────────────────────
     * 上一版只写 Local\，Session 0（计划任务）与 SESSION 1（手动双击）各认各的 → 双开。
     * 现在 Global\ 优先、Local\ 回退；Global\ 被别的实例占着 = 直接判定双开。 */
    const MutexTry mt = openMutex(mutexName);
    if (mt.alreadyExists) {
        CloseHandle(mt.h);                          // alreadyExists 时 h 仍有效，必须关
        fprintf(stderr, "[stelarith] 已有另一个实例占着互斥体 Global\\%ls → 本次启动退出\n", mutexName);
        if (reason) *reason = QStringLiteral("已有实例占着互斥体（%1）").arg(QString::fromWCharArray(mutexName));
        return false;
    }
    m_mutex = mt.h;                                 // 可能为 nullptr：极端受限时只靠文件锁，不硬退
    if (!m_mutex) {
        fprintf(stderr, "[stelarith] WARN 互斥体起不来（GetLastError=%lu），只靠文件锁兜底\n",
                (unsigned long)GetLastError());
    }

    /* ── ② 文件锁兜底 ────────────────────────────────────────────────────
     * 互斥体在"句柄被继承/残留"这类边缘情形下也会漏，文件锁是最后一道。
     * 用非阻塞 tryLock：拿不到立刻返回 false，绝不在这儿卡住启动。 */
    if (!lockFile) return true;                     // 调用方没给锁文件名 → 只守互斥体

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/lock");
    if (!QDir().mkpath(dir)) {
        release();
        if (reason) *reason = QStringLiteral("锁目录建不出来：%1").arg(dir);
        return false;
    }

    const QString path = dir + QStringLiteral("/") + QString::fromWCharArray(lockFile);
    // Qt 6.8 的 QLockFile 没有 setFileName()，路径只能走构造函数。
    QLockFile *lk = new QLockFile(path);            // 起手就 new：拿到=要守护，没拿到也要 delete
    // ⚠️ 这里**故意不调** setStaleLockTime()：
    // Qt 的陈旧判定是「mtime 到现在 > staleLockTime」，传 0 会让任何已存在的锁**瞬间过期**，
    // 第二个实例就会把锁抢走 → 又变双开（本人 2026-10-05 自查抓到的坑）。
    // 保持默认 30 分钟：活着的实例一直不写锁文件就一直不过期，崩掉/被杀的进程 OS 会解除文件锁。
    // 同理也别设 -1（彻底关掉陈旧检测，万一真留了僵尸锁就谁也起不来）。
    if (!lk->tryLock()) {
        delete lk;
        release();                                  // 互斥体若也拿到了，一并放掉
        fprintf(stderr, "[stelarith] 已有另一个实例占着文件锁 %ls → 本次启动退出\n", lockFile);
        if (reason) *reason = QStringLiteral("已有实例占着文件锁（%1）").arg(path);
        return false;
    }
    m_lock = lk;                                    // 交棒：release/析构负责 unlock + delete
    return true;
}

void SingleInstanceGuard::release()
{
    if (m_lock) {
        if (m_lock->isLocked()) m_lock->unlock();   // 进程被杀时 OS 也会解除，不会留僵尸锁
        delete m_lock;
        m_lock = nullptr;
    }
    if (m_mutex) { CloseHandle(m_mutex); m_mutex = nullptr; }
}
