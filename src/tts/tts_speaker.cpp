// TTS 统一播报实现（2026-10-30 从 src/main.cpp 抽出，main 拆分第一刀）。
// 原代码逐字搬迁（含注释），仅把文件作用域 static 全局收进文件内匿名 namespace，
// 外部可见符号只剩 speakText() 一个。依赖全部是既有独立模块，无 main.cpp 全局依赖。
#include "tts_speaker.h"

#include <QString>
#include <QObject>
#include <QTimer>
#include <QApplication>   // qApp
#include <QDebug>         // qWarning
#include <string>
#include <windows.h>
#include <sapi.h>

#pragma comment(lib, "sapi.lib")     // ISpVoice（通知 TTS）—— 从 src/main.cpp 随模块迁出

#include "../edge_tts.h"            // EdgeTts 单例（finished / failed 信号）
#include "../core/AgentSettings.h"  // voiceEnabled() / notifyVolume()
#include "../system_volume.h"       // setSystemMasterVolume()

namespace {

// TTS 朗读（Windows SAPI 5 · 微软免费离线语音）。失败**不抛**、只记日志 ——
// 通知弹窗是主链路，朗读只是增强；SAPI 不可用时（极端环境）通知照常显示。
//
// ⚠️ 2026-10-09 改**异步**（用户反馈"tts 会拖慢通知显示"）：
//    原来用同步 Speak（SPF_DEFAULT），一次朗读会把主线程整个卡住（一句几百 ms ~ 数秒）。
//    而调用点是"先把通知上屏、紧接着朗读"—— 通知窗口虽然 show() 了，主线程却卡在朗读里出不来，
//    界面要等朗读结束才真正画出来，看起来就是"通知迟迟不弹/显示被拖慢"。
//    改法：常驻一个 ISpVoice（**不能每次 Speak 完就 Release** —— 释放会把正在异步朗读的声音掐掉），
//    用 SPF_ASYNC 立即返回、把朗读交给 SAPI 自己的后台线程；再加 SPF_PURGEBEFORESPEAK，
//    新的一条顶掉还没播完的旧的一条，避免多条通知叠着念成一锅粥。
//    COM 由 Qt 的 Windows 平台插件在主线程初始化（原同步版本能跑即是证明），此处不重复 CoInitialize。
ISpVoice *g_spVoice = nullptr;

// SAPI 兜底朗读（原 speakText 本体，2026-10-11 更名保留）。
// 现在由 speakText() 在 Edge TTS 不可用/失败时回调进来 —— 离线场景（无公网、
// 无 ffmpeg、语音服务不通）只有它还能出声，所以**必须保留且行为不变**。
void speakTextSapi(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (!g_spVoice) {
        HRESULT hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice,
                                      (void **)&g_spVoice);
        if (FAILED(hr) || !g_spVoice) {
            g_spVoice = nullptr;   // 失败别留半个指针，下次进来重试
            qWarning().noquote() << QStringLiteral("[agent-qt] TTS 不可用（SAPI 初始化失败 HR=0x%1），通知不朗读，仅显示")
                                        .arg((uint)hr, 0, 16);
            return;
        }
    }
    // UTF-8 → 宽字符；SAPI 默认读中文需要系统装中文语音（Win10 自带 Huihui 等）
    const QString say = text.left(120);   // 防长广播把朗读拖到天荒地老
    const int wlen = say.length() + 1;
    std::wstring wstr(wlen, L'\0');
    say.toWCharArray(&wstr[0]);
    const HRESULT hr = g_spVoice->Speak(wstr.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
    if (FAILED(hr))
        qWarning().noquote() << QStringLiteral("[agent-qt] TTS Speak 失败 HR=0x%1（通知已显示，朗读跳过）")
                                    .arg((uint)hr, 0, 16);
}

// ── TTS 统一入口（2026-10-11 · A5 接线 §6.4）────────────────────────────
// 需求 §6.4：通知朗读**优先 Edge 神经语音**（联网时音色自然），失败自动**回退 SAPI**（离线兜底）。
//
// 为什么要回退：Edge TTS 走公网 WebSocket（speech.platform.bing.com），教室机
//   · 无公网 / 弱网超时；· 没装 ffmpeg（mp3 解码链断掉）—— 这些都不是"崩"，是"读不出来"。
//   fail-silent 红线：绝不假装读过了，而是**如实降级到 SAPI**，让通知至少能听见。
//
// ⚠️ Edge 的失败是**异步**的（连不上/超时/解码失败要等回调），所以不能"先问能不能用、
//    再决定读哪条"（那个判断当场返回不了真话）。正确做法：**先让 Edge 读**，
//    同时把它的 failed 接一根线 —— 真失败时再补一次 SAPI，且只补一次。
// ⚠️ 去重护栏：failed 可能来自**上一条**朗读（Edge 单例，新的一条会打断旧的，
//    被顶掉的那条也会 failed）。若不做判据，通知 A 的失败会把正在由通知 B 朗读的
//    内容再念一遍。判据：发起朗读时把"本条 token"记进 g_ttsCurToken；
//    failed 回调里若 g_ttsCurToken 已被更新（说明有更新的一条顶上），就**不**回退。
quint64 g_ttsSeq = 0;            // speakText 调用计数
quint64 g_ttsCurToken = 0;       // 当前"正在读"的那条的 token（0=没有）
QString g_ttsCurText;            // 当前"正在读"的文本（回退 SAPI 用）
bool g_ttsFallbackWired = false;

// ── 2026-10-11 · 语音播报音量序列（用户点单）───────────────────────────────
// 需求：开启语音播报发布通知时，**先**把音量调整到 75%，**等 TTS 播报完成后**恢复 50%。
// 为什么：教室机平时主音量 50%，直接播报在嘈杂环境听不清；播完必须收回，否则下一节
// 课件/视频会突然很响。抬/收都走 setSystemMasterVolume（Core Audio 主音量），与
// set_volume 指令同一条路径 —— 改的是**系统主音量**，不是本进程音量（否则白改）。
const int kTtsVolSpeak   = 75;   // 播报期间抬到
const int kTtsVolRestore = 50;   // 播报完成后恢复

// 播报结束（Edge 成功 finished / 回退 SAPI 读完）把主音量收回 50%。
// tok 用来判「这条还是不是当前那条」：被更新的一条顶掉之后，旧条的回调不再收音量，
// 否则会把正在播的新一条提前掐回 50。
void ttsRestoreVolume(quint64 tok)
{
    if (tok != 0 && tok != g_ttsSeq) return;
    QString verr;
    if (!setSystemMasterVolume(kTtsVolRestore, &verr))
        qWarning().noquote() << QStringLiteral("[agent-qt] TTS 播完恢复主音量 %1% 失败：%2")
                                    .arg(kTtsVolRestore).arg(verr);
}

bool g_ttsFinishedWired = false;

} // namespace

void speakText(const QString &text)
{
    // P0-1（2026-10-14）：本地「语音播报」开关关了就不朗读——通知/循环提醒照常显示，只是不念。
    if (!AgentSettings::voiceEnabled()) return;
    const QString say = text.trimmed();
    if (say.isEmpty()) return;

    EdgeTts &edge = EdgeTts::instance();

    // 只接一次：failed → 若仍是"当前这条"，回退 SAPI
    if (!g_ttsFallbackWired) {
        g_ttsFallbackWired = true;
        QObject::connect(&edge, &EdgeTts::failed, qApp, [](const QString &reason) {
            qWarning().noquote() << QStringLiteral("[agent-qt] Edge TTS 不可用（%1），回退 SAPI 朗读").arg(reason);
            // 被更新的一条顶掉了（token 已换）⇒ 这条 failed 是"过期"的，不再出声，
            // 否则会把正在由新一条朗读的内容重复念一遍。
            if (g_ttsCurToken == 0 || g_ttsCurToken != g_ttsSeq) return;
            const quint64 tok = g_ttsCurToken;
            const QString fallback = g_ttsCurText;
            g_ttsCurToken = 0;   // 只回退一次
            speakTextSapi(fallback);        // 同步阻塞：读完才返回
            ttsRestoreVolume(tok);          // 回退路也走「播完恢复 50」
        });
    }

    // 只接一次：finished → 播报正常结束，恢复主音量 50
    if (!g_ttsFinishedWired) {
        g_ttsFinishedWired = true;
        QObject::connect(&edge, &EdgeTts::finished, qApp, [](const QString &text) {
            // 念完的已经不是"当前这条"（中间又来了新通知）⇒ 不收，交给新那条收
            if (text != g_ttsCurText) return;
            ttsRestoreVolume(g_ttsSeq);
        });
    }

    // 记下"当前这条"：token = 本次调用号
    const quint64 tok = ++g_ttsSeq;
    g_ttsCurToken = tok;
    g_ttsCurText  = say;

    // ── 2026-10-21（审查 #4）：音量超时兜底 ──────────────────────────────
    // Edge TTS 走公网 WS，弱网/挂起时 finished/failed 两个信号都可能永远不来 ⇒
    // 系统主音量永久停在 75%（下一节课件/视频全被这个残留音量影响，没人会把它和
    // "两小时前一条通知"联系起来）。单次 QTimer：60s 后仍没收到收尾信号就强制恢复
    // 50。ttsRestoreVolume 内部有 token 校验 —— 被新一条顶掉时拒绝收，由新一条负责。
    QTimer::singleShot(60 * 1000, qApp, [tok] { ttsRestoreVolume(tok); });

    // ── 播报前先把系统主音量抬到 75%（需求：先调音量，再播）──
    {
        QString verr;
        if (!setSystemMasterVolume(kTtsVolSpeak, &verr))
            qWarning().noquote() << QStringLiteral("[agent-qt] TTS 播报前抬主音量 %1% 失败：%2")
                                        .arg(kTtsVolSpeak).arg(verr);
    }

    // 先让 Edge 读；它会异步报成功(finished)或失败(failed)
    // C 批（2026-10-27）：通知音量（Edge TTS 响度）走 AgentSettings::notifyVolume，
    // 与「系统主音量」是两回事——系统音量管整台设备，这里只管通知播报多大声。
    edge.speak(say, QString(), AgentSettings::notifyVolume() / 100.0);
}
