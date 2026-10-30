#pragma once
#include <QString>   // 头文件独立编译需要 QString 前向可见（main.cpp 原靠全局 QtCore 引入）
// 录像/班级监控上传（2026-10-30 从 main.cpp 迁出为独立模块，原 7118–7161）
//
// record_stop 后把本地 rec-*.mp4 POST 到云端 /api/recordings/upload（Bearer 设备令牌）。
// 异步执行：QNetworkAccessManager 是 QObject，本模块内创建并负责自己生命周期。
//
// ⚠️ 外部链接：MediaSession.cpp 跨 TU 调它，故不能放进匿名 namespace（保持 file-scope）。
//    g_uid 沿用 MediaSession.cpp 既有的「跨 TU 共享全局」约定（见本模块 .cpp 的 extern）。
void uploadRecordingAsync(const QString &mp4Path);
