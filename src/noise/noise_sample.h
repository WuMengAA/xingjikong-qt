#pragma once
// 噪音链路上的两个**值对象**（2026-10-30 · P0-2 分层第 2/3 层之间的契约）
//
// 为什么单独立一个头，而不是让各层互相 include：
//   · NoiseSample 是「域」吃进去的输入 —— 一次判定结果（dB、是否超阈、当时的基线与阈值）。
//     有了它，域服务就不必认识 NoiseMonitor（QObject + 麦克风），单测不必造采集器。
//   · NoiseEvidence 是「应用服务」吐出来的结果 —— 两张图的落盘路径与各自失败原因。
//     它是 Capture 与 Reporter 之间唯一的数据面，二者不互相 include。
//
// ⚠️ 全部字段都是**同一口径**（SPL：dBFS + 校准偏置）。口径换算只在
//    noise_monitor_source.cpp 一处做 —— 两个口径混着算，是这类功能最经典的
//    假阴性来源（探针里已经踩过一次：拿 dBFS 阈值去判 SPL 读数，永远不触发）。

#include <QDateTime>
#include <QString>

/// 一次噪音判定结果（NoiseMonitor 每 2s 产一条）。
struct NoiseSample {
    QDateTime at;                 ///< 判定时刻（存证时间戳与上报都用它对齐）
    double    db          = 0.0;  ///< 本次读数（SPL 口径）
    bool      active      = false;///< 是否超阈（**唯一**由 NoiseMonitor 判定，域不重判）
    double    baselineDb  = 0.0;  ///< 当时的自适应基线（SPL 口径；未启用 = NaN）
    double    thresholdDb = 0.0;  ///< 当时实际用的阈值（SPL 口径）
};

/// 一次触发的存证结果：桌面/摄像头各至多一帧。
struct NoiseEvidence {
    QString   desktopPath;   ///< 空 = 没抓到（见 desktopError）
    QString   cameraPath;    ///< 空 = 没抓到（见 cameraError）
    QString   desktopError;  ///< 抓不到的原因（人话；空 = 成功）
    QString   cameraError;
    QDateTime capturedAt;
    /// 至少抓到一张 —— 一张都没有时不值得上报"存证成功"，由 Reporter 如实标空。
    bool any() const { return !desktopPath.isEmpty() || !cameraPath.isEmpty(); }
};
