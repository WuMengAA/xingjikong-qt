#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>

class ScheduleClock;

// 岛组件面板数据域（2026-10-27 · G4/G5/G6 · 对标 ClassIsland 的"组件"）
//
// ── 为什么要独立成域（而不是塞回 main.cpp）─────────────────────────────
//  G 批次要补的是 ClassIsland 那套**组件**：日期 / 课表 / 倒计时。
//  三者的共同点是"数据在 C++、画在 QML"，但数据来源各异（系统时间 + NTP 校正、
//  课表档案、目标时刻推算）；把这些组装逻辑摊在 main.cpp 的指令分支里，
//  下一批再加"天气/课程进度"就会重演 main.cpp 膨胀的老路（第八刀正在拆它）。
//  所以：**域负责算数据，IslandOverlay 负责显示，QML 负责排版**，三层不越界。
//
// ── 与通知的分工（产品口径）──────────────────────────────────────────
//  通知 = 事件：来了一条，看完就走（6 秒自动收）。
//  面板 = 看板：要停下来看清、反复看（今天第几节、还有多久下课）。
//  所以面板走 IslandOverlay::showPanel（常驻展开区），不走 showIsland 的短时胶囊。
//
// ── 时间口径（重要）────────────────────────────────────────────────
//  所有"现在几点"一律取 TimesyncDomain::now()（系统钟 + NTP 差 + 人工偏移），
//  绝不直接用 QDateTime::currentDateTime() —— 否则面板显示的时间会和
//  "上下课提醒按哪个时间判定"分叉，学校铃声一偏移就自相矛盾。
namespace PanelDomain {

// 支持的面（与 QML island/IslandPanelFace.qml 的 face 字符串逐一对应）。
// 新增一面 = 这里加一项 + IslandPanelFace 加一个 Loader 分支 + 一枚独立 QML。
QStringList faces();

// 注入课表源（G5 用）。未注入时 timetable / 默认 countdown 会**如实报错**，
// 不编造"今天没有课"（那会把"没接上"伪装成"真没课"）。
void init(ScheduleClock *clock);

// 组装结果。data 同时用于：① 交给 QML 渲染；② 原样进指令回执（端到端可断言）。
struct Result {
    bool ok = false;
    QString error;
    QJsonObject data;
};

// 组装某个面的数据（纯计算，不碰 UI —— 探针/诊断可单独调用）。
// params 各面要求：
//   date      （无）
//   timetable （无）
//   countdown to="HH:MM" | ISO8601（可空 ⇒ 用课表边界）、label、title（可空）
Result build(const QString &face, const QJsonObject &params);

// 组装 + 上岛。durationMs<=0 表示常驻（直到 close() 或被下一条通知顶掉）。
// 失败时写 error 并返回 false（**不静默降级成空面板**）。
bool show(const QString &face, const QJsonObject &params, int durationMs,
          QString *error, QJsonObject *data);

// 收起面板（岛回到空闲 / 或回落到上一条内容，走既有 enterIdle 路径）。
void close();

}   // namespace PanelDomain
