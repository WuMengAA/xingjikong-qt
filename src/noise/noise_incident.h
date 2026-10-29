#pragma once
// 一次噪音事件（值对象）—— 2026-10-30 · P0-2 分层第 1 层
//
// 只装数据，不带行为、不继承 QObject、不碰网络/UI。
// 之所以强调"值对象"：它是域层与应用层之间的**唯一契约**。
// 有了它，抓帧、上传、管理端展示都能拿同一份数据，不必各自去问 NoiseMonitor 要状态
// （那种"到处反查全局单例"的写法，就是 main.cpp 变成上帝对象的起点）。

#include <QDateTime>
#include <QString>

struct NoiseIncident {
    QDateTime at;            // 触发时刻（抓帧与上报都用它对齐时间戳）
    QString   periodKey;     // 节次标识：换节次即换统计周期
    int       hits = 0;      // 本周期累计超阈次数
    double    db = 0.0;      // 触发那次的读数（SPL 口径）
    double    baselineDb = 0.0;    // 当时的自适应基线（诊断用：能看出"是真吵还是基线飘了"）
    double    thresholdDb = 0.0;   // 当时实际用的阈值
};
