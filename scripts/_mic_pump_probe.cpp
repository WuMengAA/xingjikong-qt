// Microphone::pump() 的续采问题验证。
//
// 疑点（2026-10-31 实测 2s 只收到 1 帧，而 20ms/帧应有 ~100 帧）：
//   pump() 里读满一帧后只做 waveInAddBuffer 重新入队，**没有 waveInUnprepareHeader**。
//   Windows waveIn 语义：一个 WAVEHDR 要重新复用，必须先 unprepare 再 prepare
//   （或至少 unprepare 后才能安全地重置 dwBytesRecorded/dwFlags）。
//   若真如此，第一帧之后缓冲区再也"填不满"，PCM 只来一次 → 语音通话 20ms 后就没声音。
//
// 用法：python scripts/_mic_probe2.py
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QDebug>
#include <QThread>
#include <QByteArray>
#include <cmath>

#include <windows.h>
#include <mmsystem.h>

static int fails = 0, passes = 0;
static void chk(const char *name, bool ok, const QString &detail)
{
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << name << " <- " << detail;
    ok ? ++passes : ++fails;
}

// 复刻 Microphone::pump() 的"重新入队"写法（不带 unprepare）—— 即当前产品代码
class RequeueNoUnprepare
{
public:
    bool start(int ms)
    {
        WAVEFORMATEX fmt = {};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = 16000;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 2;
        fmt.nAvgBytesPerSec = 32000;
        if (waveInOpen(&h, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            return false;
        buf.resize(640);
        memset(&hdr, 0, sizeof(hdr));
        hdr.lpData = buf.data();
        hdr.dwBufferLength = 640;
        if (waveInPrepareHeader(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) { waveInClose(h); h=nullptr; return false; }
        if (waveInAddBuffer(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR)     { waveInUnprepareHeader(h,&hdr,sizeof(hdr)); waveInClose(h); h=nullptr; return false; }
        if (waveInStart(h) != MMSYSERR_NOERROR)                            { waveInUnprepareHeader(h,&hdr,sizeof(hdr)); waveInClose(h); h=nullptr; return false; }
        Q_UNUSED(ms)
        return true;
    }
    // 与产品代码一致：只 addBuffer，不 unprepare
    int poll()
    {
        if (!h) return 0;
        if (hdr.dwBytesRecorded >= hdr.dwBufferLength) {
            frames++;
            waveInAddBuffer(h, &hdr, sizeof(hdr));   // ← 产品代码在这里
            return 1;
        }
        return 0;
    }
    void stop()
    {
        if (!h) { return; }
        waveInStop(h);
        waveInUnprepareHeader(h, &hdr, sizeof(hdr));
        waveInClose(h);
        h = nullptr;
    }
    int frames = 0;
private:
    HWAVEIN h = nullptr;
    WAVEHDR hdr = {};
    QByteArray buf;
};

// 正确写法：每次重新入队前 unprepare + 重置 + prepare
class RequeueCorrect
{
public:
    bool start(int ms)
    {
        WAVEFORMATEX fmt = {};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = 16000;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 2;
        fmt.nAvgBytesPerSec = 32000;
        if (waveInOpen(&h, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            return false;
        buf.resize(640);
        memset(&hdr, 0, sizeof(hdr));
        hdr.lpData = buf.data();
        hdr.dwBufferLength = 640;
        if (waveInPrepareHeader(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) { waveInClose(h); h=nullptr; return false; }
        if (waveInAddBuffer(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR)     { waveInUnprepareHeader(h,&hdr,sizeof(hdr)); waveInClose(h); h=nullptr; return false; }
        if (waveInStart(h) != MMSYSERR_NOERROR)                            { waveInUnprepareHeader(h,&hdr,sizeof(hdr)); waveInClose(h); h=nullptr; return false; }
        Q_UNUSED(ms)
        return true;
    }
    int poll()
    {
        if (!h) return 0;
        if (hdr.dwBytesRecorded >= hdr.dwBufferLength) {
            frames++;
            // 正确做法：先 unprepare，重置标志与已录字节，再 prepare + addBuffer
            waveInUnprepareHeader(h, &hdr, sizeof(hdr));
            hdr.dwBytesRecorded = 0;
            hdr.dwFlags &= ~WHDR_DONE;
            waveInPrepareHeader(h, &hdr, sizeof(hdr));
            waveInAddBuffer(h, &hdr, sizeof(hdr));
            return 1;
        }
        return 0;
    }
    void stop()
    {
        if (!h) return;
        waveInStop(h);
        waveInUnprepareHeader(h, &hdr, sizeof(hdr));
        waveInClose(h);
        h = nullptr;
    }
    int frames = 0;
private:
    HWAVEIN h = nullptr;
    WAVEHDR hdr = {};
    QByteArray buf;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    constexpr int kWindowMs = 2000;

    // ── 当前产品写法 ──
    {
        RequeueNoUnprepare m;
        chk("A 能开设备", m.start(kWindowMs), QStringLiteral(""));
        QElapsedTimer t; t.start();
        while (t.elapsed() < kWindowMs) {
            m.poll();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(5);
        }
        m.stop();
        qInfo().noquote() << QStringLiteral("    当前写法 2s 帧数 = %1（100 才正常）").arg(m.frames);
        chk("A 当前写法每次入队都补数据（帧数应接近 100）", m.frames >= 50,
            QStringLiteral("实际 %1 帧").arg(m.frames));
    }

    // ── 正确写法 ──
    {
        RequeueCorrect m;
        chk("B 能开设备", m.start(kWindowMs), QStringLiteral(""));
        QElapsedTimer t; t.start();
        while (t.elapsed() < kWindowMs) {
            m.poll();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(5);
        }
        m.stop();
        qInfo().noquote() << QStringLiteral("    正确写法 2s 帧数 = %1").arg(m.frames);
        chk("B 正确写法帧数接近 100", m.frames >= 50,
            QStringLiteral("实际 %1 帧").arg(m.frames));
    }

    qInfo().noquote() << "";
    qInfo().noquote() << QStringLiteral("[结论] PASS %1 / FAIL %2").arg(passes).arg(fails);
    return fails == 0 ? 0 : 1;
}
