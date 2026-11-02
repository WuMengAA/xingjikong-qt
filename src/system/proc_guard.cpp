#include "proc_guard.h"

namespace {

constexpr DWORD kProcInfoBreakOnTermination = 29;   // ProcessBreakOnTermination
constexpr DWORD kProcInfoProtection         = 61;   // ProcessProtectionInformation
constexpr DWORD kProcInfoSession            = 25;   // ProcessSessionInformation

typedef LONG (NTAPI *PfnNtQueryInformationProcess)(HANDLE, DWORD, PVOID, ULONG, PULONG);

struct StePsProtection {
    UCHAR level;    // Type|Signer<<3 —— 非 0 即受保护
    UCHAR type;
    UCHAR signer;
};

HMODULE g_ntdll = nullptr;
PfnNtQueryInformationProcess g_ntQueryProc = nullptr;

void ensureNtdll()
{
    if (g_ntQueryProc) return;
    if (!g_ntdll) g_ntdll = GetModuleHandleW(L"ntdll.dll");
    if (g_ntdll) {
        g_ntQueryProc = reinterpret_cast<PfnNtQueryInformationProcess>(
            GetProcAddress(g_ntdll, "NtQueryInformationProcess"));
    }
}

} // namespace

DWORD queryProcessSessionId(HANDLE handle)
{
    ensureNtdll();
    if (!g_ntQueryProc) return 0xFFFFFFFF;   // 拿不到内核查询入口 ⇒ 未知
    ULONG dummy = 0;
    DWORD sessionId = 0xFFFFFFFF;
    if (g_ntQueryProc(handle, kProcInfoSession, &sessionId, sizeof(sessionId), &dummy) < 0)
        return 0xFFFFFFFF;
    return sessionId;
}

bool processIsUntouchable(HANDLE handle, DWORD sessionId)
{
    ensureNtdll();
    if (!g_ntQueryProc) return true;   // 拿不到内核查询入口 ⇒ 一律不杀

    ULONG retLen = 0;

    // ① critical 进程：内核标记为"被终止就蓝屏"
    BOOLEAN breakOnTerm = FALSE;
    if (g_ntQueryProc(handle, kProcInfoBreakOnTermination, &breakOnTerm, sizeof(breakOnTerm), &retLen) < 0
        || retLen < sizeof(BOOLEAN)) {
        return true;
    }
    if (breakOnTerm) return true;

    // ② 受保护进程（PPL：杀软 / 系统组件常见）
    StePsProtection prot = { 0, 0, 0 };
    if (g_ntQueryProc(handle, kProcInfoProtection, &prot, sizeof(prot), &retLen) < 0)
        return true;
    if (retLen >= 1 && prot.level != 0) return true;

    // ③ Session 0 = 服务会话。服务不在用户的桌面上，杀它没有任何收益，
    //    风险却是整机崩掉 —— 一律放过。
    if (sessionId == 0) return true;

    return false;
}

QString terminateBlockedReason(DWORD pid, const QString &nameGuardReason)
{
    if (!nameGuardReason.isEmpty()) return nameGuardReason;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        // ⚠️ 打不开句柄 = 权限更高或系统进程 —— 一律放过（不是"反抗"，是保护）
        return QStringLiteral("打不开进程句柄（错误码 %1）：按受保护处理，拒绝结束")
            .arg(int(GetLastError()));
    }
    const DWORD sid = queryProcessSessionId(h);
    const bool untouchable = processIsUntouchable(h, sid);
    CloseHandle(h);

    if (!untouchable) return QString();   // 空串 = 可以杀
    return QStringLiteral("内核标记为 critical/受保护进程，或属于服务会话（Session %1）")
        .arg(sid == 0xFFFFFFFF ? QStringLiteral("未知") : QString::number(sid));
}
