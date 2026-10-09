#include "edge_tts.h"

#include <QWebSocket>
#include <QUrl>
#include <QDebug>
#include <QFile>
#include <QTemporaryFile>
#include <QElapsedTimer>
#include <windows.h>
#include <mmsystem.h>   // waveOut* / WAVEHDR / WAVE_OUTPUTMAPPER（windows.h 不默认带）

// ── Edge TTS 协议端点与请求头 ─────────────────────────────────
// 微软 Edge 浏览器内建的神经语音服务（与 edge-tts 项目同协议）。
namespace {
const char *kEdgeWssUrl =
    "wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/edge/v1?"
    "TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4";
const char *kSecMsGec = "X-Timestamp:2026-10-08T00:00:00.000Z\r\nContent-Type:application/json; charset=utf-8\r\nPath:speech.config\r\n\r\n";
const char *kVoice = "zh-CN-XiaoxiaoNeural";
}

// ffmpeg 定位（与主程序同约定，独立于 main.cpp 复制一份最小逻辑）
static QString ffmpegBin()
{
    const QString env = qEnvironmentVariable("STELARITH_AGENT_FFMPEG");
    if (!env.isEmpty() && QFile::exists(env)) return env;
    const QString env2 = qEnvironmentVariable("STE_QT_FFMPEG");
    if (!env2.isEmpty() && QFile::exists(env2)) return env2;
    const QString which = QString::fromLocal8Bit(qgetenv("PATH"));
    // PATH 里找 ffmpeg.exe
    for (const QString &dir : which.split(QLatin1Char(';'))) {
        const QString cand = dir + QStringLiteral("/ffmpeg.exe");
        if (QFile::exists(cand)) return cand;
    }
    return QString();
}

EdgeTts &EdgeTts::instance()
{
    static EdgeTts inst;
    return inst;
}

EdgeTts::EdgeTts(QObject *parent) : QObject(parent)
{
    m_ffmpeg.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_ffmpeg, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (code != 0) {
            m_busy = false;
            emit failed(QStringLiteral("Edge TTS 解码失败（ffmpeg 退出码 %1）").arg(code));
            return;
        }
        playPcm(m_pcm);
    });
    // 连接/解码双双护栏：总超时 20s，防止教室机弱网卡住
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, [this]() {
        if (!m_busy) return;
        m_busy = false;
        if (m_ws) { m_ws->close(); m_ws->deleteLater(); m_ws = nullptr; }
        m_ffmpeg.kill();
        emit failed(QStringLiteral("Edge TTS 超时（20s 无响应）"));
    });
}

EdgeTts::~EdgeTts()
{
    if (m_ws) { m_ws->deleteLater(); }
}

void EdgeTts::speak(const QString &text, const QString &voice, double volume)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) { emit finished(text); return; }
    if (m_busy) stop();          // 新朗读打断旧的

    m_text = t;
    m_voice = voice.isEmpty() ? QString::fromLatin1(kVoice) : voice;
    m_volume = volume >= 0.0 && volume <= 1.0 ? volume : 0.75;
    m_audio.clear();
    m_pcm.clear();
    m_busy = true;

    if (ffmpegBin().isEmpty()) {
        m_busy = false;
        emit failed(QStringLiteral("找不到 ffmpeg（Edge TTS 需要 ffmpeg 解码 mp3；可设 STE_QT_FFMPEG）"));
        return;
    }

    openSocket();
    m_idle.start(20000);
}

void EdgeTts::stop()
{
    m_busy = false;
    m_idle.stop();
    if (m_ws) { m_ws->close(); m_ws->deleteLater(); m_ws = nullptr; }
    m_ffmpeg.kill();
    emit finished(m_text);
}

void EdgeTts::openSocket()
{
    if (!m_ws) {
        m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        connect(m_ws, &QWebSocket::connected, this, [this]() {
            // 连接成功：先发 speech.config（握手）
            m_ws->sendTextMessage(QString::fromLatin1(kSecMsGec));
            sendSsml(m_text);
        });
        connect(m_ws, &QWebSocket::textMessageReceived, this, [this](const QString &) {
            // text 帧是元数据（Path:turn.start 等），音频在二进帧里
        });
        connect(m_ws, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &msg) {
            // 二进制帧 = mp3 音频分片，累积
            m_audio.append(msg);
        });
        connect(m_ws, &QWebSocket::disconnected, this, [this]() {
            // 正常收完：Edge 发完 turn.end 就断，此刻开始解码
            if (m_busy && !m_audio.isEmpty()) {
                decodeAndPlay();
            } else if (m_busy && m_audio.isEmpty()) {
                m_busy = false;
                m_idle.stop();
                emit failed(QStringLiteral("Edge TTS 没收到音频（可能无公网或语音不可用）"));
            }
        });
        connect(m_ws, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            if (!m_busy) return;
            m_busy = false;
            m_idle.stop();
            emit failed(QStringLiteral("Edge TTS 连接失败：%1").arg(
                m_ws ? m_ws->errorString() : QStringLiteral("未知错误")));
        });
    }
    m_ws->open(QUrl(QString::fromLatin1(kEdgeWssUrl)));
}

void EdgeTts::sendSsml(const QString &text)
{
    // SSML：告诉服务用哪个语音、多大的音量、说哪句话
    const QString voice = m_voice;
    const int volPct = qBound(25, int(m_volume * 100), 100);   // Edge 音量 25..100%
    const QString ssml = QStringLiteral(
        "<speak version='1.0' xml:lang='zh-CN'>"
        "<voice name='%1'>"
        "<prosody volume='%2%%' rate='+0%%'>%3</prosody>"
        "</voice></speak>").arg(voice).arg(volPct).arg(text);
    const QString payload = QStringLiteral(
        "X-RequestId:%1\r\nContent-Type:application/ssml+xml\r\n"
        "Path:ssml\r\nX-Timestamp:2026-10-08T00:00:00.000Z\r\n\r\n%2")
        .arg(QString::number(QDateTime::currentMSecsSinceEpoch()), ssml);
    m_ws->sendTextMessage(payload);
}

void EdgeTts::decodeAndPlay()
{
    // mp3 落临时文件 → ffmpeg 解码成 16kHz 单声道 16bit PCM
    QTemporaryFile tmp;
    if (!tmp.open()) { m_busy=false; emit failed(QStringLiteral("临时文件打不开")); return; }
    tmp.write(m_audio);
    tmp.flush();
    const QString ff = ffmpegBin();
    m_pcm.clear();
    m_ffmpeg.kill();
    m_ffmpeg.start(ff, {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-i"), tmp.fileName(),
        QStringLiteral("-f"), QStringLiteral("s16le"),
        QStringLiteral("-ar"), QStringLiteral("16000"),
        QStringLiteral("-ac"), QStringLiteral("1"),
        QStringLiteral("pipe:1")
    });
    connect(&m_ffmpeg, &QProcess::readyReadStandardOutput, this, [this]() {
        if (m_busy) m_pcm.append(m_ffmpeg.readAllStandardOutput());
    }, Qt::UniqueConnection);
}

void EdgeTts::playPcm(const QByteArray &pcm)
{
    // winmm waveOut 播放（16kHz 单声道 16bit）——与 audio_player 同族，独立实现
    if (pcm.isEmpty()) { m_busy=false; emit failed(QStringLiteral("解码结果为空")); return; }
    HWAVEOUT hwo = nullptr;
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 16000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = 16000 * 2;

    if (waveOutOpen(&hwo, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        m_busy=false; emit failed(QStringLiteral("打不开音频设备")); return;
    }
    // 一次播完整段（通知朗读 ≤120 字，量小；同步播放最简）
    WAVEHDR hdr = {};
    QByteArray buf = pcm;
    hdr.lpData = buf.data();
    hdr.dwBufferLength = DWORD(pcm.size());
    MMRESULT r1 = waveOutPrepareHeader(hwo, &hdr, sizeof(hdr));
    MMRESULT r2 = r1 == MMSYSERR_NOERROR ? waveOutWrite(hwo, &hdr, sizeof(hdr)) : r1;
    if (r2 == MMSYSERR_NOERROR) {
        // 等播完（约文本时长）；护栏 30s 兜底
        QElapsedTimer et; et.start();
        while ((waveOutUnprepareHeader(hwo, &hdr, sizeof(hdr)) == WAVERR_STILLPLAYING)
               && et.elapsed() < 30000) {
            Sleep(30);
        }
    }
    waveOutClose(hwo);
    m_busy = false;
    m_idle.stop();
    emit finished(m_text);
}