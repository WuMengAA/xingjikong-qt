#pragma once
// 星集控被控端 · H264 推流器（A 档第②步，2026-10-31）
//
// 定位：把「抓屏 → H264EncoderMf 编码 → 抛出 NALU」串成一条定时循环。
//   真机：ScreenCapture（DXGI 桌面复制）抓 BGRX 帧 → 编码 → 上行；
//   沙箱/无桌面：回落合成彩条帧，保证整条管道（被控端→云端→管理端）仍可端到端验证。
//
// 分层（AGENTS §10）：本类只做「采集 + 编码」，不知道 WS、不知道协议信封——
//   编码出的裸 NALU 通过信号 h264FrameReady(QByteArray) 抛出，由 main.cpp（装配层）打包成
//   v1 帧（makeH264FrameBytes）并 sendBinaryMessage 出海。main.cpp 只做这一行接线。
//
// 降级纪律：编码器初始化失败或单帧 0 产出 → 静默跳过，不刷屏、不抛异常、不影响主流程
//   （远控是旁路，绝不以编解码反噬桌面/音视频主业务）。

#include <cstdint>
#include <QObject>
#include <QByteArray>

class H264EncoderMf;   // 前向声明（§10.4）：头文件不引入编码器实现依赖

class H264Streamer : public QObject {
    Q_OBJECT
public:
    static H264Streamer &instance();

    /// 启动推流。w/h 建议与抓屏输出一致（如 1280x720）；fps 默认 30。
    /// @return true = 编码器就绪（真机可出帧）；false = 本机 MF 不可用（调用方回落 VNC）。
    bool start(int w, int h, int fps = 30, int bitrateKbps = 2000);

    /// 停止推流并释放编码器。
    void stop();

    bool running() const;

signals:
    /// 编码出有效 NALU（annex-B）时发射，交给装配层打包 h264-data 帧并发主 WS。
    void h264FrameReady(const QByteArray &nalu);

private:
    explicit H264Streamer(QObject *parent = nullptr);
    ~H264Streamer() override;
    H264Streamer(const H264Streamer &) = delete;
    H264Streamer &operator=(const H264Streamer &) = delete;

    struct Impl;
    Impl *m_p = nullptr;
};
