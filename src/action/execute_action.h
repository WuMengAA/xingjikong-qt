#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QPair>
#include <windows.h>   // HWND（WinCache 字段；main.cpp 亦已包含）

// ── 播放时段管理 / 调度 / 软控 / 窗口快照：executeAction 依赖的匿名域模型 ──
// main 拆分 #10（2026-10-30）。原定义于 main.cpp 匿名域（内部链接，跨不了 TU），
// 迁至本头让 execute_action.cpp 与 main.cpp 共用同一类型。
//   SchedTask：定时关机/重启任务（内存态，与 JSON 一一对应），at 是绝对毫秒时间戳。
//   SoftPolicy：软控黑白名单（考试/管控场景）。
//   WinCache：进程主窗口快照缓存（names 按 pid 存标题，wins 存 (hwnd,pid) 对）。
struct SchedTask { QString id; QString what; qint64 at = 0; qint64 createdMs = 0; };
struct SoftPolicy {
    QStringList blacklist;   // 禁止运行（denyRun 开启时轮询终止）+ 界面标红
    QStringList whitelist;   // 永不禁用；**不显示在进程列表**
    bool denyRun = false;    // 黑名单守护开关（考试/管控场景）
};
struct WinCache {
    qint64 at = 0;
    QHash<quint32, QString> names;
    QVector<QPair<HWND, quint32>> wins;
};
constexpr int kProcessListLimit = 50;    // process_list 默认条数（与 Rust DEFAULT_LIMIT 同量级）

class QTimer;       // g_schedTimerById 值类型（指针）
class QFile;        // g_fileRecvFile（指针）
class NoiseMonitor; // g_noise（指针）
class AutomationEngine; // g_automationEngine（指针）

// executeAction 依赖的 main.cpp 匿名域符号（内部链接）经此上下文访问（方案 B）。
// main() 负责填充（同一 TU 才能取匿名域符号地址）；execute_action.cpp 只经
// g_actionDeps 调用，不改函数名/参数、语义等价。函数指针保持非空由 main() 填充点保证。
//
// 统一替换规则（execute_action.cpp 内）：
//   · 全局量 g_X        → (*g_actionDeps->memberX)   —— 成员 = 指向真实变量的指针
//   · 匿名域函数 fn(...) → g_actionDeps->fn(...)
struct ActionDeps {
    // ── 匿名域全局量（19 个）：成员 = 指向真实变量的指针（可读写）──
    qint64 *fileRecvBytes;          // 累计已写入字节（file_done 时与声明值比对）
    QFile **fileRecvFile;           // 接收文件句柄（WriteOnly）
    qint64 *fileRecvLastMs;         // 接收会话最后一次有动静的时刻（毫秒）
    QString *fileRecvName;          // 接收文件名（进度卡显示用）
    int *fileRecvNext;              // 下一个期望的分片序号（必须按序）
    QString *fileRecvTarget;        // 目标文件绝对路径（file_push 时定下）
    qint64 *fileRecvTotal;          // 声明的总字节（file_done 比对 + 进度条分母）
    QSet<quint32> *frozenPids;      // 已冻结的 pid（避免重复冻结/误解冻）
    QString *lastInstrId;           // 当前这条 instruction 的 id
    NoiseMonitor **noise;           // 噪音采集器（懒加载单例）
    QString *recvDir;               // file_push 落盘目录
    QString *schedFile;             // 调度持久化文件
    QHash<QString, QTimer *> *schedTimerById; // 任务 id → 定时器
    SoftPolicy *soft;               // 软控黑白名单
    QString *softPolicyFile;        // soft-manage.json 路径
    QSet<QString> *terminalDone;    // 已执行过的指令 id（内存态）
    AutomationEngine **automationEngine; // 规则引擎单例
    bool *studyOn;                  // 自习会话是否开启
    qint64 *studyStartTs;           // 本次自习开始时间（长期记录算时长用）

    // ── 匿名域函数（20 个）──
    void (*armScheduleTimer)(const SchedTask &t, int delayMs);
    bool (*enableShutdownPriv)();
    QString (*humanBytes)(qint64 n);
    QVector<SchedTask> (*loadSchedules)();
    void (*markTerminalDone)(const QString &id);
    int (*memMbOf)(quint32 pid);
    bool (*pathInside)(const QString &root, const QString &target);
    QString (*processPathOf)(quint32 pid);
    QString (*protectedCategory)(const QString &exeName);
    void (*saveSchedules)(const QVector<SchedTask> &tasks);
    void (*saveSoftPolicy)();
    bool (*schedFireAllowed)();
    bool (*setNetBlocked)(const QString &exePath, bool block);
    bool (*setProcessFrozen)(quint32 pid, bool frozen);
    bool (*softHit)(const QStringList &list, const QString &exeName);
    QString (*windowTitleOf)(HWND hwnd);
    void (*ensureNoise)();
    const WinCache &(*visibleWindowsCached)();
    QHash<quint32, QString> (*snapshotProcessNames)();
    QString (*windowIconBase64)(HWND hwnd);   // 文件作用域 static
    BOOL (__stdcall *enumWindowsCb)(HWND hwnd, LPARAM lp); // 枚举主窗口回调
};

// 定义于 main.cpp（文件作用域外部链接）；main() 启动即填充，任何 executeAction 调用前必已就绪。
extern ActionDeps *g_actionDeps;

// 由 execute_action.cpp 提供。main.cpp 的 handleControlText 等经此下发指令。
class ExecOut;
ExecOut executeAction(const QString &action, const QJsonObject &params);
