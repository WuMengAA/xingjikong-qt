// 星集控被控端 · 系统媒体识别（SMTC，2026-10-22）
//
// 解决的问题：教室里那台机器「现在到底在放什么」，C++ 侧以前完全不知道。
// 现有 island_bridge 的 media*/mediaNext* 是 **VoiceHub 轮询来的排期播放器状态**，
// 语义是"我们的广播站播放器播到哪了"，跟"这台机器上真在响的是什么"是两码事：
// 学生自己开个网易云，VoiceHub 那边依然显示"未播放"，岛面上什么也看不到。
//
// 本类补的就是这个缺口：直接问 Windows 系统。
//
// ── 数据源：SMTC ────────────────────────────────────────────────
// Win10 1809+ 的 GlobalSystemMediaTransportControlsSessionManager
// （就是你按音量键时左上角弹出来的那条媒体控制条背后的 API）。
// 网易云 / QQ音乐 / 酷狗 / Chrome / Edge / PotPlayer / foobar2000 都会往里注册，
// 0 成本拿到 歌名 / 歌手 / 专辑 / 播放态 / 进度 / 封面。
//
// 为什么不做 WASAPI 录音 + 音频指纹：要 15 秒样本、要联网查库、延迟 15 秒起，
// 教室里"下一首放什么"等不起。而且指纹只能答"像哪首歌"，答不了"谁在放"。
//
// ── 线程模型（重要）──────────────────────────────────────────────
// WinRT 的 IAsyncOperation::get() 是**阻塞等待**，在 UI 线程调会把整岛冻住。
// 所以：本类自持一条 std::thread（MTA，init_apartment(multi_threaded)），
// 800ms 轮询后把快照通过 QMetaObject::invokeMethod(QueuedConnection) 排回
// 本对象所属线程再改成员、发信号 —— QML 绑定永远在 GUI 线程求值，不跨线程读成员。
//
// ── 刻意不做的事（如实记账，别当遗漏）────────────────────────────
// · **不做 WASAPI 活动进程降级**：SMTC 拿不到的播放器，WASAPI 也只给 PID/进程名，
//   拿不到歌名；再靠 GetWindowText 猜标题是"这台机器能用、换台机器就废"的假功能，
//   猜错了比不显示更糟。宁可 available=false 让 UI 自己决定隐藏，也不给会撒谎的降级。
// · **不做 WinRT 事件订阅**：MediaPropertiesChanged 需要在 MTA 里维持强引用并小心吊销，
//   翻车表现是"偶发掉订阅"（最难查）。800ms 轮询的成本只是一次进程内 RPC，可忽略。
// · **不做封面 ImageProvider**：封面统一转 PNG 落 %TEMP% 一个固定文件，
//   QML 用 coverPath + coverRevision 破缓存即可 —— 不引入"挂错 engine 全空"的老坑。
//
// 头文件里**不出现任何 winrt/windows.h**：全塞进 .cpp 的 Impl，
// 避免 <windows.h> 的 min/max 宏污染其它编译单元（本项目 274 个文件共用这套头）。

#ifndef STELARITH_SMTC_MONITOR_H
#define STELARITH_SMTC_MONITOR_H

#include <QObject>
#include <QString>

class SmtcMonitor : public QObject
{
    Q_OBJECT

    // ── 能力与状态 ──────────────────────────────────────────
    // available：SMTC 会话管理器拿到了没有。false = 系统太老 / 请求被拒，
    //            UI 应当整块隐藏，而不是显示空歌名。
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // active：当前有会话（不代表在播，可能暂停）。playing 才代表"正在响"。
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)

    // ── 元数据 ──────────────────────────────────────────────
    Q_PROPERTY(QString appId   READ appId   NOTIFY changed) // 原始 AUMID，排障用
    Q_PROPERTY(QString appName READ appName NOTIFY changed) // 友好名（网易云音乐 / Chrome…）
    Q_PROPERTY(QString title   READ title   NOTIFY changed)
    Q_PROPERTY(QString artist  READ artist  NOTIFY changed)
    Q_PROPERTY(QString album   READ album   NOTIFY changed)

    // ── 进度（单独信号，避免每 800ms 把整套内容属性刷一遍）──
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY positionChanged)

    // ── 封面 ────────────────────────────────────────────────
    // coverPath = file:///…/stelarith-smtc-cover.png（无封面时为空串）
    // QML: Image { source: smtc.coverPath + (smtc.coverPath ? "?v=" + smtc.coverRevision : "") }
    Q_PROPERTY(QString coverPath     READ coverPath     NOTIFY changed)
    Q_PROPERTY(int     coverRevision READ coverRevision NOTIFY changed)

    // 最近一次失败原因（初始化/轮询异常），正常情况下是空串。
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

    // ── 控制能力位（2026-10-22 加）─────────────────────────────
    // 这些**不是**我们决定的，是播放器自己声明的。网易云/QQ音乐/Edge 各不相同，
    // 而且同一播放器在"暂停中 / 无歌曲 / 播广告"时也会变。
    // ⚠️ UI 必须按这些位禁用按钮，不要假设"是播放器就能暂停"。
    //    有些播放器只上报元数据、完全不响应控制（canPause=false）。
    Q_PROPERTY(bool canPlay         READ canPlay         NOTIFY changed)
    Q_PROPERTY(bool canPause        READ canPause        NOTIFY changed)
    Q_PROPERTY(bool canNext         READ canNext         NOTIFY changed)
    Q_PROPERTY(bool canPrevious     READ canPrevious     NOTIFY changed)
    Q_PROPERTY(bool canSeek         READ canSeek         NOTIFY changed)

public:
    static SmtcMonitor *instance();

    // 一次轮询的结果。字段全是 Qt 基础类型，**跨线程按值传递**：
    // 工作线程构造 → QueuedConnection 拷贝 → GUI 线程消费，两边不共享可变状态。
    // 之所以放在头文件，是因为它要作为 lambda 捕获物穿过 QMetaObject::invokeMethod。
    struct Snapshot
    {
        bool    available = false;
        bool    active = false;
        bool    playing = false;
        QString appId;
        QString appName;
        QString title;
        QString artist;
        QString album;
        QString lastError;
        qint64  posMs = 0;
        qint64  durMs = 0;
        QString coverPath;   // 本地路径；空 = 本首没封面 / 没换歌
        bool    canPlay = false;
        bool    canPause = false;
        bool    canNext = false;
        bool    canPrevious = false;
        bool    canSeek = false;
    };

    // 幂等：重复调 start() 不会起第二条线程。
    void start();
    void stop();

    bool    available() const;
    bool    active() const;
    bool    playing() const;
    QString appId() const;
    QString appName() const;
    QString title() const;
    QString artist() const;
    QString album() const;
    qint64  positionMs() const;
    qint64  durationMs() const;
    QString coverPath() const;
    int     coverRevision() const;
    QString lastError() const;
    bool    canPlay() const;
    bool    canPause() const;
    bool    canNext() const;
    bool    canPrevious() const;
    bool    canSeek() const;

    // ── 控制（2026-10-22 加）─────────────────────────────────
    // 全部**异步**：只把命令丢进工作线程的命令队列就返回，绝不在 UI 线程阻塞。
    // 结果不直接返回 —— 执行完工作线程会立刻重采一次，UI 靠 playing/positionMs 变化感知。
    // ⚠️ 这是**媒体键语义**，不是"播放指定歌曲"。SMTC 没有"放这首"的接口，
    //    要指定内容只能走各播放器自己的协议（网易云是 orpheus://，见 SmtcLauncher）。
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlayPause();   // 按当前 playing 决定 play/pause
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);

    // 立刻醒一次工作线程（换歌/切播放器后想让 UI 尽快跟上时用）。
    // 只置标志，不做任何阻塞操作 —— 从 UI 线程调是安全的。
    Q_INVOKABLE void refreshNow();

signals:
    void changed();
    void positionChanged();

private:
    explicit SmtcMonitor(QObject *parent = nullptr);
    ~SmtcMonitor() override;

    // 只由工作线程经 QueuedConnection 调用（即永远在 GUI 线程执行）。
    void applySnapshot(const Snapshot &s);

    struct Impl;
    Impl *d = nullptr;

    bool    m_available = false;
    bool    m_active = false;
    bool    m_playing = false;
    QString m_appId;
    QString m_appName;
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_lastError;
    qint64  m_positionMs = 0;
    qint64  m_durationMs = 0;
    QString m_coverPath;   // 本地文件路径（非 URL），对外 getter 再转 file://
    int     m_coverRevision = 0;
    bool    m_canPlay = false;
    bool    m_canPause = false;
    bool    m_canNext = false;
    bool    m_canPrevious = false;
    bool    m_canSeek = false;
    bool    m_quitHooked = false;   // aboutToQuit 只连一次（见 start() 里的说明）
};

#endif // STELARITH_SMTC_MONITOR_H
