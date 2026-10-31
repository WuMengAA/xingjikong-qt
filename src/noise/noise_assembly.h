#pragma once
// 噪音链路装配工厂（2026-10-30 · P0-2 分层 · main.cpp 只调这一个函数）
//
// 把所有「具体类型 new 出来并接线」的事收进 .cpp，main.cpp 只：
//   #include 本头 + 一行 createNoisePipeline(&app) + 把噪声样本/节次信号连上去。
// 这样 main.cpp 不膨胀、不认识 DesktopSnapshot/CameraSnapshot/CloudNoiseTransport 这些具体类，
// 业务全在 src/noise/ 里。
#include "noise_announcer.h"        // 完整类型：main 要对 announcer 的槽取址（&NoiseAnnouncer::onNoiseThreshold）
#include "noise_incident_pipeline.h"   // 完整类型：main 要对其调用成员函数 + 当 connect 的 context 对象，
                                       // 只前向声明会让编译器无法验证其派生自 QObject（模板匹配失败）

/// 构造并接线完整的噪音存证链路（抓帧源 + 传输 + 上报 + 协调器），返回协调器（main 持有）。
/// 返回的对象已自包含；main 只需再 connect 两个信号源（噪声样本、课表节次）。
NoiseIncidentPipeline *createNoisePipeline(QObject *parent);

/// 全局提醒器：目录在噪音域（thresholdReached 的消费方），由 createNoisePipeline 顺带创建，
/// main 只在 ensureNoise() 里连一行 thresholdReached → announcer（见 noise_assembly.cpp 装配）。
NoiseAnnouncer *getNoiseAnnouncer();
