#pragma once
#include <QString>
#include <QJsonArray>

// P3 长期记录查询/展示（2026-10-30 main 拆分 #6b 迁出）。
// readJsonlDir / openFolder / openNoiseLogDialog / openStudyLogDialog /
// buildNoiseLogText / buildStudyLogText 从 main.cpp 迁出；dataRoot() 复用 p3_log（#6a）。
// openFolder 也被 main 侧 openPlayScheduleDialog 与 AgentUi（打开记录文件夹按钮）调用，
// 故保持公开 API。

/**
 * 读记录目录的结果，**带诊断**。
 *
 * ── 为什么需要它（2026-10-31 修的真 bug）────────────────────────────────
 * 旧 readJsonlDir 把三种完全不同的情况折叠成同一个空数组：
 *   ① 目录不存在        = 还没启用过功能   → 界面显示「还没有记录」✓
 *   ② 文件打不开        = 故障（权限/占用） → 界面仍显示「还没有记录」✗
 *   ③ 内容全是坏 JSON   = 数据损坏          → 界面仍显示「还没有记录」✗
 * ②③ 下用户的记录**可能完好躺在磁盘上**，界面却告诉他"没有" ——
 * 让"有记录"这个状态本身变成谎话，用户既不会去修也不会去找回。
 * 这就是 AppErrorState 存在的理由：错误态 ≠ 空态。
 *
 * ── 用法 ────────────────────────────────────────────────────────────────
 *   拿到 status == Ok         → 走空态或正常列表（records 为空 = 真的没有）
 *   拿到其它三种             → 走错误态：reason 说为什么，impact 说"还能干什么"
 */
struct P3ReadResult
{
    enum Status {
        Ok = 0,        ///< 读成功（records 可能为空 = 确实还没数据）
        Unreadable,    ///< 目录/文件存在但打不开（权限、被占用）—— 故障
        Corrupt,       ///< 一行都没读出有效 JSON，但确有内容 —— 数据损坏
        Missing,       ///< 记录根目录本身不可用（AppData 不可写等）—— 环境故障
    };

    Status status = Ok;
    QJsonArray records;      ///< 解析出的记录（Ok / Corrupt 时可能非空）
    int badLines = 0;        ///< 解析失败的行数（非 0 时说明有部分损坏）
    int skippedFiles = 0;    ///< 打不开而被跳过的文件数
    int readableFiles = 0;   ///< 成功打开的文件数
    QString reason;          ///< 人话原因（错误态上屏用；Ok 时为空）
    QString impact;          ///< 人话影响（错误态上屏用；Ok 时为空）

    bool isOk() const { return status == Ok; }
};

/**
 * 读记录目录并给出诊断。records 口径与旧 readJsonlDir 完全一致。
 * @param host   数据根目录（正常传 dataRoot()；探针传临时目录以便造场景）
 * @param subdir 子目录名（"noise_log" / "study_log"）
 */
P3ReadResult readJsonlDirWithDiag(const QString &host, const QString &subdir);

// ── 旧接口（保持原签名与行为，内部改走 readJsonlDirWithDiag）─────────────
// 保留它是因为 buildNoiseLogText / buildStudyLogText（托盘侧历史纯文本口径）
// 与 main.cpp 里若干调用点都按原样在用；改签名会牵连 agent_panel.cpp
// （该文件 645 行 / 红线 600，一行都不能加）。
QJsonArray readJsonlDir(const QString &subdir);
QJsonArray readJsonlDir(const QString &host, const QString &subdir);

void openFolder(const QString &subdir);
void openNoiseLogDialog();
void openStudyLogDialog();
QString buildNoiseLogText();
QString buildStudyLogText();