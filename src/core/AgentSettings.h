// 星集控被控端 · 本地功能开关（跨 TU 共享）
// 2026-10-23 · AGENTS.md §10 第四刀地基：从 main.cpp 提到共享头文件。
//
// ⚠️ 为什么要提出来：它原先是**嵌套在 main.cpp 匿名 namespace（251 起）内**的
//    具名 namespace，函数又都带 `static` ⇒ 内部链接，**跨不了 TU**。
//    media_session_start 分支要下沉到 src/media/，那边必须能读到 playScheduleEnabled。
//
// 唯一变换：`static` → `inline`。头文件里的函数定义本来就该 inline
// （允许多个 TU 各持一份定义，语义与原先的 static 等价：都是只读 QSettings，无状态）。
// 函数体逐字照搬，**键名一个字符都没动** —— 键名是门禁契约。

#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

// 各子系统的入口点（speakText / notifyFromParams / noise_monitor_start / 灵动岛初始化）统一读这里。
// 这样"设置项"才真的"调用设置项"，而不是只摆几个开机自启开关（用户 2026-10-14 反思）。
namespace AgentSettings {
    inline bool noiseEnabled()  { return QSettings().value(QStringLiteral("feature/noiseEnabled"), true).toBool(); }
    inline void setNoiseEnabled(bool v) { QSettings().setValue(QStringLiteral("feature/noiseEnabled"), v); }
    inline bool voiceEnabled()  { return QSettings().value(QStringLiteral("feature/voiceEnabled"), true).toBool(); }
    inline void setVoiceEnabled(bool v) { QSettings().setValue(QStringLiteral("feature/voiceEnabled"), v); }
    inline bool notifyEnabled() { return QSettings().value(QStringLiteral("feature/notifyEnabled"), true).toBool(); }
    inline void setNotifyEnabled(bool v) { QSettings().setValue(QStringLiteral("feature/notifyEnabled"), v); }
    inline bool islandIdle()    { return QSettings().value(QStringLiteral("ui/islandIdle"), true).toBool(); }
    inline void setIslandIdle(bool v) { QSettings().setValue(QStringLiteral("ui/islandIdle"), v); }
    inline bool lightMode()     { return QSettings().value(QStringLiteral("ui/lightMode"), false).toBool(); }
    inline void setLightMode(bool v) { QSettings().setValue(QStringLiteral("ui/lightMode"), v); }

    // ── P0-0（2026-10-17 · 灵动岛第三方推岛治理）─────────────────────────────
    // 「允许第三方（插件）向灵动岛推送」。默认**开**：与既有能力保持一致 ——
    // 插件通道上线以来 island_show 一直是放行的，默认关会让已装插件一夜之间"静默失效"，
    // 那属于行为回归而不是治理。治理靠的是 pluginhost 里的长度/频率/来源/紧急档四道约束，
    // 这个开关是老师侧的"一键掐断"，不是默认态。
    // 键名 feature/pluginIslandPush；**键名是门禁契约**（shared-qml/scripts/test-plugin-island-gate.mjs
    // 会核对 key/默认值/接线三处一致）。
    inline bool pluginIslandPush() { return QSettings().value(QStringLiteral("feature/pluginIslandPush"), true).toBool(); }
    inline void setPluginIslandPush(bool v) { QSettings().setValue(QStringLiteral("feature/pluginIslandPush"), v); }

    // ── P3 播放时段管理：允许的播放时段窗口（本地时 "HH:MM-HH:MM"）──
    // 默认关闭 = 随时可播（不限制）；开启后只有落在窗口内才允许 media_session_start。
    inline bool playScheduleEnabled() { return QSettings().value(QStringLiteral("play/scheduleEnabled"), false).toBool(); }
    inline void setPlayScheduleEnabled(bool v) { QSettings().setValue(QStringLiteral("play/scheduleEnabled"), v); }
    inline QStringList playWindows() { return QSettings().value(QStringLiteral("play/windows"), QStringList()).toStringList(); }
    inline void setPlayWindows(const QStringList &v) { QSettings().setValue(QStringLiteral("play/windows"), v); }

    // ── C 批（2026-10-27）· 通知音量（岛态面双滑块的第二根）──────────────
    // 语义：**通知朗读（Edge TTS）的响度**，0-100，默认 75（对齐 edge_tts 既有默认 0.75）。
    // 与系统主音量的分工：系统音量管"整台设备多大声"（含课件/视频），
    // 通知音量只管"通知播报多大声"——老师把课堂内容开大、又不想通知炸耳，就要这个。
    // ⚠️ 提示音（whale_audio / winmm PlaySoundW）不在此列：winmm 没有音量参数，
    //    它只能跟系统音量（既有事实，别在 UI 上谎称能单独调）。
    inline int notifyVolume() { return qBound(0, QSettings().value(QStringLiteral("notify/volume"), 75).toInt(), 100); }
    inline void setNotifyVolume(int v) { QSettings().setValue(QStringLiteral("notify/volume"), qBound(0, v, 100)); }

    // ── C 批（2026-10-27）· 岛黑名单（通知来源）───────────────────────────
    // 老师把"老弹烦人通知的来源"拉黑后，该来源的通知**静默丢弃**（不弹、不响、不入历史）。
    // 来源键取自通知载荷 `from`（应用名 / 插件 id / 通道名，由下发方给，空 = 不适用）。
    // 与系统统一配置：落 QSettings（设备级），岛和管理端读到的是同一份。
    inline QStringList notifyBlacklist() { return QSettings().value(QStringLiteral("notify/blacklist"), QStringList()).toStringList(); }
    inline void setNotifyBlacklist(const QStringList &v) { QSettings().setValue(QStringLiteral("notify/blacklist"), v); }

    // ── 2026-10-28 模块三 · 设置页按原文补全（通用分类）──────────────────
    // 「安全模式（意外崩溃防护）」：上次没走有序退出（崩溃/断电/强杀）⇒ 本次不加载
    // 第三方插件。默认**开**：教室里一台机器被插件拖崩后，重启还要人跑去托盘关插件
    // 才能用，那是最糟的现场体验；默认开的代价只是"崩溃后少一个插件"。
    // 键名 settings/safeModeAuto（键名是契约，改它等于让老机器的设置失效）。
    inline bool safeModeAuto() { return QSettings().value(QStringLiteral("settings/safeModeAuto"), true).toBool(); }
    inline void setSafeModeAuto(bool v) { QSettings().setValue(QStringLiteral("settings/safeModeAuto"), v); }

    // 「自动校时」（时间配置）：开 = 启动后与每 30 分钟跟 NTP 对一次（TimesyncDomain::start()
    // 的实际开关，关掉后只保留人工偏移）。默认开，与 TimesyncDomain 既有行为一致。
    inline bool timeAutoSync() { return QSettings().value(QStringLiteral("settings/timeAutoSync"), true).toBool(); }
    inline void setTimeAutoSync(bool v) { QSettings().setValue(QStringLiteral("settings/timeAutoSync"), v); }
}
