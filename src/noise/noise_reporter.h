#pragma once
// 噪音事件上报（应用服务）—— 2026-10-30 · P0-2 分层第 3 层
//
// 职责：拿到「事件 + 存证」→ 把两张图的字节 base64 进 JSON → 交给 INoiseTransport。
// 纯序列化 + 转发，不抓帧、不判定、不开 socket。依赖抽象接口 ⇒ 可单测。
//
// ⚠️ 只依赖抽象 INoiseTransport：单测用 Mock 验「上报次数 / 发出的 JSON 字段」，
//    不碰真 WS、不真连云端。
#include "noise_transport.h"
#include "noise_sample.h"
#include "noise_incident.h"   // NoiseIncident 定义在域头（reporter.h 只用了 noise_sample.h，漏了这个）

class NoiseReporter
{
public:
    explicit NoiseReporter(INoiseTransport *transport);   // 不拥有 transport
    ~NoiseReporter() = default;

    /// 组装并发送一条噪音存证事件。两张图缺失（抓不到）则如实留空 + 记原因，不假装成功。
    void report(const NoiseIncident &inc, const NoiseEvidence &ev);

    int sentCount() const { return m_sent; }

private:
    INoiseTransport *m_transport = nullptr;
    int m_sent = 0;
};
