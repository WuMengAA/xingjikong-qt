// 星集控 · 单实例（防多进程）守卫 —— 管理端（viewer）与被控端（agent）**同一份实现**。
//
// 为什么不在公共库里：两个工程各自独立出包，抽公共库会牵动版本号与部署目录；
// 这里保持"两端同样两份代码"，改的时候一次改两处（两端 main.cpp 的调用句也要一起改）。
//
// 上一版为什么**失效**（2026-10-05 实测，用户报"防多进程又失效了"）：
//   被控端原来只用了 `CreateMutexW(..., L"Local\\StelarithAgentQt_Singleton")`。
//   `Local\` 是**会话内**命名空间 —— 计划任务常跑在 Session 0（SYSTEM 账户、无交互桌面），
//   手动启动的在交互会话（SESSION 1）。两边各有一份 Local\ 命名空间、互不可见，
//   于是两个实例都 CreateMutex 成功、都觉得自己是第一个 → 双开仍有 2 个进程在跑。
//   原注释里"Local\ 同会话内足够（计划任务与手动启动同属交互会话）"这句判断是错的：
//   它成立的前提是那个计划任务必须是**用户级**的，一旦改成 SYSTEM 级就翻车。
//
// 现在的**两道闸**（两道都占到才算单例，任一道被占 → acquire() 返回 false，调用方直接退出）：
//   ① 命名互斥体：`Global\` 优先（跨会话，Session 0 也算同一台机器），
//      没有 SeCreateGlobalPrivilege 权限（普通受限上下文会 ERROR_ACCESS_DENIED）就退回 `Local\`；
//   ② 文件锁（QLockFile）兜底：写在 %LOCALAPPDATA% 下，跨会话、跨进程都稳；
//      进程崩溃/被杀时 OS 会随句柄关闭自动解除文件锁，不会留僵尸锁。
//
// ⚠️ 踩过的坑（2026-10-05，都是本次真编译报错/自查抓出来的，别再踩）：
//   - Qt6 **没有** `QFileLock`（那是 Qt4 的死名字），只有 `QLockFile`（#include <QLockFile>）。
//   - Qt 6.8 的 `QLockFile` **没有 `setFileName()`**（6.9 才加）—— 路径只能走构造函数。
//     所以这里存 QLockFile*，acquire() 里按路径 new 出来，release() 里 delete。
//     顺带还避掉一个雷：QLockFile 的拷贝构造是 deleted，当**值成员**会让外层的默认构造也被判死。
//   - `setStaleLockTime(0)` **不能写**：Qt 的语义是「文件 mtime 到现在 > staleLockTime 才算陈旧」，
//     传 0 会让任何已存在的锁瞬间"过期"，第二个实例直接把锁抢走 → 又变双开。必须留默认 30 分钟。
//
// 用法（两端 main.cpp 同款，别抄一半）：
//   SingleInstanceGuard guard;
//   QString why;
//   if (!guard.acquire(L"StelarithViewerQt_Singleton", L"viewer.lock", &why)) {
//       logf("…已有实例（%s）→ 退出", qPrintable(why));   // 给用户一个看得见的提示
//       return 0;
//   }

#pragma once

#include <QString>

// HANDLE 就位（头文件要声明 HANDLE 成员，不能等 cpp 再带 windows.h）
#include <windows.h>

// ② 文件锁那道闸的类（Qt6 里 QLockFile 派生自 QObject，头文件就得带上）
#include <QLockFile>

/**
 * 尝试占住"单机只允许一个实例"的位置。
 *
 * @param mutexName 互斥体名字（**不含** Global\ / Local\ 前缀），两端各用各的，别撞名。
 * @param lockFile  锁文件名（落在 %LOCALAPPDATA%/<应用数据目录>/lock/ 下），两道闸之一。
 *                  传 nullptr 表示只走互斥体（不推荐：少了兜底）。
 * @param reason    失败原因回写（成功时保持为空串），给日志/弹窗用。
 * @return true = 抢到了（guard 析构时自动放；整个 main 期间**必须保持这个对象活着**）。
 */
class SingleInstanceGuard
{
public:
    SingleInstanceGuard() = default;
    ~SingleInstanceGuard() { release(); }

    SingleInstanceGuard(const SingleInstanceGuard &) = delete;
    SingleInstanceGuard &operator=(const SingleInstanceGuard &) = delete;

    bool acquire(const wchar_t *mutexName, const wchar_t *lockFile, QString *reason = nullptr);

    /** 主动放手（退出前调用一次；析构也会做）。 */
    void release();

private:
    HANDLE m_mutex = nullptr;      // ① 互斥体句柄（nullptr = 极端受限上下文里没起起来，只靠文件锁）
    QLockFile *m_lock = nullptr;   // ② 文件锁（acquire 里按路径 new；release 里 unlock+delete）
};
