#include "proc_inspect.h"

#include <QDateTime>

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include "../action/execute_action.h"   // WinCache 完整定义（进程主窗口快照缓存）

// 进程快照 + 可见窗口枚举的短时效缓存：管理端开「软件」弹窗会连发
// process_list 与 list_shortcut_candidates 两条，原本各自独立枚举一遍窗口
// （每条还要逐窗口非阻塞取标题）。两条间隔通常 <100ms，缓存 1.5s 让第二条
// 直接复用，省掉一次完整枚举，避免拖慢弹窗响应。
static WinCache g_winCache;
constexpr qint64 kWinCacheTtlMs = 1500;

/** 枚举所有进程 pid → exe 名。只读快照，不打开任何进程句柄。 */
QHash<quint32, QString> snapshotProcessNames()
{
    QHash<quint32, QString> m;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return m;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            m.insert((quint32)pe.th32ProcessID, QString::fromWCharArray(pe.szExeFile));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return m;
}

/** EnumWindows 回调：收集「有非空标题的顶级窗口」→ (hwnd, pid)。与 Rust 同口径。 */
BOOL CALLBACK enumWindowsCb(HWND hwnd, LPARAM lp)
{
    auto *acc = reinterpret_cast<QVector<QPair<HWND, quint32>> *>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;
    // 非阻塞取标题长度：挂起（无响应）窗口用 GetWindowTextLengthW 会卡死整条枚举，
    // 必须改用 SendMessageTimeout + SMTO_ABORTIFHUNG（与 windowIconBase64 取图标同一策略）。
    DWORD_PTR len = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETTEXTLENGTH, 0, 0, SMTO_ABORTIFHUNG, 60, &len) || len == 0)
        return TRUE;
    acc->append({ hwnd, (quint32)pid });
    return TRUE;
}

const WinCache &visibleWindowsCached()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!g_winCache.names.isEmpty() && now - g_winCache.at < kWinCacheTtlMs)
        return g_winCache;
    g_winCache.names = snapshotProcessNames();
    g_winCache.wins.clear();
    if (!g_winCache.names.isEmpty())
        EnumWindows(enumWindowsCb, reinterpret_cast<LPARAM>(&g_winCache.wins));
    g_winCache.at = now;
    return g_winCache;
}

QString windowTitleOf(HWND hwnd)
{
    // 非阻塞取标题：GetWindowTextW 对挂起（无响应）窗口会卡住整条枚举，
    // 必须改用 SendMessageTimeout + ABORTIFHUNG（与 enumWindowsCb 一致）。
    wchar_t buf[1024] = { 0 };
    DWORD_PTR n = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETTEXT, (WPARAM)(sizeof(buf) / sizeof(wchar_t)),
                             (LPARAM)buf, SMTO_ABORTIFHUNG, 60, &n) || n == 0)
        return QString();
    return QString::fromWCharArray(buf, (int)n);
}

/** 进程工作集内存（MB）。取不到返回 0 —— 权限不足是常态，不算失败。 */
int memMbOf(quint32 pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) return 0;
    PROCESS_MEMORY_COUNTERS pmc = {};
    pmc.cb = sizeof(pmc);
    const BOOL ok = GetProcessMemoryInfo(h, &pmc, pmc.cb);
    CloseHandle(h);
    return ok ? (int)(pmc.WorkingSetSize / (1024 * 1024)) : 0;
}
