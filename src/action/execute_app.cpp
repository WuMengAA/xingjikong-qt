// 星集控被控端 · 应用与进程指令 —— executeAction 按域拆分 #1（2026-11-02 · 屎山整理）
//
// 负责：远程启动软件 / 进程列表与终止 / 日志尾 / 快捷方式候选扫描。
//
// 由 src/action/execute_action.cpp 原 2029 行单函数机械拆出：**块体逐字保留**，
// 唯一改动是每块**内部**首行插入 `handled = true;`（告诉调用方这条指令归本域管）。
// 函数名/参数/分支顺序/错误文案/回执字段全部不变，行为零变化。
//
// 拆分依据是 AGENTS.md §10.5 文件规模红线（.cpp ≤ 600 行）与 §10.6 禁上帝类：
// 原文件是一个函数里的 54 个指令分支，随产品演进步步膨胀，任何一处改动都要在
// 2000 行里定位。按**指令域**拆才对得上「一条指令归谁管」这个真实边界。

#include "execute_action_shared.h"

ExecOut executeAppProcessAction(const QString &action, const QJsonObject &params, bool &handled)
{
    ExecOut out;

    // 远程打开软件。白名单约束（照抄 Rust）：默认只允许教育/办公类，
    // 避免"任意程序可被远程拉起"变成攻击面。
    if (action == QStringLiteral("launch_app") || action == QStringLiteral("open_app")) {
        handled = true;
        const QString target = params.value(QStringLiteral("target")).toString().trimmed();
        if (target.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("参数缺失：需要 target（进程名或可执行路径）");
            return out;
        }
        // ⚠️ 2026-10-13 三端审计修复：原来白名单用 `target.toLower().contains(item)` ——
        //    **子串**匹配。于是 `incoming\chrome-setup.exe`（file_push 能把它落到收件目录，
        //    见 (*g_actionDeps->recvDir)）里"含有 chrome"就被放行 ⇒ 配合 file_push = 免确认的任意代码执行。
        //    改成**两道闸**（缺一不可）：
        //      ① 名字必须**基名精确**命中（剥路径 + 去 .exe 后缀后全等）；
        //      ② 带路径的 target 必须落在**可信安装目录**内 —— 防"把恶意二进制直接改名成
        //         chrome.exe 再放进收件目录"绕过 ①（file_push 不禁止叫 chrome.exe）。
        auto normExeName = [](const QString &s) {
            QString n = QFileInfo(s.trimmed()).fileName().toLower();
            if (n.endsWith(QLatin1String(".exe"))) n.chop(4);
            return n;
        };
        const QString tName = normExeName(target);

        // ① 名字闸：内置名单（去 .exe 后缀，与归一化对齐）+ 管理员配的放行名单
        bool allowed = false;
        const QString allow = qEnvironmentVariable("STELARITH_AGENT_LAUNCH_ALLOWLIST").toLower();
        const QStringList items = allow.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &item : items)
            if (!item.trimmed().isEmpty() && normExeName(item) == tName) { allowed = true; break; }
        static const char *const kBuiltinSafe[] = {
            "classisland", "xingjikong", "explorer", "msedge", "chrome", "firefox",
            "wechat", "wps", "word", "excel", "powerpnt", "notepad", "cmd", nullptr
        };
        if (!allowed)
            for (const char *const *p = kBuiltinSafe; *p; ++p)
                if (tName == QLatin1String(*p)) { allowed = true; break; }
        if (!allowed) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("launch_app: 「%1」不在内置安全名单（classisland/xingjikong/浏览器/Office/记事本…），"
                                       "被拒绝。可配置 STELARITH_AGENT_LAUNCH_ALLOWLIST 放行。").arg(target);
            return out;
        }

        // ② 目录闸：带路径的 target 必须落在可信安装目录（否则"改名成合法名"就能绕过 ①）。
        //    裸名字（如 "notepad"）不含分隔符 → 交给 ShellExecuteW 走 App Paths/PATH 系统级解析，安全。
        if (target.contains(QLatin1Char('\\')) || target.contains(QLatin1Char('/'))) {
            const QString abs = QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath()).toLower();
            QStringList roots;
            const auto addRoot = [&roots](const QString &p) {
                if (!p.trimmed().isEmpty())
                    roots << (QDir::toNativeSeparators(QDir::cleanPath(p)).toLower() + QLatin1Char('\\'));
            };
            addRoot(qEnvironmentVariable("ProgramFiles"));
            addRoot(qEnvironmentVariable("ProgramFiles(x86)"));
            addRoot(qEnvironmentVariable("ProgramW6432"));
            addRoot(qEnvironmentVariable("SystemRoot"));                        // 含 System32 / SysWOW64
            addRoot(qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs"));  // 用户级安装
            // 逃生口：非标准安装位置（如 D:\Programs）由管理员显式追加目录白名单
            for (const QString &extra : qEnvironmentVariable("STELARITH_AGENT_LAUNCH_DIRS")
                                            .split(QLatin1Char(';'), Qt::SkipEmptyParts))
                addRoot(extra);
            bool inTrusted = false;
            for (const QString &root : roots) if (abs.startsWith(root)) { inTrusted = true; break; }
            if (!inTrusted) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("launch_app: 「%1」不在可信安装目录（Program Files / Windows / 用户程序目录），"
                                           "已拒绝。非标准安装位置可配置 STELARITH_AGENT_LAUNCH_DIRS 追加。").arg(target);
                qWarning("[agent-qt] launch_app 拒绝：%s 不在可信目录（防改名绕过）", qPrintable(target));
                return out;
            }
        }
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(target.utf16()),
                                          nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) <= 32) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("ShellExecuteW 失败，错误码 %1").arg((int)reinterpret_cast<INT_PTR>(r));
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("target"), target);
        return out;
    }

    // 软件运行查看：只列「有主窗口标题」的进程 —— 老师要看的是"教室机上开着什么"，
    // 不是几百个 svchost。只读快照 + 最小权限查询，不提权、不注入。
    if (action == QStringLiteral("process_list")) {
        handled = true;
        const int lim = params.value(QStringLiteral("limit")).toInt(kProcessListLimit);
        const int cap = lim > 0 ? qMin(lim, 500) : kProcessListLimit;
        const auto &snap = g_actionDeps->visibleWindowsCached();
        if (snap.names.isEmpty()) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("无法获取进程快照"); return out; }
        const QHash<quint32, QString> &names = snap.names;
        const auto &wins = snap.wins;
        QJsonArray items;
        bool truncated = false;
        for (const auto &w : wins) {
            const auto it = names.constFind(w.second);
            if (it == names.constEnd() || it->isEmpty()) continue;
            if ((int)items.size() >= cap) { truncated = true; break; }
            // 2026-10-26（P3）：白名单 = 永不禁用且**不显示在列表**（保护系统稳定 + 免误关）
            if (g_actionDeps->softHit((*g_actionDeps->soft).whitelist, *it)) continue;
            QJsonObject o;
            o.insert(QStringLiteral("pid"), (int)w.second);
            o.insert(QStringLiteral("name"), *it);
            o.insert(QStringLiteral("title"), g_actionDeps->windowTitleOf(w.first));
            o.insert(QStringLiteral("mem_mb"), g_actionDeps->memMbOf(w.second));
            const QString prot = g_actionDeps->protectedCategory(*it);
            if (!prot.isEmpty()) o.insert(QStringLiteral("protected"), prot);
            // 黑名单 → 标红；已冻结 → 标冻结（界面置灰"解冻"）
            if (g_actionDeps->softHit((*g_actionDeps->soft).blacklist, *it)) o.insert(QStringLiteral("blacklisted"), true);
            if ((*g_actionDeps->frozenPids).contains((quint32)w.second)) o.insert(QStringLiteral("frozen"), true);
            // 2026-10-11 需求#6：管理端进程列表要显示图标。图标只能这边取
            // （管理端没有这台机器上的 exe），取不到就留空 —— 那边画首字方块兜底。
            const QString ic = g_actionDeps->windowIconBase64(w.first);
            if (!ic.isEmpty()) o.insert(QStringLiteral("icon"), ic);
            items.append(o);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), (int)items.size());
        out.data.insert(QStringLiteral("truncated"), truncated);
        return out;
    }

    // 软件控制：硬禁止名单第一关（命中**连 WM_CLOSE 都不发**）→ 温和关闭（发 WM_CLOSE，软件能存盘）
    // → 最多等 5 秒 → 仍未退出才 TerminateProcess。
    if (action == QStringLiteral("process_stop")) {
        handled = true;
        const QJsonValue jpid = params.value(QStringLiteral("pid"));
        const int pid = jpid.isDouble() ? jpid.toInt()
                                        : jpid.toString().trimmed().toInt();
        if (pid <= 0) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("参数缺失：需要 pid"); return out; }
        const QHash<quint32, QString> names = g_actionDeps->snapshotProcessNames();
        const QString exe = names.value((quint32)pid);
        if (exe.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 pid=%1 对应的进程（可能已退出）").arg(pid);
            return out;
        }
        const QString prot = g_actionDeps->protectedCategory(exe);
        if (!prot.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("拒绝结束 %1（pid %2）：命中硬禁止名单「%3」").arg(exe).arg(pid).arg(prot);
            return out;
        }
        // ⚠️ 2026-10-13：名字名单之外**再过一次内核闸门**。
        //    名单不可能穷举 —— MemCompression / Registry / Secure System / LogonUI / 任意未列名的
        //    session-0 服务宿主都不在 kProtectedSystem 里，而名单曾是这条指令**唯一**的保护。
        //    这与 2026-10-09 学校机房"反复重启"事故（名单漏项 → TerminateProcess 掉 critical 进程
        //    → CRITICAL_PROCESS_DIED 0xEF）是同一类机理，只是换到了 process_stop 这条路径上；
        //    本端以 Highest 权限运行，杀掉 MemCompression 就是同一个 0xEF。
        //    闸门与 exam_mode 共用 proc_guard（问内核，不猜名字；查不到一律当受保护）。
        const QString blocked = terminateBlockedReason((DWORD)pid, prot);
        if (!blocked.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("拒绝结束 %1（pid %2）：%3").arg(exe).arg(pid).arg(blocked);
            return out;
        }
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
        if (!h) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("OpenProcess 失败（错误码 %1），可能权限不足或进程受保护").arg((int)GetLastError());
            return out;
        }
        int closed = 0;
        QVector<QPair<HWND, quint32>> wins;
        EnumWindows(g_actionDeps->enumWindowsCb, reinterpret_cast<LPARAM>(&wins));
        for (const auto &w : wins)
            if (w.second == (quint32)pid) { PostMessageW(w.first, WM_CLOSE, 0, 0); closed++; }
        if (closed > 0 && WaitForSingleObject(h, 5000) == WAIT_OBJECT_0) {
            CloseHandle(h);
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("pid"), pid);
            out.data.insert(QStringLiteral("name"), exe);
            out.data.insert(QStringLiteral("how"), QStringLiteral("wm_close"));
            out.data.insert(QStringLiteral("detail"), QStringLiteral("已温和关闭（WM_CLOSE，主窗口 %1 个）").arg(closed));
            return out;
        }
        const BOOL killed = TerminateProcess(h, 1);
        const DWORD lastErr = GetLastError();
        CloseHandle(h);
        if (killed) {
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("pid"), pid);
            out.data.insert(QStringLiteral("name"), exe);
            out.data.insert(QStringLiteral("how"), QStringLiteral("terminate"));
            out.data.insert(QStringLiteral("detail"),
                            closed > 0 ? QStringLiteral("温和关闭 5 秒未生效，已强制结束")
                                       : QStringLiteral("该进程没有主窗口，直接强制结束"));
            return out;
        }
        out.result = QStringLiteral("failed");
        out.error = QStringLiteral("TerminateProcess 失败（错误码 %1）").arg((int)lastErr);
        return out;
    }

    // 本机日志尾部：装机后没有 stdout，出故障只能靠它
    if (action == QStringLiteral("log_tail")) {
        handled = true;
        if (g_logPath.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("本端未启用日志文件");
            return out;
        }
        QFile f(g_logPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("读取日志失败：%1").arg(g_logPath);
            return out;
        }
        int want = params.value(QStringLiteral("lines")).toInt(200);
        if (want <= 0) want = 200;
        want = qMin(want, 2000);
        const QStringList all = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        f.close();
        const int from = qMax(0, (int)all.size() - want);
        QJsonArray lines;
        for (int i = from; i < (int)all.size(); ++i) lines.append(all.at(i));
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("path"), QDir::toNativeSeparators(g_logPath));
        out.data.insert(QStringLiteral("lines"), lines);
        out.data.insert(QStringLiteral("count"), (int)lines.size());
        return out;
    }

    // 快捷方式候选：管理端要「从这台机器上挑」，所以这里返回**可选来源**，而不是预置名单。
    // 两类：① 正在运行且有主窗口的程序（同一 exe 只出一条）② 桌面上的文件与文件夹。
    // 纪律：**不把集控自己列进去**（否则会变成"自己开自己"）。
    if (action == QStringLiteral("list_shortcut_candidates")) {
        handled = true;
        QJsonArray apps;
        {
            const auto &snap = g_actionDeps->visibleWindowsCached();
            const QHash<quint32, QString> &names = snap.names;
            const auto &wins = snap.wins;
            QSet<QString> seen;
            for (const auto &w : wins) {
                const auto it = names.constFind(w.second);
                if (it == names.constEnd() || it->isEmpty()) continue;
                const QString exe = *it;
                const QString low = exe.toLower();
                if (low.startsWith(QStringLiteral("stelarith")) || low.startsWith(QStringLiteral("xingjikong")))
                    continue;
                if (seen.contains(low)) continue;
                seen.insert(low);
                QJsonObject o;
                o.insert(QStringLiteral("kind"), QStringLiteral("app"));
                o.insert(QStringLiteral("target"), exe);
                o.insert(QStringLiteral("title"), g_actionDeps->windowTitleOf(w.first));
                o.insert(QStringLiteral("pid"), (int)w.second);
                apps.append(o);
            }
        }

        QJsonArray files;
        const QString deskPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        {
            QDir d(deskPath);
            const QFileInfoList list =
                d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            int n = 0;
            for (const QFileInfo &fi : list) {
                if (n++ >= 60) break;   // 桌面上东西多时截断，别把回执撑爆
                QJsonObject o;
                o.insert(QStringLiteral("kind"), fi.isDir() ? QStringLiteral("dir") : QStringLiteral("file"));
                o.insert(QStringLiteral("target"), QDir::toNativeSeparators(fi.absoluteFilePath()));
                o.insert(QStringLiteral("title"), fi.fileName());
                files.append(o);
            }
        }

        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("apps"), apps);
        out.data.insert(QStringLiteral("desktop"), files);
        out.data.insert(QStringLiteral("desktopPath"), QDir::toNativeSeparators(deskPath));
        return out;
    }

    return out;   // handled 仍为 false：这条指令不归本域，调用方继续往下问
}
