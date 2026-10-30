#pragma once
// 噪音事件传输接口（2026-10-30 · P0-2 分层第 4 层 · 可注入抽象）
//
// Reporter 只认识这个接口，不认识 WS / HTTP / CloudLink。换传输实现（真·WS / Mock /
// 离线队列）只需换一个实现，Reporter 一行不改 —— 单测用 Mock 即可验「上报过几次、发了什么」。
#include <QJsonObject>

class INoiseTransport
{
public:
    virtual ~INoiseTransport() = default;

    /// 把一条已组装好的噪音事件（含存证图 base64）交给传输层。
    /// 是否在线、要不要重投由实现决定（真实现未连则静默丢弃，与遥测同口径）。
    virtual void sendIncident(const QJsonObject &incident) = 0;
};
