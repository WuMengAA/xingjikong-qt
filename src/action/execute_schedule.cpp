// 星集控被控端 · 调度与软控指令 —— executeAction 按域拆分 #1（2026-11-02 · 屎山整理）
//
// 负责：定时关机重启 / 任务取消 / 软控黑白名单与冻结、断网。
//
// 由 src/action/execute_action.cpp 原 2029 行单函数机械拆出：**块体逐字保留**，
// 唯一改动是每块**内部**首行插入 `handled = true;`（告诉调用方这条指令归本域管）。
// 函数名/参数/分支顺序/错误文案/回执字段全部不变，行为零变化。
//
// 拆分依据是 AGENTS.md §10.5 文件规模红线（.cpp ≤ 600 行）与 §10.6 禁上帝类：
// 原文件是一个函数里的 54 个指令分支，随产品演进步步膨胀，任何一处改动都要在
// 2000 行里定位。按**指令域**拆才对得上「一条指令归谁管」这个真实边界。

#include "execute_action_shared.h"

ExecOut executeScheduleAction(const QString &action, const QJsonObject &params, bool &handled)
{
    ExecOut out;

    // ── 定时关机三件套 ──
    // 参数：at（ISO 8601 绝对时间）或 delay_sec（秒），二选一；what：shutdown|reboot（默认 shutdown）
    // 落盘到 schedules.json，进程重启会自动重挂——所以"定时"不是靠进程活着撑着的假定时。
    if (action == QStringLiteral("schedule_shutdown") || action == QStringLiteral("schedule_reboot")) {
        handled = true;
        const QString what = (action == QStringLiteral("schedule_reboot"))
                                 ? QStringLiteral("reboot") : QStringLiteral("shutdown");
        qint64 at = 0;
        const QString atStr = params.value(QStringLiteral("at")).toString().trimmed();
        const QJsonValue dsec = params.value(QStringLiteral("delay_sec"));
        if (!atStr.isEmpty()) {
            // 兼容带 Z 与不带 Z：QT 的 fromString 对 T 分隔 + Z 都能吃
            QDateTime dt = QDateTime::fromString(atStr, Qt::ISODateWithMs);
            if (dt.date() == QDate()) dt = QDateTime::fromString(atStr, Qt::ISODate);
            if (!dt.isValid()) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("schedule: at 不是合法时间（要 ISO 8601，如 2026-10-03T22:00:00）");
                return out;
            }
            at = dt.toMSecsSinceEpoch();
        } else if (dsec.isDouble()) {
            // 数字型：直接取。此前这里把 `okv` 留在 false（只在字符串分支赋过值）
            // → 条件 `!okv` 恒真 → **传数字的 delay_sec 一律被拒**，只有字符串型才能安排成功。
            // 数字是下发侧最自然的写法（JSON number），这个洞等于"定时开关机"用不了。
            const int sec = dsec.toInt();
            if (sec <= 0) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("schedule: delay_sec 必须 > 0 的整数秒");
                return out;
            }
            at = QDateTime::currentMSecsSinceEpoch() + (qint64)sec * 1000;
        } else if (dsec.isString() && !dsec.toString().trimmed().isEmpty()) {
            bool okv = false;
            const int sec = dsec.toString().trimmed().toInt(&okv);
            if (!okv || sec <= 0) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("schedule: delay_sec 必须 > 0 的整数秒");
                return out;
            }
            at = QDateTime::currentMSecsSinceEpoch() + (qint64)sec * 1000;
        } else {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule: 需要 at（ISO 时间）或 delay_sec（秒）其一");
            return out;
        }
        if (at <= QDateTime::currentMSecsSinceEpoch()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule: 时间已在过去，拒绝安排");
            return out;
        }

        // 同一台机只允许一条待执行任务：教室机上是"排一个就排一个"，多个排队语义复杂且易误伤
        QVector<SchedTask> cur = g_actionDeps->loadSchedules();
        QString replacedId;
        for (int i = 0; i < cur.size(); ++i) {
            const QString oldId = cur.at(i).id;
            // 同步停掉旧定时器，否则会出现"文件里没记录但定时器仍在跑"——到点照样关机
            const auto ti = (*g_actionDeps->schedTimerById).constFind(oldId);
            if (ti != (*g_actionDeps->schedTimerById).constEnd() && ti.value()) {
                ti.value()->stop();
                delete ti.value();
                (*g_actionDeps->schedTimerById).erase(ti);
            }
            replacedId = oldId;
            cur.removeAt(i);
            break;
        }

        SchedTask nt;
        nt.id = QStringLiteral("sch-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        nt.what = what;
        nt.at = at;
        nt.createdMs = QDateTime::currentMSecsSinceEpoch();
        cur.append(nt);
        g_actionDeps->saveSchedules(cur);
        g_actionDeps->armScheduleTimer(nt, (int)qMin<qint64>(at - QDateTime::currentMSecsSinceEpoch(), 2147483647LL));

        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), nt.id);
        out.data.insert(QStringLiteral("what"), what);
        out.data.insert(QStringLiteral("at"), QDateTime::fromMSecsSinceEpoch(at).toString(Qt::ISODateWithMs));
        out.data.insert(QStringLiteral("inMs"), at - QDateTime::currentMSecsSinceEpoch());
        out.data.insert(QStringLiteral("persisted"), true);
        out.data.insert(QStringLiteral("fireAllowed"), g_actionDeps->schedFireAllowed());
        // 明说这条定时**现在就不会真关机器**（未装机开启时）——“报了安排成功”和“真的会关”是两件事
        if (!g_actionDeps->schedFireAllowed())
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("到期不会真执行（STE_QT_ALLOW_SCHED_FIRE≠1），装机时需在 agent.env 设为 1"));
        if (!replacedId.isEmpty()) out.data.insert(QStringLiteral("replaced"), replacedId);
        qInfo("[agent-qt] ⏲ 已安排 %s @ %s（%lld 秒后）", qPrintable(what),
              qPrintable(out.data.value(QStringLiteral("at")).toString()),
              (at - QDateTime::currentMSecsSinceEpoch()) / 1000);
        return out;
    }

    // 列出所有待执行定时任务（管理端"看计划"用）。已到期未执行的不会出现在这里。
    if (action == QStringLiteral("list_schedules")) {
        handled = true;
        const QVector<SchedTask> tasks = g_actionDeps->loadSchedules();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonArray items;
        int pending = 0;
        for (const auto &t : tasks) {
            if (t.at <= now) continue;
            QJsonObject o;
            o.insert(QStringLiteral("id"), t.id);
            o.insert(QStringLiteral("what"), t.what);
            o.insert(QStringLiteral("at"), QDateTime::fromMSecsSinceEpoch(t.at).toString(Qt::ISODateWithMs));
            o.insert(QStringLiteral("inMs"), t.at - now);
            o.insert(QStringLiteral("inSec"), (t.at - now) / 1000);
            o.insert(QStringLiteral("createdAt"),
                     QDateTime::fromMSecsSinceEpoch(t.createdMs).toString(Qt::ISODateWithMs));
            items.append(o);
            ++pending;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), pending);
        out.data.insert(QStringLiteral("file"), QDir::toNativeSeparators((*g_actionDeps->schedFile)));
        return out;
    }

    // ── 软件管理（2026-10-26 · P3 产品笔记）─────────────────────────────────
    // 读策略：黑白名单 + 守护开关
    if (action == QStringLiteral("soft_policy_get")) {
        handled = true;
        QJsonArray bl, wl;
        for (const QString &s : (*g_actionDeps->soft).blacklist) bl.append(s);
        for (const QString &s : (*g_actionDeps->soft).whitelist) wl.append(s);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("blacklist"), bl);
        out.data.insert(QStringLiteral("whitelist"), wl);
        out.data.insert(QStringLiteral("denyRun"), (*g_actionDeps->soft).denyRun);
        out.data.insert(QStringLiteral("file"), QDir::toNativeSeparators((*g_actionDeps->softPolicyFile)));
        return out;
    }

    // 写策略：{ blacklist?: [...], whitelist?: [...], denyRun?: bool }
    if (action == QStringLiteral("soft_policy_set")) {
        handled = true;
        const QJsonValue jb = params.value(QStringLiteral("blacklist"));
        const QJsonValue jw = params.value(QStringLiteral("whitelist"));
        if (jb.isArray()) {
            QStringList nl;
            for (const auto &v : jb.toArray()) {
                const QString s = v.toString().trimmed();
                // 护栏：不许把受保护的进程塞进黑名单（否则守护会去杀系统/安全软件）
                if (!s.isEmpty() && g_actionDeps->protectedCategory(s).isEmpty() && !nl.contains(s)) nl.append(s);
            }
            (*g_actionDeps->soft).blacklist = nl;
        }
        if (jw.isArray()) {
            QStringList nl;
            for (const auto &v : jw.toArray()) {
                const QString s = v.toString().trimmed();
                if (!s.isEmpty() && !nl.contains(s)) nl.append(s);
            }
            (*g_actionDeps->soft).whitelist = nl;
        }
        const QJsonValue jd = params.value(QStringLiteral("denyRun"));
        if (jd.isBool()) (*g_actionDeps->soft).denyRun = jd.toBool();
        g_actionDeps->saveSoftPolicy();
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("blackCount"), (*g_actionDeps->soft).blacklist.size());
        out.data.insert(QStringLiteral("whiteCount"), (*g_actionDeps->soft).whitelist.size());
        out.data.insert(QStringLiteral("denyRun"), (*g_actionDeps->soft).denyRun);
        return out;
    }

    // 冻结 / 解冻进程（仅常用应用；受保护一律拒绝）
    if (action == QStringLiteral("soft_freeze") || action == QStringLiteral("soft_unfreeze")) {
        handled = true;
        const bool freeze = (action == QStringLiteral("soft_freeze"));
        const QJsonValue jpid = params.value(QStringLiteral("pid"));
        const int pid = jpid.isDouble() ? jpid.toInt() : jpid.toString().trimmed().toInt();
        if (pid <= 0) { out.result = QStringLiteral("failed"); out.error = QStringLiteral("需要 pid"); return out; }
        const QString exe = g_actionDeps->snapshotProcessNames().value((quint32)pid);
        if (exe.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 pid=%1 的进程（可能已退出）").arg(pid);
            return out;
        }
        const QString prot = g_actionDeps->protectedCategory(exe);
        if (!prot.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("不许对受保护进程操作：%1（%2）").arg(exe, prot);
            return out;
        }
        const bool ok = g_actionDeps->setProcessFrozen((quint32)pid, freeze);
        if (!ok) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("%1 失败：%2（ntdll 接口不可用或权限不足）").arg(freeze ? QStringLiteral("冻结") : QStringLiteral("解冻"), exe);
            return out;
        }
        if (freeze) (*g_actionDeps->frozenPids).insert((quint32)pid);
        else (*g_actionDeps->frozenPids).remove((quint32)pid);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("pid"), pid);
        out.data.insert(QStringLiteral("frozen"), freeze);
        return out;
    }

    // 去网 / 恢复（防火墙出站规则；需管理员，失败如实报）
    if (action == QStringLiteral("soft_deny_net") || action == QStringLiteral("soft_restore_net")) {
        handled = true;
        const bool block = (action == QStringLiteral("soft_deny_net"));
        const QJsonValue jpid = params.value(QStringLiteral("pid"));
        const int pid = jpid.isDouble() ? jpid.toInt() : jpid.toString().trimmed().toInt();
        const QString path = (pid > 0) ? g_actionDeps->processPathOf((quint32)pid)
                                       : params.value(QStringLiteral("path")).toString().trimmed();
        if (path.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("需要 pid 或 path（去网按可执行文件路径加规则）");
            return out;
        }
        const QString exe = QFileInfo(path).fileName();
        const QString prot = g_actionDeps->protectedCategory(exe);
        if (!prot.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("不许对受保护程序去网：%1（%2）").arg(exe, prot);
            return out;
        }
        const bool ok = g_actionDeps->setNetBlocked(path, block);
        out.result = ok ? QStringLiteral("done") : QStringLiteral("failed");
        if (!ok)
            out.error = QStringLiteral("%1 失败：netsh 非零退出（多半是**没有管理员权限**）").arg(block ? QStringLiteral("去网") : QStringLiteral("恢复网络"));
        else
            out.data.insert(QStringLiteral("path"), path);
        return out;
    }

    // 按 id 取消。找不到就如实报 failed（不算"静默成功"）。
    if (action == QStringLiteral("cancel_schedule")) {
        handled = true;
        const QString id = params.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("cancel_schedule: 需要 id");
            return out;
        }
        QVector<SchedTask> cur = g_actionDeps->loadSchedules();
        int hit = -1;
        for (int i = 0; i < cur.size(); ++i)
            if (cur.at(i).id == id) { hit = i; break; }
        if (hit < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 id=%1 的定时任务（可能已执行或已取消）").arg(id);
            return out;
        }
        // 先停定时器再删记录：顺序反了会出现"记录没了但定时器还在跑"，到点照样关机
        const auto ti = (*g_actionDeps->schedTimerById).constFind(id);
        if (ti != (*g_actionDeps->schedTimerById).constEnd() && ti.value()) {
            ti.value()->stop();
            delete ti.value();
            (*g_actionDeps->schedTimerById).erase(ti);
        }
        cur.removeAt(hit);
        g_actionDeps->saveSchedules(cur);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), id);
        out.data.insert(QStringLiteral("cancelled"), true);
        out.data.insert(QStringLiteral("remaining"), (int)cur.size());
        return out;
    }

    return out;   // handled 仍为 false：这条指令不归本域，调用方继续往下问
}
