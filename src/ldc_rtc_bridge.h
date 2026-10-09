#pragma once
// WebRTC ldc 桥接（2026-10-09 · 去 WebEngine 底层）
//
// 为什么独立文件：viewerbackend.cpp 已 2700+ 行（单编译单元内存峰值 C1060），
// 不能再往里塞 libdatachannel 接线。本文件只前向声明 ViewerBackend，
// viewerbackend.cpp include 它**零增量**；实现（ldc_rtc_bridge.cpp）单独编译，
// 那里才 include viewerbackend.h + ldc_receiver.h 的重头。

class ViewerBackend;

#include <QString>
#include <QByteArray>

// 初始化 ldc 收流（连接 LdcReceiver 信号 → viewerbackend 出站/状态/帧）
void ldcInitRtc(ViewerBackend *backend);

// 喂信令给 ldc：type ∈ {offer, candidate}
void ldcFeedSignal(ViewerBackend *backend, const QString &type, const QByteArray &payload);