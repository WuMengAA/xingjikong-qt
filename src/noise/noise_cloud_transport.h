#pragma once
// 真传输实现（2026-10-30 · P0-2 分层第 4 层）
//
// 把事件交给 main.cpp 的 WS 桥 sendNoiseIncident —— 那是**唯一**知道 socket 与信封格式的地方。
// 本类自身不 include <QWebSocket>、不开连接，纯粹转发。
#include "noise_transport.h"

class CloudNoiseTransport : public INoiseTransport
{
public:
    void sendIncident(const QJsonObject &incident) override;
};
