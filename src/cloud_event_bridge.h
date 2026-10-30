#pragma once
// 设备→云端 传输桥（2026-10-30 · P0-2 分层第 4 层）
//
// 为什么有这个头：噪音链路里的 Reporter / Transport 必须让「知道 WS 存在」这件事
// 收敛到 main.cpp 一处。本头只声明一个转发函数，实体在 main.cpp（那里才持有 g_ws 与信封格式）。
// noise/ 下的类 include 本头、调用 sendNoiseIncident —— 它们不 include <QWebSocket>、
// 不开 socket、不拼信封，纯粹把 JSON 交出去。
//
// ⚠️ 实体在 main.cpp，故本声明对应的定义具有**外部链接**（不是 static）；
//    noise_cloud_transport.cpp 跨 TU 调用它靠的就是这条外部链接。
#include <QJsonObject>

/// 把一条噪音存证事件（含两张图的 base64）经 WS 信封发出去（未连则静默丢弃，与遥测同口径）。
void sendNoiseIncident(const QJsonObject &p);
