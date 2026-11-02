#pragma once
#include <QString>

// OTA 更新渠道持久化（2026-10-30 从 main.cpp 迁出；逻辑与原地逐字一致）。
// channelFilePath() 依赖 agentEnvPath()（仍定义在 main.cpp，已提升为外部链接，见下方 extern）。
// 2026-11-02 口径变更：旧三档 stable/rc/gray → 新三档 stable/beta/alpha（渠道自动晋升）。
// 旧名字在 resolvedChannel() 里读时翻译，已落盘机器不需要手工改文件。
QString channelFilePath();
QString resolvedChannel();
void persistChannel(const QString &ch);
// 渠道名归一：alpha/gray→alpha、beta/rc→beta、stable/空→stable、其余→空串（表示不认识）。
// 与云端 ota-promote.js normalizeChannelName 同表；返回空串是让调用方能区分
// "用户写错名字" 与 "用户选了正式渠道"，前者要吵出来而不是静默当 stable。
QString channelCanonical(const QString &raw);
