#pragma once
#include <QString>

// OTA 灰度渠道持久化（2026-10-30 从 main.cpp 迁出；逻辑与原地逐字一致）。
// channelFilePath() 依赖 agentEnvPath()（仍定义在 main.cpp，已提升为外部链接，见下方 extern）。
QString channelFilePath();
QString resolvedChannel();
void persistChannel(const QString &ch);
