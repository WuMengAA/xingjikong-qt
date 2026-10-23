// 班级域缩略图上报探针（#459 · 被控端侧）
// 独立小程序：不起 Qt 事件循环，直接驱动 ThumbnailReporter::tick()。
// 合成帧注入 + 记录式 sender ⇒ 全闸门离线可验（不起 agent、不抓真屏、
// 不连云端）。真实 DXGI 抓屏画面 / 真实云端 fanout 不在此覆盖（归档未验证项）。
//
// 退出码：0 = 全过；1 = 有失败（CI/离屏自检同款口径）。
#include "../src/media/ThumbnailReporter.h"

#include <QCoreApplication>
#include <QImage>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <vector>

namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char *what)
{
    if (cond) { ++g_pass; std::printf("  [PASS] %s\n", what); }
    else      { ++g_fail; std::printf("  [FAIL] %s\n", what); }
}

// ── 测试全局态：三个注入 lambda 全程只读它们 ⇒ 一次注入贯穿全场景 ────
// （ThumbnailReporter 的注入点是进程级全局，场景切换改这里的量即可）
const uint8_t *g_framePtr = nullptr;   // nullptr = 抓屏失败场景
int g_frameW = 0, g_frameH = 0;
int g_frameCalls = 0;                  // 帧源被调次数（证明"未就绪不抓屏"）
bool g_readyFlag = true;
int g_sentCount = 0;
QJsonObject g_lastPayload;
std::vector<uint8_t> g_frameStore;     // 持有当前帧（帧源返回其 data()）

const uint8_t *testFrameSource(int &w, int &h)
{
    ++g_frameCalls;
    if (!g_framePtr) return nullptr;
    w = g_frameW;
    h = g_frameH;
    return g_framePtr;
}

void testSender(const QJsonObject &payload)
{
    ++g_sentCount;
    g_lastPayload = payload;
}

bool testReady() { return g_readyFlag; }

/// 换一张合成帧（彩条）
void useSynthetic(int w, int h)
{
    g_frameStore = ThumbnailReporter::makeSyntheticFrame(w, h);
    g_framePtr = g_frameStore.data();
    g_frameW = w;
    g_frameH = h;
}

/// 换一张全零帧（黑帧场景）
void useBlack(int w, int h)
{
    g_frameStore.assign(static_cast<size_t>(w) * h * 4, 0);
    g_framePtr = g_frameStore.data();
    g_frameW = w;
    g_frameH = h;
}

/// 换一张确定性伪随机噪点帧（LCG；JPEG 压缩率极差 ⇒ 必超预算场景）
void useNoise(int w, int h)
{
    g_frameStore.assign(static_cast<size_t>(w) * h * 4, 0);
    uint32_t s = 0x12345678u;
    for (auto &b : g_frameStore) {
        s = s * 1664525u + 1013904223u;
        b = static_cast<uint8_t>(s >> 24);
    }
    g_framePtr = g_frameStore.data();
    g_frameW = w;
    g_frameH = h;
}

bool isJpeg(const QByteArray &b)
{
    return b.size() > 4
        && static_cast<uint8_t>(b[0]) == 0xFF && static_cast<uint8_t>(b[1]) == 0xD8   // SOI
        && static_cast<uint8_t>(b[b.size() - 2]) == 0xFF && static_cast<uint8_t>(b[b.size() - 1]) == 0xD9; // EOI
}

} // namespace

int main(int argc, char *argv[])
{
    // Qt 的 imageformats 插件（qjpeg.dll）在 Qt 安装目录，探针 exe 旁没有 ⇒ 指过去。
    // QCoreApplication 构造时读 QT_PLUGIN_PATH。
    qputenv("QT_PLUGIN_PATH", "D:/QTqwq/6.12.0/msvc2022_64/plugins");
    QCoreApplication app(argc, argv);

    ThumbnailReporter::setFrameSource(testFrameSource);
    ThumbnailReporter::setSender(testSender);
    ThumbnailReporter::setReadyCheck(testReady);

    // ── 1. 编码单元：合成帧 → JPEG（魔数/可解码/预算内）─────────────
    std::printf("[1] 编码单元：合成帧 320x180 → JPEG\n");
    {
        const std::vector<uint8_t> fr = ThumbnailReporter::makeSyntheticFrame(320, 180);
        const QByteArray jpeg = ThumbnailReporter::encodeThumbJpeg(fr.data(), 320, 180);
        check(!jpeg.isEmpty(), "编码非空");
        check(isJpeg(jpeg), "JPEG 魔数（FFD8 头 / FFD9 尾）");
        check(jpeg.size() <= 46 * 1024, "体积 ≤46KB 预算");
        QImage back;
        back.loadFromData(jpeg, "JPG");
        check(!back.isNull() && back.width() == 320 && back.height() == 180,
              "QImage 可解码回 320x180");
        // 坏输入不炸
        check(ThumbnailReporter::encodeThumbJpeg(nullptr, 10, 10).isEmpty(), "nullptr 输入返回空（不炸）");
        check(ThumbnailReporter::encodeThumbJpeg(fr.data(), 0, 10).isEmpty(), "零宽输入返回空（不炸）");
    }

    // ── 2. 正常 tick：发出 + payload 形状 + b64 可解码 ───────────────
    std::printf("[2] 正常 tick：就绪 + 有帧 → 发出\n");
    useSynthetic(320, 180);
    g_sentCount = 0;
    {
        const bool sent = ThumbnailReporter::tick();
        check(sent, "tick() 返回 true");
        check(g_sentCount == 1, "sender 被调 1 次");
        const QJsonObject pl = g_lastPayload;
        check(pl.value("kind").toString() == "thumbnail", "payload.kind == thumbnail");
        const QJsonObject data = pl.value("data").toObject();
        check(data.value("w").toInt() == 320 && data.value("h").toInt() == 180, "data.w/h 正确");
        check(data.value("mime").toString() == "image/jpeg", "data.mime == image/jpeg");
        check(!data.contains("classCode"), "payload 不带 classCode（伪冒防护铁律）");
        const QString b64 = data.value("b64").toString();
        check(!b64.isEmpty(), "data.b64 非空");
        const QByteArray raw = QByteArray::fromBase64(b64.toLatin1());
        check(isJpeg(raw), "b64 解码后是 JPEG");
        QImage back;
        back.loadFromData(raw, "JPG");
        check(!back.isNull() && back.width() == 320, "b64 内容可解码回图");
        // 云端闸门对齐：data 的 JSON 序列化 ≤64KB
        const int dataJson = QJsonDocument(data).toJson(QJsonDocument::Compact).size();
        check(dataJson <= 64 * 1024, "data JSON ≤64KB（云端 class-event 闸门）");
        std::printf("       （本条 data JSON %d B）\n", dataJson);
    }

    // ── 3. 未就绪：跳过且不抓屏（零空转证明）────────────────────────
    std::printf("[3] 未就绪（未绑班/WS 断）：跳过且不抓屏\n");
    g_readyFlag = false;
    {
        const int callsBefore = g_frameCalls;
        const int sentBefore = g_sentCount;
        const bool sent = ThumbnailReporter::tick();
        check(!sent, "tick() 返回 false");
        check(g_frameCalls == callsBefore, "帧源一次都没被调（零空转）");
        check(g_sentCount == sentBefore, "sender 未被调");
    }
    g_readyFlag = true;

    // ── 4. 抓屏失败：跳过 ────────────────────────────────────────────
    std::printf("[4] 抓屏失败（帧源回 nullptr）：跳过\n");
    g_framePtr = nullptr;
    {
        const int sentBefore = g_sentCount;
        const bool sent = ThumbnailReporter::tick();
        check(!sent, "tick() 返回 false");
        check(g_sentCount == sentBefore, "sender 未被调");
    }

    // ── 5. 黑帧：跳过（锁屏/登录桌面/Session 0 无可看内容）───────────
    std::printf("[5] 黑帧（全零）：跳过\n");
    useBlack(320, 180);
    {
        const int sentBefore = g_sentCount;
        const bool sent = ThumbnailReporter::tick();
        check(!sent, "tick() 返回 false");
        check(g_sentCount == sentBefore, "sender 未被调");
    }

    // ── 6. 超预算：三档降质仍超 ⇒ 丢帧，绝不发越界数据 ──────────────
    std::printf("[6] 超预算（2048x1152 噪点）：丢帧守闸门\n");
    useNoise(2048, 1152);
    {
        const int sentBefore = g_sentCount;
        const auto st0 = ThumbnailReporter::stats();
        const bool sent = ThumbnailReporter::tick();
        const auto st1 = ThumbnailReporter::stats();
        check(!sent, "tick() 返回 false（丢弃）");
        check(g_sentCount == sentBefore, "sender 未被调（没有越界数据出海）");
        check(st1.dropped == st0.dropped + 1, "stats.dropped +1");
    }

    // ── 7. env 总开关：STE_THUMB_REPORT=0 ⇒ 不发；恢复后照常 ─────────
    std::printf("[7] env 开关：STE_THUMB_REPORT\n");
    useSynthetic(320, 180);
    {
        qputenv("STE_THUMB_REPORT", "0");
        check(!ThumbnailReporter::enabled(), "STE_THUMB_REPORT=0 ⇒ enabled() false");
        const int sentBefore = g_sentCount;
        check(!ThumbnailReporter::tick(), "关时 tick false");
        check(g_sentCount == sentBefore, "关时 sender 未被调");
        qputenv("STE_THUMB_REPORT", "1");
        check(ThumbnailReporter::enabled(), "STE_THUMB_REPORT=1 ⇒ enabled() true");
        check(ThumbnailReporter::tick(), "开时 tick true");
    }

    // ── 8. 默认配置值 + env 覆盖（改值不重启的口径）──────────────────
    std::printf("[8] 配置默认值 + env 覆盖\n");
    {
        check(ThumbnailReporter::intervalMs() == 15000, "默认间隔 15000ms");
        check(ThumbnailReporter::maxEdge() == 320, "默认长边 320");
        qputenv("STE_THUMB_INTERVAL_MS", "30000");
        qputenv("STE_THUMB_MAX_EDGE", "640");
        check(ThumbnailReporter::intervalMs() == 30000, "STE_THUMB_INTERVAL_MS=30000 生效");
        check(ThumbnailReporter::maxEdge() == 640, "STE_THUMB_MAX_EDGE=640 生效");
        qunsetenv("STE_THUMB_INTERVAL_MS");
        qunsetenv("STE_THUMB_MAX_EDGE");
        check(ThumbnailReporter::intervalMs() == 15000, "unset 后回落默认");
    }

    // ── 汇总 ────────────────────────────────────────────────────────
    std::printf("\n==== 缩略图探针：%d 过 / %d 败 ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
