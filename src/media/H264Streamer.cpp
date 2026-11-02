// 星集控被控端 · H264 推流器实现（A 档第②步，2026-10-31）
//
// 职责（仅采集 + 编码，不碰 WS/协议，见 H264Streamer.h 分层说明）：
//   1. start() 初始化编码器 + ScreenCapture，起一个 fps 节拍 QTimer；
//   2. tick() 抓一帧（真实桌面 BGRX 或沙箱合成彩条）→ 喂编码器 → 编码出的 NALU 经信号发射；
//   3. 编码器 0 产出 / 初始化失败 → 静默跳过，不刷屏、不抛异常（远控旁路纪律）。
//
// 为什么合成帧兜底：本机沙箱无真实桌面会话（MF 编解码亦 0 产出，A 档第①步已证伪），
// 但"管道端到端"需要被控端能持续喂数据。合成彩条保证：真机有画面、沙箱也能跑通数据链路，
// 只是沙箱里编码器不产 NALU（本机预期行为，非代码缺陷）。

#include "H264Streamer.h"
#include "H264EncoderMf.h"
#include "H264EncoderOh264.h"

#ifdef _WIN32
#include <QTimer>
#include <QDebug>
#include <vector>
#include "../capture/screen_capture.h"   // ScreenCapture（BGRX 抓屏，与 ThumbnailReporter 同口径）

namespace {
// 合成彩条 BGRX 帧（与 A 档第①步探针同款）：8 竖条 + 横坡，确定性强、可复现。
// 仅用于沙箱无桌面 / 抓屏失败时的兜底，保证管道不空转。
std::vector<uint8_t> makeSyntheticBgrx(int w, int h)
{
    std::vector<uint8_t> f(size_t(w) * h * 4, 0);
    static const int bars[8][3] = {
        {255,0,0},{0,255,0},{0,0,255},{255,255,0},
        {0,255,255},{255,0,255},{255,255,255},{128,128,128}
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int bi = (x * 8 / w) % 8;
            const int ramp = (x * 255) / w;          // 0..255 横坡
            uint8_t *p = &f[size_t(y) * w * 4 + size_t(x) * 4];
            p[0] = static_cast<uint8_t>(bars[bi][2] * ramp / 255 + 20); // B
            p[1] = static_cast<uint8_t>(bars[bi][1] * ramp / 255 + 20); // G
            p[2] = static_cast<uint8_t>(bars[bi][0] * ramp / 255 + 20); // R
            p[3] = 255;                                                     // X
        }
    return f;
}
} // namespace

struct H264Streamer::Impl {
    H264EncoderMf enc;
    H264EncoderOh264 encOh264;   // 软件编码兜底（MF 硬件编不出帧时接管）
    bool useOh264 = false;
    QTimer *timer = nullptr;
    ScreenCapture *cap = nullptr;     // 真实抓屏（沙箱可能为 nullptr/抓空）
    bool ok = false;
    int w = 0, h = 0;
    std::vector<uint8_t> synth;      // 合成帧缓冲（兜底用）
};

H264Streamer::H264Streamer(QObject *parent) : QObject(parent), m_p(new Impl) {}
H264Streamer::~H264Streamer() { stop(); delete m_p; }

H264Streamer &H264Streamer::instance()
{
    static H264Streamer s;
    return s;
}

bool H264Streamer::start(int w, int h, int fps, int bitrateKbps)
{
    if (m_p->ok) stop();
    m_p->w = w; m_p->h = h;
    // 编码器择优：先 MF 硬件（零依赖、教室机可 GPU 加速），
    // 失败则回落 OpenH264 软件编码（2026-11-01 实测：本机 MF 协商成功却 0 产出，
    // 软编 30/30 帧出帧且不依赖 GPU/DWM），两者都失败才回落 VNC。
    if (m_p->enc.init(w, h, fps, bitrateKbps)) {
        m_p->useOh264 = false;
    } else if (m_p->encOh264.init(w, h, fps, bitrateKbps)) {
        m_p->useOh264 = true;
        qInfo("[h264str] MF 硬件编码不可用 → 已切到 OpenH264 软件编码");
    } else {
        qInfo("[h264str] 编码器 init 失败（MF 与软编都不可用）→ 推流未启动（远控回落 VNC）");
        return false;
    }
    // 真实抓屏（沙箱无桌面时 init 可能失败，不影响推流管线——用合成帧兜底）
    m_p->cap = new ScreenCapture;
    if (!m_p->cap->init(w, h)) {
        qInfo("[h264str] ScreenCapture init 失败（沙箱/无桌面）→ 用合成帧兜底");
        delete m_p->cap; m_p->cap = nullptr;
    }
    m_p->synth = makeSyntheticBgrx(w, h);

    m_p->timer = new QTimer(this);
    m_p->timer->setInterval(fps > 0 ? 1000 / fps : 33);
    connect(m_p->timer, &QTimer::timeout, this, [this]() {
        int gw = m_p->w, gh = m_p->h;
        const uint8_t *px = nullptr;
        if (m_p->cap) px = m_p->cap->grab(gw, gh);   // 真实桌面 BGRX（可能返回 nullptr）
        if (!px || gw != m_p->w || gh != m_p->h) {
            px = m_p->synth.empty() ? nullptr : m_p->synth.data();   // 兜底合成帧
        }
        if (!px) return;   // 两路都无像素 → 跳过本帧

        std::vector<uint8_t> nalu;
        const bool got = m_p->useOh264
            ? m_p->encOh264.encode(px, m_p->w, m_p->h, nalu)
            : m_p->enc.encode(px, m_p->w, m_p->h, nalu);
        if (got && !nalu.empty()) {
            emit h264FrameReady(QByteArray(reinterpret_cast<const char *>(nalu.data()),
                                          static_cast<int>(nalu.size())));
        }
        // encode 返回 false = 本帧未产出（队列满 / MF 硬件 0 产出）→ 静默跳过
    });
    m_p->timer->start();
    m_p->ok = true;
    qInfo("[h264str] H264 推流启动 %dx%d @%dfps", w, h, fps);
    return true;
}

void H264Streamer::stop()
{
    if (m_p->timer) { m_p->timer->stop(); delete m_p->timer; m_p->timer = nullptr; }
    if (m_p->cap) { delete m_p->cap; m_p->cap = nullptr; }
    if (m_p->ok) {
        m_p->enc.shutdown();
        m_p->encOh264.shutdown();
    }
    m_p->useOh264 = false;
    m_p->ok = false;
    m_p->synth.clear();
}

bool H264Streamer::running() const { return m_p && m_p->ok; }

#else // 非 Windows：本模块不参与编译

H264Streamer::H264Streamer(QObject *) : m_p(nullptr) {}
H264Streamer::~H264Streamer() {}
H264Streamer &H264Streamer::instance() { static H264Streamer s; return s; }
bool H264Streamer::start(int, int, int, int) { return false; }
void H264Streamer::stop() {}
bool H264Streamer::running() const { return false; }

#endif
