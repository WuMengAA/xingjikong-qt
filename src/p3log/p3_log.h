#pragma once
#include <QString>
#include <QJsonObject>

// P3 长期记录：噪音/自习会话 本地落盘内核。
// 2026-10-30 main 拆分 #6a：dataRoot / appendJsonl / logNoiseSample / logStudyStart /
// logStudyStop 从 main.cpp（原 4036-4113）迁出。全部自包含（仅 QStandardPaths / QJson /
// QDate / 文件 IO，**零 main 全局依赖**），main 侧 P3 查询对话框与自习/噪音接线经本头
// 同名调用（函数名不变 ⇒ 调用点不改行）。

QString dataRoot();
void appendJsonl(const QString &subdir, const QJsonObject &o);
void logNoiseSample(double db, bool active, int seq, bool study, bool ok);
qint64 logStudyStart(double th, int need, int recSec);
void logStudyStop(qint64 startTs, int alertCount);