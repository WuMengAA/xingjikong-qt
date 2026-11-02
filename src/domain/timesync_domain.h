#pragma once
// 2026-10-27：**时间同步域**（G 批次 · 灵动岛拓展第二项，整份文件新建，不进 main.cpp）。
//
// ── 为什么要有它（不是"锦上添花的校时"）──────────────────────────────
//   ClassIsland 的提醒准点，底座是 NTP：教室机长期不关机、主板 RTC 漂几分钟是常态，
//   而我们的「上课/下课提醒」是拿 QTime::currentTime() 跟课表边界比 —— 系统钟漂 3 分钟，
//   铃就早/晚 3 分钟，老师会认定"这软件不准"。所以先校时，再谈组件。
//
//   ⚠️ 与 ClassIsland 的差异（我们也要有）：**时间偏移**。学校铃声通常比标准时间早
//   几十秒到两分钟（打铃器自己走），老师需要"把课表按分钟整体挪一下"，但**不能改系统钟**
//   （改系统钟会连带改文件时间戳、证书校验、日志时序，副作用太大）。
//   ⇒ 本域把「NTP 校正量」与「人工偏移」**分开记**：
//        skewMs() = ntpDeltaMs（真时 − 系统钟）+ offsetMin × 60000
//      只影响**判定用的时间**（课表/提醒），系统钟一个字节都不动。
//
// ── 失败口径（与项目一贯"绝不假成功"）────────────────────────────────
//   NTP 不通（机房禁 UDP 123 很常见）⇒ ntpDeltaMs 归零 = **回落系统时间**，
//   状态里 ntpOk=false + error 写明原因；人工偏移照样生效（它不依赖网络）。
//   绝不留着上一次的旧 delta 假装还在校时（那会让钟"越校越偏"且没人发现）。
//
// ── 依赖边界 ────────────────────────────────────────────────────
//   不引第三方库：SNTP 是 48 字节定长报文，QUdpSocket 足够（决策 #4，见接续清单）。
//   不反向依赖 main.cpp：状态变化用 setOnChanged 注入回调（同 AlarmDomain/NotifyDomain 口径）。

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <QtGlobal>

namespace TimesyncDomain {

// 一次同步的结果 + 当前生效的偏移（time_status 指令原样回给管理端）
struct Status {
    bool    ntpOk      = false;   // 最近一次 NTP 是否成功（失败 = 已回落系统时间）
    qint64  deltaMs    = 0;       // NTP 差：真时 − 系统钟（负 = 系统钟快）
    int     offsetMin  = 0;       // 人工偏移（分钟，正 = 判定时间往前拨）
    qint64  lastSyncMs = 0;       // 最近一次**成功**同步的时刻（0 = 从未成功）
    QString server;               // 成功的服务器名
    QString error;                // 失败原因（成功后清空）
    QString lastAttempt;          // 最近一次尝试的服务器（诊断用，失败时也有值）
};

// 持久化文件（timesync.json）—— 只存**人工偏移**：NTP 差是易变量，重启后必须重新测，
// 存下来只会在下次开机时把旧误差当"权威"用。
void init(const QString &file);

// 启动周期同步：首轮 5s（等事件循环/网络就绪），之后每 30 分钟一次。可重复调用。
void start();

// 立即（异步）同步一次；正在同步中则忽略（防重入，返回 false 表示没发起）。
bool syncNow();

// 人工偏移（分钟）：落盘 + 立即生效 + 回调通知。范围 [-720, 720]，越界拒绝（返回 false）。
bool setOffsetMinutes(int minutes);
int  offsetMinutes();

// 当前总偏移（毫秒）= NTP 差 + 人工偏移。NTP 未成功时只剩人工偏移。
qint64 skewMs();

// 判定用时间（课表/提醒都该读这个，不要直接 QDateTime::currentDateTime()）
QDateTime now();
qint64    nowMs();

Status status();

// 偏移/同步结果变化时回调（main.cpp 注入 → 重推课表时钟的校正量 + 刷岛）
void setOnChanged(std::function<void()> cb);

} // namespace TimesyncDomain
