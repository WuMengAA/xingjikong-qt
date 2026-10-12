// 语音对讲 · 管理端音频采集 —— Android 实现（2026-10-11 从 stub 升级 AAudio）
//
// 桌面版用 winmm waveIn（audio_capture.cpp）；Android 没有 winmm，而
// Qt Multimedia 模块两端 kit 都未安装（QAudioSource 不可用），故用 NDK 原生
// AAudio（Android 8.0+/API 26+，纯 C++，无 Qt 模块依赖）。
//
// 对外契约（与桌面版逐字一致）：onFrame 每 20ms 吐一帧 640 字节
// 16kHz / 单声道 / 16bit 小端 PCM —— 对端（被控端 waveOut）按这个格式播。
//
// ⚠️ 2026-10-13 修（#111「Android 取值/格式」排查）：
//   AAudio 的 setSampleRate / setFormat / setChannelCount **都只是"建议"**，
//   设备完全可以给你别的值（48kHz、FLOAT、双声道…）**且不报错**。原实现无条件
//   按"16kHz/单声道/I16"解释回调数据、并按固定 640 字节切帧，于是：
//     ① 设备回 FLOAT 时 —— float 字节被当 int16 发出去 → 对端听到一串噪声；
//     ② 设备回 48kHz 时 —— 仍只按字节数切 640B，每帧其实只有 6.7ms，
//        对端按 20ms 播 → 快放/变调（"听不清"）。
//   现在在**回调内做归一化**：任意格式/声道 → 单声道 float → 线性重采样到
//   16kHz → I16。对外发的永远是契约里的 16k/mono/I16，**对端零改动**。
//   实际设备参数会打进日志（真机排查时一眼能看到设备到底给了什么）。
//
// ⚠️ 权限：AndroidManifest 已声明 RECORD_AUDIO；运行期权限在
// ViewerBackend::startSpeaking 里请求，授权后才调 startCapture。
// AAudio open 在未授权时会返回错误码，这里给成人话（不静默）。

#include "audio_capture.h"

#include <aaudio/AAudio.h>

#include <QByteArray>
#include <QDebug>
#include <QVector>

namespace {

AudioCapture *g_androidCapture = nullptr;
AAudioStream *g_androidStream = nullptr;

/** 归一化后的输出字节流（16kHz/单声道/I16），按 640B 切帧后交 onFrame。 */
QByteArray g_androidBuf;

/** 设备**实际**流参数 —— open 成功后回读填入（与请求值可能不同）。 */
int             g_srcRate = 16000;
int             g_srcCh   = 1;
aaudio_format_t g_srcFmt  = AAUDIO_FORMAT_PCM_I16;

/** 线性重采样状态（必须跨回调保持，否则每个回调边界都会"咔"一声）。
 *  g_rsPos：下一个输出样本落在"已累积输入样本"里的位置（可为小数）；
 *  g_rsIn ：本轮回调累积的单声道 float 输入样本（消费过的头部会被丢弃）。 */
double g_rsPos = 0.0;
QVector<float> g_rsIn;

constexpr int kOutRate    = 16000;
constexpr int kFrameBytes = 640;   // 20ms @ 16k / mono / I16

/** 按实际格式把一个样本读成 float（[-1,1]）。 */
inline float sampleToFloat(const void *base, int index, aaudio_format_t fmt)
{
    switch (fmt) {
    case AAUDIO_FORMAT_PCM_FLOAT:
        return static_cast<const float *>(base)[index];
    case AAUDIO_FORMAT_PCM_I32:
        return float(static_cast<const int32_t *>(base)[index] / 2147483648.0);
    case AAUDIO_FORMAT_PCM_I24_PACKED: {
        const unsigned char *p =
            static_cast<const unsigned char *>(base) + qsizetype(index) * 3;
        int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
        if (v & 0x800000) v |= ~0xFFFFFF;          // 24bit 符号扩展
        return float(v / 8388608.0);
    }
    case AAUDIO_FORMAT_PCM_I16:
    default:
        return float(static_cast<const int16_t *>(base)[index] / 32768.0);
    }
}

inline int16_t floatToI16(float s)
{
    if (s > 0.999969f) s = 0.999969f;
    if (s < -1.0f) s = -1.0f;
    return int16_t(s * 32767.0f);
}

/** 把 onFrame 契约的一帧（640B）尽量多地吐出去。 */
inline void drainFrames(AudioCapture *cap)
{
    while (g_androidBuf.size() >= kFrameBytes) {
        const QByteArray pcm = g_androidBuf.left(kFrameBytes);
        g_androidBuf.remove(0, kFrameBytes);
        if (cap && cap->onFrame) cap->onFrame(pcm);
    }
}

// AAudio 数据回调（跑在音频线程，与桌面 waveIn 回调线程同级）。
static aaudio_data_callback_result_t aaudioDataCb(AAudioStream *stream,
                                                  void *userData,
                                                  void *audioData,
                                                  int32_t numFrames)
{
    AudioCapture *cap = reinterpret_cast<AudioCapture *>(userData);
    if (!cap || !g_androidStream || !audioData) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    if (numFrames <= 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;

    const int32_t ch = AAudioStream_getChannelCount(stream);
    const int     nSamples = numFrames * (ch > 0 ? ch : 1);

    // ── ① 任意格式 → 单声道 float（多声道只取第 0 通道：对讲只要人声，不混音）──
    static QVector<float> mono;          // 静态复用，避免音频线程反复分配
    mono.resize(numFrames);
    if (ch > 1) {
        for (int f = 0; f < numFrames; ++f)
            mono[f] = sampleToFloat(audioData, f * ch, g_srcFmt);
    } else {
        for (int i = 0; i < nSamples; ++i)
            mono[i] = sampleToFloat(audioData, i, g_srcFmt);
    }

    // ── ② 采样率 → 16kHz。设备给的就是 16k 时走直通快路径（不插值、零损耗）──
    static QVector<float> out16k;
    out16k.clear();

    if (g_srcRate == kOutRate) {
        out16k = mono;                                   // ratio = 1，直通
    } else {
        const double ratio = double(g_srcRate) / double(kOutRate);   // 输入/输出
        g_rsIn += mono;                                  // QVector 支持 +=
        // 只要"下一个输出点 + 1"仍落在已累积样本内，就能插值出这个输出点
        while (g_rsPos + 1.0 < double(g_rsIn.size())) {
            const int    i0   = int(g_rsPos);
            const double frac = g_rsPos - double(i0);
            const float  s    = float(g_rsIn[i0] * (1.0 - frac) + g_rsIn[i0 + 1] * frac);
            out16k.append(s);
            g_rsPos += ratio;
        }
        // 丢弃已消费的头部，只留最后 1 个样本供下次边界插值
        const int consumed = int(g_rsPos);
        if (consumed > 0) {
            g_rsIn.remove(0, consumed);
            g_rsPos -= double(consumed);
        }
    }

    // ── ③ float → I16，攒进契约缓冲，满 640B 就吐帧 ──
    if (!out16k.isEmpty()) {
        const int oldSize = g_androidBuf.size();
        g_androidBuf.resize(oldSize + out16k.size() * 2);
        char *dst = g_androidBuf.data() + oldSize;
        for (int i = 0; i < out16k.size(); ++i) {
            const int16_t v = floatToI16(out16k[i]);
            dst[i * 2]     = char(v & 0xFF);
            dst[i * 2 + 1] = char((v >> 8) & 0xFF);
        }
        drainFrames(cap);
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

} // namespace

AudioCapture::AudioCapture(QObject *parent) : QObject(parent) {}

AudioCapture::~AudioCapture()
{
    stopCapture();
    if (g_androidCapture == this) g_androidCapture = nullptr;
}

bool AudioCapture::startCapture()
{
    if (m_active) return true;
    if (g_androidCapture && g_androidCapture != this) {
        g_androidCapture->stopCapture();   // 单麦语义：先停旧的
    }
    g_androidCapture = this;

    AAudioStreamBuilder *builder = nullptr;
    aaudio_result_t bb = AAudio_createStreamBuilder(&builder);
    if (bb != AAUDIO_OK || !builder) {
        m_lastError = QStringLiteral("AAudio_createStreamBuilder 失败（%1）").arg(int(bb));
        g_androidCapture = nullptr;
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder, 16000);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setDataCallback(builder, aaudioDataCb, this);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);

    aaudio_result_t ob = AAudioStreamBuilder_openStream(builder, &g_androidStream);
    AAudioStreamBuilder_delete(builder);
    if (ob != AAUDIO_OK || !g_androidStream) {
        m_lastError = QStringLiteral("AAudio open 失败（%1）——是否未授予录音权限（RECORD_AUDIO）？")
                          .arg(int(ob));
        g_androidStream = nullptr;
        g_androidCapture = nullptr;
        return false;
    }

    // ── 回读设备**实际**给的参数（上面那些 set 只是建议，设备可不照办）──
    g_srcRate = AAudioStream_getSampleRate(g_androidStream);
    g_srcCh   = AAudioStream_getChannelCount(g_androidStream);
    g_srcFmt  = AAudioStream_getFormat(g_androidStream);
    if (g_srcRate <= 0) g_srcRate = 16000;   // 兜底：读不到就当请求值
    if (g_srcCh   <= 0) g_srcCh   = 1;
    // 认不出的格式按 I16 解释（比按 FLOAT 解释安全：至少不会把噪声放大 4 倍）
    if (g_srcFmt != AAUDIO_FORMAT_PCM_I16 && g_srcFmt != AAUDIO_FORMAT_PCM_FLOAT
        && g_srcFmt != AAUDIO_FORMAT_PCM_I32 && g_srcFmt != AAUDIO_FORMAT_PCM_I24_PACKED) {
        qInfo("[audio] ⚠️ Android 设备给了未知格式 %d，按 PCM_I16 解释", int(g_srcFmt));
        g_srcFmt = AAUDIO_FORMAT_PCM_I16;
    }

    g_androidBuf.clear();
    g_rsIn.clear();
    g_rsPos = 0.0;

    const aaudio_result_t sb = AAudioStream_requestStart(g_androidStream);
    if (sb != AAUDIO_OK) {
        m_lastError = QStringLiteral("AAudio start 失败（%1）").arg(int(sb));
        AAudioStream_close(g_androidStream);
        g_androidStream = nullptr;
        g_androidCapture = nullptr;
        return false;
    }

    m_active = true;
    qInfo("[audio] 🎤 Android 麦克风已开（设备实际 %d Hz / %d 声道 / fmt=%d → 归一化 16kHz 单声道 16bit，20ms/帧）",
          g_srcRate, g_srcCh, int(g_srcFmt));
    if (g_srcRate != kOutRate || g_srcCh != 1) {
        qInfo("[audio] ℹ️ 设备未按请求给 16kHz/单声道，已在本端重采样/降声道（对端无需改动）");
    }
    return true;
}

void AudioCapture::stopCapture()
{
    if (g_androidStream) {
        AAudioStream_requestStop(g_androidStream);
        AAudioStream_close(g_androidStream);
        g_androidStream = nullptr;
    }
    g_androidBuf.clear();
    g_rsIn.clear();
    g_rsPos = 0.0;
    m_active = false;
    g_androidCapture = nullptr;
}

void AudioCapture::handleBuffer()
{
    // Android 由 AAudio readyRead-like 数据回调驱动，无 waveIn 缓冲回调用不到。
}
