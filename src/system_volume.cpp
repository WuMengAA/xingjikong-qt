// 系统主音量实现（2026-10-11 · Core Audio / WASAPI）
//
// 背景：原先 set_volume 用 winmm 的 waveOutSetVolume，它改的是**调用进程自己的**
// 波形输出音量；Vista 之后音量是按应用会话分的，教室机上正在放听力/放视频的程序
// 根本不受影响 —— 老师在管理端拖滑块，教室机声音纹丝不动，报上来就是"音量调不了"。
// 这里改成操作默认播放设备的端点主音量。

#include "system_volume.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <objbase.h>

#pragma comment(lib, "ole32.lib")

/**
 * 设**系统主音量**（Core Audio / WASAPI），0-100。
 *
 * 为什么不用 waveOutSetVolume：那是 WinMM 时代的接口，改的是**调用进程自己的**
 * 波形输出音量；Vista 之后系统音量是「每应用会话」的，教室机上放听力/放视频的
 * 那些进程根本不受它影响 —— 老师在管理端拖滑块，教室机声音一动不动。
 *
 * @param v      目标音量 0-100（调用方已夹紧）
 * @param errOut 失败原因（人话，直接回给管理端显示）
 * @return 成功 true；失败 false 且 errOut 有内容（调用方据此回落 waveOut）
 */
bool setSystemMasterVolume(int v, QString *errOut)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = SUCCEEDED(hr);
    // RPC_E_CHANGED_MODE 表示本线程已经以别的模式初始化过 COM —— 不算错误，继续用
    if (hr == RPC_E_CHANGED_MODE) hr = S_OK;
    if (FAILED(hr)) {
        if (errOut) *errOut = QStringLiteral("COM 初始化失败（0x%1）").arg((quint32)hr, 8, 16, QLatin1Char('0'));
        return false;
    }

    bool ok = false;
    IMMDeviceEnumerator *enumer = nullptr;
    IMMDevice *dev = nullptr;
    IAudioEndpointVolume *vol = nullptr;

    do {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), (void **)&enumer);
        if (FAILED(hr) || !enumer) { if (errOut) *errOut = QStringLiteral("建设备枚举器失败（0x%1）").arg((quint32)hr, 8, 16, QLatin1Char('0')); break; }

        hr = enumer->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        if (FAILED(hr) || !dev) { if (errOut) *errOut = QStringLiteral("取不到默认播放设备（可能没插声卡或在远程会话里）"); break; }

        hr = dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void **)&vol);
        if (FAILED(hr) || !vol) { if (errOut) *errOut = QStringLiteral("打不开主音量接口（0x%1）").arg((quint32)hr, 8, 16, QLatin1Char('0')); break; }

        // 音量是标量 0.0-1.0；系统音量条不是线性的，Scalar 版本已帮我们做了感知映射
        const float scalar = float(v) / 100.0f;
        hr = vol->SetMasterVolumeLevelScalar(scalar, nullptr);
        if (FAILED(hr)) { if (errOut) *errOut = QStringLiteral("写主音量失败（0x%1）").arg((quint32)hr, 8, 16, QLatin1Char('0')); break; }

        ok = true;
    } while (false);

    if (vol) vol->Release();
    if (dev) dev->Release();
    if (enumer) enumer->Release();
    if (needUninit) CoUninitialize();

    if (ok)
        qInfo("[agent-qt] 系统主音量已设为 %d%%（Core Audio）", v);
    return ok;
}
