#include "noise_cloud_transport.h"

#include "../cloud_event_bridge.h"   // sendNoiseIncident 的实体在 main.cpp（唯一知道 WS 处）
                                     // 本文件在 src/noise/，要用 ../ 回到 src/ 根再找

void CloudNoiseTransport::sendIncident(const QJsonObject &incident)
{
    sendNoiseIncident(incident);
}
