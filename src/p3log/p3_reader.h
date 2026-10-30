#pragma once
#include <QString>
#include <QJsonArray>

// P3 长期记录查询/展示（2026-10-30 main 拆分 #6b 迁出）。
// readJsonlDir / openFolder / openNoiseLogDialog / openStudyLogDialog /
// buildNoiseLogText / buildStudyLogText 从 main.cpp 迁出；dataRoot() 复用 p3_log（#6a）。
// openFolder 也被 main 侧 openPlayScheduleDialog 与 AgentUi（打开记录文件夹按钮）调用，
// 故保持公开 API。

QJsonArray readJsonlDir(const QString &subdir);
void openFolder(const QString &subdir);
void openNoiseLogDialog();
void openStudyLogDialog();
QString buildNoiseLogText();
QString buildStudyLogText();