// 麦克风真机实验：把 Microphone 与 NoiseMonitor::captureRms 的真实行为测出来。
//
// 为什么要有它：之前我说"没有真机插拔试验"就收工了 —— 这是偷懒。
// 本机 waveInGetNumDevs() == 3（真麦克风阵列 + 虚拟设备），完全能做实验：
//   ① Mic 正常开/关，是否真的收到 PCM 数据（而不是"开着但没数据"）
//   ② Mic 长占时，噪音采集（短窗口 open）是否确实失败
//   ③ 反向：噪音短窗口占着时，Mic 开是否失败（这条以前没人测）
//   ④ 多次快速开关是否泄漏（设备句柄）
//
// 用法：python scripts/_mic_probe.py
#include "microphone.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QDebug>
#include <QThread>

#include <windows.h>
#include <mmsystem.h>

static int fails = 0;
static int passes = 0;
static void chk(const char *name, bool ok, const QString &detail)
{
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << name << " <- " << detail;
    ok ? ++passes : ++fails;
}

// 与 src/noise_monitor.cpp 的 captureRms 同口径（短窗口 open → 读 → 关）
static double captureRms(int sampleRate)
{
    if (waveInGetNumDevs() == 0) return -1.0;
    HWAVEIN h = nullptr;
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = static_cast<DWORD>(sampleRate) * 2;
    if (waveInOpen(&h, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        return -1.0;
    const int bytes = sampleRate * 2 * 150 / 1000;
    QByteArray buf(bytes, 0);
    WAVEHDR hdr = {};
    hdr.lpData = buf.data();
    hdr.dwBufferLength = static_cast<DWORD>(bytes);
    if (waveInPrepareHeader(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) {
        waveInClose(h); return -1.0;
    }
    if (waveInAddBuffer(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) {
        waveInUnprepareHeader(h, &hdr, sizeof(hdr)); waveInClose(h); return -1.0;
    }
    if (waveInStart(h) != MMSYSERR_NOERROR) {
        waveInUnprepareHeader(h, &hdr, sizeof(hdr)); waveInClose(h); return -1.0;
    }
    QElapsedTimer t; t.start();
    while (t.elapsed() < 600 && !(hdr.dwFlags & WHDR_DONE))
        QThread::msleep(5);
    const bool done = (hdr.dwFlags & WHDR_DONE);
    waveInStop(h);
    waveInUnprepareHeader(h, &hdr, sizeof(hdr));
    waveInClose(h);
    if (!done) return -1.0;
    const qint16 *s = reinterpret_cast<const qint16 *>(buf.constData());
    const int n = bytes / 2;
    if (n <= 0) return -1.0;
    double sum = 0;
    for (int i = 0; i < n; ++i) { const double v = s[i] / 32768.0; sum += v * v; }
    return std::sqrt(sum / n);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    qInfo().noquote() << "=== 本机音频设备 ===";
    const UINT ndev = waveInGetNumDevs();
    qInfo().noquote() << QStringLiteral("waveIn 设备数 = %1").arg(ndev);
    chk("本机至少有一个录音设备（否则实验无意义）", ndev > 0,
        QStringLiteral("ndev=%1").arg(ndev));

    // ══════ ① Mic 正常开 → 是否持续收到 PCM ══════
    //
    // ⚠️ 第一版探针在这里写错了：用 `while(...) { processEvents(); QThread::msleep(5); }`
    //   轮询，结果 2 秒只收到 1 帧，我一度据此怀疑 pump() 有 bug。
    //   实际是**探针自己**的问题：Microphone 靠 20ms 的 QTimer 驱动 pump()，
    //   而 msleep 阻塞事件循环 ⇒ 定时器走不动。
    //   改为真跑事件循环（QEventLoop + QTimer 到点退出）后帧数正常。
    //   教训：测异步的东西不能用忙等，否则测的是你的轮询节奏。
    {
        Microphone mic;
        int frames = 0;
        QObject::connect(&mic, &Microphone::pcmReady, [&frames](const QByteArray &) { ++frames; });

        const bool started = mic.start(5000);
        chk("① mic.start() 返回 true", started, QStringLiteral("started=%1").arg(started));

        // 真事件循环等 2 秒（不阻塞），让 20ms 定时器正常跑
        QEventLoop loop;
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();

        chk("① mic 持续吐 PCM 帧（不是「开着但只有第一帧」）", frames >= 50,
            QStringLiteral("2s 收到 %1 帧（20ms/帧 ⇒ 约 100）").arg(frames));
        chk("① mic.running() 与实际一致", mic.running() == started,
            QStringLiteral("running=%1").arg(mic.running()));
        mic.stop();
        chk("① stop() 后 running() 为假", !mic.running(),
            QStringLiteral("running=%1").arg(mic.running()));
    }

    // ══════ ② Mic 长占时，噪音短窗口采集会怎样 ══════
    //
    // ⚠️ 2026-10-31 实测推翻了原假设：
    //   noise_monitor.cpp 与 microphone.cpp 的注释都写着「设备被语音对讲独占时
    //   waveInOpen 会失败 / 采集失败是常态」，据此推理"长占期间噪音采不到"。
    //   **实测不成立**：mic 长占期间短窗口采集照样返回有效 rms（非 -1）。
    //   单独验证：同一进程连开两个 waveInOpen 全部 rc=0 —— 现代 Windows 音频栈
    //   走共享模式，多个打开方并存，不存在老式独占。
    // 故这里断言的是**实际行为**（能共存），而不是我以为的行为。
    // 若将来某台机器/驱动真的独占，这条会红 —— 那正是需要知道的信号。
    {
        Microphone mic;
        const bool started = mic.start(8000);
        if (!started) {
            qInfo().noquote() << "SKIP ②（mic 起不来，本机可能无设备）";
        } else {
            QThread::msleep(300);   // 让 waveIn 真正进入采集状态
            const double rms = captureRms(16000);
            chk("② mic 长占期间，噪音短窗口采集仍能共存（非独占）", rms >= 0.0,
                QStringLiteral("rms=%1（>=0 表示共享模式可共存；<0 说明该机真独占）").arg(rms));
            mic.stop();
            QThread::msleep(300);
        }
    }

    // ══════ ③ 反向：短窗口采集刚做完，Mic 能否立刻开 ══════
    // 这条以前没人测过：150ms 采样与 120s 长占的临界竞态。
    {
        int fails2 = 0;
        for (int i = 0; i < 5; ++i) {
            captureRms(16000);          // 立刻做一次短窗口
            Microphone mic;
            if (!mic.start(2000)) ++fails2;   // 紧接着开长占
            mic.stop();
        }
        chk("③ 连做 5 轮「短窗口 → 开 mic」不失败（临界竞态）", fails2 == 0,
            QStringLiteral("失败 %1/5 轮").arg(fails2));
    }

    // ══════ ④ 反复开关是否泄漏设备句柄 ══════
    {
        int bad = 0;
        for (int i = 0; i < 10; ++i) {
            Microphone mic;
            if (!mic.start(1000)) { ++bad; continue; }
            QThread::msleep(30);
            mic.stop();
        }
        chk("④ 连续开关 10 次全部成功（无句柄泄漏）", bad == 0,
            QStringLiteral("失败 %1/10").arg(bad));
        // 泄漏的判据：全部关掉后，短窗口采集应当还能拿到数据
        const double rms = captureRms(16000);
        chk("④ 全部释放后短窗口采集仍可用（设备没被占死）", rms >= 0.0,
            QStringLiteral("rms=%1").arg(rms));
    }

    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("[结论] PASS %1 / FAIL %2").arg(passes).arg(fails);
    return fails == 0 ? 0 : 1;
}
