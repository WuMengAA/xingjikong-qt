// 进程终止安全闸门（2026-10-13 抽出，供 exam_mode 与 main.cpp 的 process_stop **共用**）
//
// ── 为什么必须是公共件，不能各写一份 ─────────────────────────────
//   2026-10-09 学校机房"反复重启"事故（CRITICAL_PROCESS_DIED 0xEF 蓝屏 → 自启 → 再杀）
//   之后的铁律是「任何 TerminateProcess 之前都要**问内核**，不猜进程名名单」。
//   但这条闸门一度只长在 exam_mode.cpp 里：main.cpp 的 process_stop 走的是另一条**纯进程名
//   硬禁止名单**（kProtectedSystem/kProtectedSecurity），而名单永远有缺口 ——
//   MemCompression / Registry / Secure System / LogonUI / 任意未列名的 session-0 服务宿主
//   都不在表里。被控端以 Highest 权限运行，TerminateProcess 掉 MemCompression 就是同一个 0xEF。
//   两处各写一份必然漂移（本项目已经栽过"改一处漏一处"），所以抽到这里：
//   **谁要杀进程，谁先过这道门。**
//
// ── 判定口径（照抄自 exam_mode 的原始实现，行为逐字一致）──────────
//     · ProcessBreakOnTermination(29)：内核标记 critical，杀它立刻蓝屏
//     · ProcessProtectionInformation(61)：受保护进程（PPL：杀软/系统组件常见）
//     · Session 0：服务会话 —— 不在用户的桌面上，杀它没有收益、风险是整机崩
//   ⚠️ 铁律：**打不开句柄 / 拿不到查询入口 / 任何一次查询失败，一律按"受保护"处理**
//      （宁可不杀，不可杀错）。这条保守处理就是这个功能的全部价值所在。

#ifndef STELARITH_PROC_GUARD_H
#define STELARITH_PROC_GUARD_H

#include <QString>
#include <windows.h>

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

/** 查进程的 SessionId。**查不到返回 0xFFFFFFFF**（调用方应按"不可杀"处理）。 */
DWORD queryProcessSessionId(HANDLE handle);

/**
 * 这个进程能不能杀？返回 **true = 绝不能杀**。
 * 保守原则：拿不到内核查询入口、或任何一次查询失败，都返回 true。
 */
bool processIsUntouchable(HANDLE handle, DWORD sessionId);

/**
 * 一站式判定：按 pid 给出"不能杀的原因"（**空串 = 可以杀**）。
 * 内部自己 OpenProcess / 查询 / CloseHandle。
 *
 * @param nameGuardReason 名字名单（kProtectedSystem 等）的判定结果：
 *        非空表示已被名单拦下，直接原样返回，不再开句柄。
 */
QString terminateBlockedReason(DWORD pid, const QString &nameGuardReason = QString());

#endif // STELARITH_PROC_GUARD_H
