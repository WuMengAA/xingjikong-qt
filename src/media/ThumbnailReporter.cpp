// 星集控被控端 · 班级域缩略图上报（#459）—— 实现
// 设计纪律见 ThumbnailReporter.h 头注释。本文件零 QObject 依赖：
// 定时器由 main.cpp 驱动，本模块只有 tick() 纯逻辑 ⇒ 测试可静态链接直跑。

#include "ThumbnailReporter.h"

#include <QBuffer>
#include <QImage>

#include <algorithm>
#include <cstring>
#include <memory>

#include "../screen_capture.h"   // 默认帧源：ScreenCapture 门面（DXGI→GDI 自选）+ pickTargetDisplay

namespace ThumbnailReporter {

namespace {

// ── 进程态（一个 agent 一个 reporter；测试同进程多场景共享注入点）─────
// 只在主线程读写（tick 由 main.cpp 的 QTimer 驱动，注入发生在启动接线时）⇒ 无锁。
FrameSource g_frameSrc;
Sender g_sender;
ReadyCheck g_ready;
Stats g_stats;

// 默认帧源（lazy）：独立小尺寸 ScreenCapture 实例，与 VNC 的 720p 实例
// 互不干扰（DXGI OutputDuplication 多实例合法，见头注释）。
std::unique_ptr<ScreenCapture> g_cap;
QString g_capBackend;          // "dxgi"/"gdi"/"none"：诊断 + payload 的 cap 字段
int g_srcW = 0, g_srcH = 0;    // 目标屏原生尺寸（比例变化检测用）
int g_outW = 0, g_outH = 0;    // 当前缩略图输出尺寸

/// JPEG 原图预算：base64（×4/3）后加 {w,h,mime,cap,b64} 的 JSON 结构
/// 杂项（~120B）仍须 ≤ 云端 64KB data 闸门。46KB → 61.3KB + 0.12KB，安全。
constexpr int kMaxJpegBytes = 46 * 1024;

/// 三档质量：先保清晰，超预算再降。320×180 普通画面第一档（q50）就过。
const int kQualities[3] = {50, 35, 25};

bool envFlag(const char *name, bool dflt)
{
    const QByteArray v = qgetenv(name);
    if (v.isEmpty()) return dflt;
    // "0"/"false"/"off"/"no"（大小写不敏感）算关，其余算开——与单实例守卫同款语义
    const QString s = QString::fromUtf8(v).trimmed().toLower();
    return !(s == QLatin1String("0") || s == QLatin1String("false")
          || s == QLatin1String("off") || s == QLatin1String("no"));
}

int envInt(const char *name, int dflt, int lo, int hi)
{
    bool ok = false;
    const int v = QString::fromUtf8(qgetenv(name)).toInt(&ok);
    if (!ok || v <= 0) return dflt;
    return qBound(lo, v, hi);
}

/// 抽样判黑（8×8 网格 64 点）：BGR 全零判黑。锁屏 / 登录桌面 / 纯 Session 0
/// 无桌面时 BitBlt 不失败但给全黑 —— 看板显示旧图胜过一块黑（VNC 抓屏
/// 黑帧检查同款纪律）。纯黑桌面背景会误判，概率极低且语义正确（无可看内容）。
bool looksBlack(const uint8_t *bgrx, int w, int h)
{
    if (!bgrx || w <= 0 || h <= 0) return true;
    int zero = 0, total = 0;
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 8; ++j) {
            const int x = qBound(0, static_cast<int>((static_cast<long long>(w) * i) / 7), w - 1);
            const int y = qBound(0, static_cast<int>((static_cast<long long>(h) * j) / 7), h - 1);
            const uint8_t *p = bgrx + (static_cast<size_t>(y) * w + x) * 4;
            ++total;
            if (p[0] == 0 && p[1] == 0 && p[2] == 0) ++zero;
        }
    }
    return total > 0 && zero == total;
}

/// 按目标屏比例算缩略图尺寸（长边 maxEdge，保持宽高比——变形画面老师难认设备）。
void calcThumbSize(int srcW, int srcH, int &outW, int &outH)
{
    const int edge = maxEdge();
    if (srcW <= 0 || srcH <= 0) { outW = edge; outH = edge * 9 / 16; return; }  // 兜底 16:9
    const double scale = static_cast<double>(edge) / std::max(srcW, srcH);
    outW = qMax(2, static_cast<int>(srcW * scale));
    outH = qMax(2, static_cast<int>(srcH * scale));
}

/// 默认帧源 lazy init + 抓一帧。选择器沿用修复②v2 的物理屏挑选
///（pickTargetDisplay：免疫 GameViewer 类虚拟屏抢"主屏"位，2026-10-19 白屏事故的修法）。
const uint8_t *defaultGrab(int &w, int &h)
{
#ifdef _WIN32
    const SteTargetDisplay d = pickTargetDisplay();
    if (d.ok && d.w > 0 && d.h > 0 && (g_srcW != d.w || g_srcH != d.h || !g_cap)) {
        // 首次 / 目标屏比例变化（投影切换是教室常态：16:9↔4:3）：按新比例重建。
        // 注意：这里是「原生尺寸变了才重建」，不是每拍重建；门面内部还会自行
        // 处理同尺寸下的分辨率微调。
        g_srcW = d.w;
        g_srcH = d.h;
        calcThumbSize(d.w, d.h, g_outW, g_outH);
        auto cap = std::make_unique<ScreenCapture>();
        if (cap->init(g_outW, g_outH)) {
            g_cap = std::move(cap);
            g_capBackend = QString::fromLatin1(g_cap->backend());
            qInfo("[agent-thumb] 缩略图源就绪：目标屏 %dx%d（%s）→ 输出 %dx%d",
                  d.w, d.h, g_cap->backend(), g_outW, g_outH);
        } else {
            // init 失败（无 D3D11 / 老系统 / 独占全屏会话异常）：门面内部已回落
            // GDI 仍失败 ⇒ 本拍不抓，下一拍（15s 后）重试——不反噬主流程。
            qWarning("[agent-thumb] 缩略图源 init 失败（本拍跳过，稍后重试）");
            g_cap.reset();
            g_capBackend.clear();
            return nullptr;
        }
    }
    if (!g_cap) return nullptr;
    const uint8_t *fr = g_cap->grab(w, h);
    if (!fr) return nullptr;   // DXGI 无新帧 / ACCESS_LOST 重建中：静默跳过本拍
    return fr;
#else
    Q_UNUSED(w);
    Q_UNUSED(h);
    return nullptr;            // 非 Windows：无默认源（测试注入合成帧）
#endif
}

} // namespace

// ── 注入 ──────────────────────────────────────────────────────────────
void setFrameSource(FrameSource f) { g_frameSrc = std::move(f); }
void setSender(Sender s) { g_sender = std::move(s); }
void setReadyCheck(ReadyCheck c) { g_ready = std::move(c); }

// ── 配置（env 现读：改值不用重启；间隔每拍重读由 main.cpp 的 timer 同步）──
bool enabled() { return envFlag("STE_THUMB_REPORT", true); }
int intervalMs() { return envInt("STE_THUMB_INTERVAL_MS", 15000, 2000, 600000); }
int maxEdge() { return envInt("STE_THUMB_MAX_EDGE", 320, 64, 1280); }

// ── 编码（独立可测，无抓屏依赖）──────────────────────────────────────
QByteArray encodeThumbJpeg(const uint8_t *bgrx, int w, int h, int q)
{
    if (!bgrx || w <= 0 || h <= 0) return {};
    // BGRX 紧凑缓冲（stride = w*4）的小端内存布局恰是 QImage::Format_RGB32
    // （0xffRRGGBB 的字节序 B,G,R,X）——零转换直读，X 字节 JPEG 不写。
    const QImage img(bgrx, w, h, w * 4, QImage::Format_RGB32);
    if (img.isNull()) return {};
    for (int qi = 0; qi < 3; ++qi) {
        const int quality = (qi == 0) ? q : kQualities[qi];
        QByteArray jpeg;
        QBuffer buf(&jpeg);
        if (!buf.open(QIODevice::WriteOnly)) return {};
        const bool ok = img.save(&buf, "JPG", quality);
        buf.close();
        if (!ok || jpeg.isEmpty()) return {};
        if (jpeg.size() <= kMaxJpegBytes) return jpeg;
        // 超预算：降下一档；三档都超返回空（调用方丢帧，绝不发越界数据）
    }
    return {};
}

// ── 拍一拍 ────────────────────────────────────────────────────────────
bool tick()
{
    ++g_stats.ticks;
    if (!enabled()) return false;
    if (g_ready && !g_ready()) { ++g_stats.skipped; return false; }

    int w = 0, h = 0;
    const uint8_t *fr = g_frameSrc ? g_frameSrc(w, h) : defaultGrab(w, h);
    if (!fr || w <= 0 || h <= 0) { ++g_stats.skipped; return false; }
    if (looksBlack(fr, w, h)) { ++g_stats.skipped; return false; }

    const QByteArray jpeg = encodeThumbJpeg(fr, w, h, kQualities[0]);
    if (jpeg.isEmpty()) {
        ++g_stats.dropped;
        qWarning("[agent-thumb] JPEG 三档降质仍超 %dKB 预算，本帧丢弃（%dx%d）",
                 kMaxJpegBytes / 1024, w, h);
        return false;
    }

    QJsonObject data;
    data[QStringLiteral("w")] = w;
    data[QStringLiteral("h")] = h;
    data[QStringLiteral("mime")] = QStringLiteral("image/jpeg");
    if (!g_capBackend.isEmpty()) data[QStringLiteral("cap")] = g_capBackend;
    data[QStringLiteral("b64")] = QString::fromLatin1(jpeg.toBase64());

    QJsonObject payload;
    payload[QStringLiteral("kind")] = QStringLiteral("thumbnail");
    payload[QStringLiteral("data")] = data;

    if (g_sender) g_sender(payload);
    ++g_stats.sent;
    g_stats.lastBytes = jpeg.size();
    g_stats.lastW = w;
    g_stats.lastH = h;
    return true;
}

Stats stats() { return g_stats; }

// ── 合成帧（测试 / 探针共用）─────────────────────────────────────────
std::vector<uint8_t> makeSyntheticFrame(int w, int h)
{
    std::vector<uint8_t> fr(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    // 8 色竖条（BGRX 字节序 B,G,R,X）+ 从左到右暗→亮斜坡；确定性，无随机
    static const uint8_t kBar[8][3] = {
        {0, 0, 255}, {0, 255, 0}, {255, 0, 0}, {0, 255, 255},
        {255, 255, 0}, {255, 0, 255}, {128, 128, 128}, {255, 255, 255}};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int bar = qBound(0, (x * 8) / w, 7);
            const double ramp = 0.35 + 0.65 * static_cast<double>(x) / w;
            uint8_t *p = fr.data() + (static_cast<size_t>(y) * w + x) * 4;
            p[0] = static_cast<uint8_t>(qMin(255, static_cast<int>(kBar[bar][0] * ramp)));
            p[1] = static_cast<uint8_t>(qMin(255, static_cast<int>(kBar[bar][1] * ramp)));
            p[2] = static_cast<uint8_t>(qMin(255, static_cast<int>(kBar[bar][2] * ramp)));
            p[3] = 0;   // X
        }
    }
    return fr;
}

} // namespace ThumbnailReporter
