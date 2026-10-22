// 星集控被控端 · 系统媒体识别（SMTC）实现，2026-10-22
//
// 设计取舍与线程模型见 smtc_monitor.h 顶部注释。这里只记**实现层面的坑**：
//
// 1) windows.h 必须先于 winrt/* 引入，并且要 WIN32_LEAN_AND_MEAN + NOMINMAX。
//    不加 NOMINMAX 时 <windows.h> 的 min/max 宏会把 std::min/std::max 和
//    Qt 里所有 min/max 调用全毒掉，报错位置离真正的 include 有几百行。
// 2) TimeSpan 是 std::chrono::duration<int64_t, 100ns>，不能当毫秒直接读，
//    必须 duration_cast<std::chrono::milliseconds>。
// 3) GetSessions() 返回的是**当前所有**注册到系统的会话。多个播放器同时开着时
//    取「第一个 Playing 的」，都暂停才退回 GetCurrentSession()。
//    只认 GetCurrentSession() 会在"网易云暂停着、Chrome 在放"时显示暂停那首。
// 4) 封面每首都读会很贵（有的播放器给的是没压缩的大图）。只在**曲目键变化**时读，
//    并且统一转成 PNG 落盘 —— 原字节可能是 bmp/webp，QML Image 嗅探不认扩展名，
//    但转 PNG 后行为确定，也避免每次换歌都换文件名（固定路径 + revision 破缓存）。
// 5) 任何 WinRT 调用都可能抛 hresult_error（服务没起、会话被杀、权限被策略禁掉）。
//    全部就地 catch，降级成 available=false + lastError，绝不让异常穿过线程边界。

#include "smtc_monitor.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMetaObject>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
// ⚠️ 必须显式包含：GetSessions() 返回 IVectorView，range-for 依赖
//    Windows.Foundation.Collections 里的 begin/end 适配。漏了这个头，
//    报错是 `C3536 "<end>$L0": 初始化之前无法使用` —— 看着像循环写错，
//    实际是迭代器根本没定义（本项目已踩过一次，记在这里）。
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

namespace {

constexpr int kPollMs = 800;           // 轮询间隔
constexpr int kMgrRetryMs = 5000;      // 会话管理器拿失败后的重试间隔
constexpr int kPositionPushMs = 1000;  // 进度信号最小间隔（避免 800ms 刷爆绑定）
constexpr qint64 kCoverMaxBytes = 8 * 1024 * 1024;

// AUMID / 进程名 → 人看得懂的名字。
// 注意：这是**纯展示层**，认不出来就退回去掉 .exe 的原始串，绝不编造。
QString friendlyAppName(const QString &raw)
{
    if (raw.isEmpty())
        return QString();

    QString id = raw;
    // UWP：Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic → 取 ! 前
    const int bang = id.indexOf(QLatin1Char('!'));
    if (bang > 0)
        id = id.left(bang);
    // 包族名后缀 _<publisher hash>（8 位以上十六进制）
    const int us = id.lastIndexOf(QLatin1Char('_'));
    if (us > 0 && id.size() - us - 1 >= 8)
        id = id.left(us);

    const QString low = id.toLower();
    static const struct { const char *needle; const char *name; } kMap[] = {
        { "cloudmusic", "网易云音乐" },
        { "netease",    "网易云音乐" },
        { "qqmusic",    "QQ音乐" },
        { "kugou",      "酷狗音乐" },
        { "kuwo",       "酷我音乐" },
        { "spotify",    "Spotify" },
        { "potplayer",  "PotPlayer" },
        { "foobar",     "foobar2000" },
        { "vlc",        "VLC" },
        { "zunemusic",  "媒体播放器" },
        { "music.ui",   "媒体播放器" },
        { "msedge",     "Edge" },
        { "chrome",     "Chrome" },
        { "firefox",    "Firefox" },
        { "bilibili",   "哔哩哔哩" },
        { "musicfox",   "MusicFox" },
    };
    for (const auto &e : kMap) {
        if (low.contains(QLatin1String(e.needle)))
            return QString::fromUtf8(e.name);
    }

    QString s = id;
    if (s.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        s.chop(4);
    const int dot = s.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        s = s.mid(dot + 1);
    return s;
}

} // namespace

// ── Impl ────────────────────────────────────────────────────────────────────
struct SmtcMonitor::Impl
{
    SmtcMonitor *owner = nullptr;

    std::thread th;
    std::atomic<bool> stop{ false };
    std::mutex mu;
    std::condition_variable cv;
    bool wake = false;

    // GUI 线程独占的"去重记忆"（不跨线程）
    QString lastKey;
    qint64 lastPosPushMs = 0;

    // 以下只在 worker 线程碰
    QElapsedTimer clk;
    bool mgrTried = false;
    QString lastCoverKey;
    QString coverFilePath;

    // ── 控制命令队列（2026-10-22 加）──────────────────────────
    // UI 线程只往这里塞命令，worker 线程负责真正调 WinRT。
    // 为什么不做成"UI 线程直接调 session 方法"：session 句柄归 worker 独占，
    // 跨线程用 COM 对象得多一层 marshal，而且 Try*Async().get() 会阻塞 UI。
    enum class Cmd { Play, Pause, Toggle, Next, Prev, Seek };
    struct CmdItem { Cmd kind; qint64 arg = 0; };
    std::mutex cmdMu;
    std::deque<CmdItem> cmdQ;

#if defined(_WIN32)
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };
    // 上一轮选中的会话。命令打在它身上 —— 保留跨轮是安全的（COM 引用计数）。
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession curSession{ nullptr };
#endif

    Snapshot poll();
    void drainCommands();
    void post(Cmd kind, qint64 arg);   // UI 线程调用
    void runLoop();
};

#if defined(_WIN32)

namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;
namespace wf  = winrt::Windows::Foundation;

static qint64 msOf(const wf::TimeSpan &ts)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(ts).count();
}

static QString hstr(const winrt::hstring &h)
{
    if (h.empty())
        return QString();
    return QString::fromWCharArray(h.c_str(), static_cast<int>(h.size()));
}

// 封面：读原始字节 → QImage → 统一转 PNG 落 %TEMP% 固定文件。
// 失败返回空串（UI 显示占位），不抛。
static QString writeCover(const wss::IRandomAccessStreamReference &thumb, const QString &dirPath)
{
    if (!thumb)
        return QString();
    try {
        auto stream = thumb.OpenReadAsync().get();
        if (!stream)
            return QString();
        const quint64 size = stream.Size();
        if (size == 0 || static_cast<qint64>(size) > kCoverMaxBytes)
            return QString();

        wss::DataReader reader(stream);
        const auto loaded = reader.LoadAsync(static_cast<uint32_t>(size)).get();
        auto buf = reader.DetachBuffer();
        if (loaded == 0 || buf.Length() == 0)
            return QString();

        QImage img;
        if (!img.loadFromData(reinterpret_cast<const uchar *>(buf.data()),
                              static_cast<int>(buf.Length())))
            return QString();
        QDir().mkpath(dirPath);
        const QString path = QDir(dirPath).filePath(QStringLiteral("stelarith-smtc-cover.png"));
        if (!img.save(path, "PNG"))
            return QString();
        return path;
    } catch (const winrt::hresult_error &) {
        return QString();
    } catch (...) {
        return QString();
    }
}

SmtcMonitor::Snapshot SmtcMonitor::Impl::poll()
{
    SmtcMonitor::Snapshot s;
    s.available = mgr != nullptr;
    if (!mgr)
        return s;

    try {
        // 选会话：优先"第一个在播的"，都暂停才退回当前会话
        wmc::GlobalSystemMediaTransportControlsSession session{ nullptr };
        bool anyPlaying = false;
        for (const auto &cand : mgr.GetSessions()) {
            if (!cand)
                continue;
            if (cand.GetPlaybackInfo().PlaybackStatus()
                == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                session = cand;
                anyPlaying = true;
                break;
            }
        }
        if (!session)
            session = mgr.GetCurrentSession();
        if (!session) {
            lastCoverKey.clear();   // 没有会话 → 下次出现时重新读封面
            curSession = nullptr;   // 命令队列打在旧句柄上是危险的，一起清掉
            return s;               // active=false
        }
        curSession = session;       // 供 drainCommands() 使用

        s.active = true;
        s.appId = hstr(session.SourceAppUserModelId());
        s.appName = friendlyAppName(s.appId);

        const auto info = session.GetPlaybackInfo();
        s.playing = anyPlaying
                    || info.PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;

        // 能力位：播放器自己声明的。有的播放器什么都不支持，UI 得据此禁用按钮。
        // Controls() 返回的是**值类型 struct**（不是指针），不需要判空。
        const auto ctl = info.Controls();
        s.canPlay     = ctl.IsPlayEnabled();
        s.canPause    = ctl.IsPauseEnabled();
        s.canNext     = ctl.IsNextEnabled();
        s.canPrevious = ctl.IsPreviousEnabled();
        s.canSeek     = ctl.IsPlaybackPositionEnabled();

        const auto tl = session.GetTimelineProperties();
        s.posMs = qMax<qint64>(0, msOf(tl.Position()));
        s.durMs = qMax<qint64>(0, msOf(tl.EndTime()));

        const auto props = session.TryGetMediaPropertiesAsync().get();
        if (props) {
            s.title  = hstr(props.Title());
            s.artist = hstr(props.Artist());
            if (s.artist.isEmpty())
                s.artist = hstr(props.AlbumArtist());
            s.album  = hstr(props.AlbumTitle());
        }

        // 有歌名但没歌手时，别让 UI 显示 " - "，artist 留空由 QML 决定怎么拼。
        const QString key = s.appId + QLatin1Char('\x1f') + s.title + QLatin1Char('\x1f') + s.artist;
        if (key != lastCoverKey) {
            lastCoverKey = key;
            if (props) {
                const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
                const QString p = writeCover(props.Thumbnail(), dir);
                if (!p.isEmpty()) {
                    coverFilePath = p;
                    s.coverPath = p;
                }
            }
        }
    } catch (const winrt::hresult_error &e) {
        s.lastError = QStringLiteral("hresult 0x%1: %2")
                          .arg(static_cast<quint32>(e.code()), 8, 16, QLatin1Char('0'))
                          .arg(hstr(e.message()));
        s.active = false;
        s.playing = false;
    } catch (const std::exception &e) {
        s.lastError = QString::fromUtf8(e.what());
        s.active = false;
        s.playing = false;
    } catch (...) {
        s.lastError = QStringLiteral("未知异常");
        s.active = false;
        s.playing = false;
    }
    return s;
}

// 执行 UI 线程排进来的控制命令（只在 worker 线程调）。
// 用 curSession（上一轮 poll() 选的），命令执行完由下一轮 poll 把新状态推回 UI。
void SmtcMonitor::Impl::drainCommands()
{
    std::deque<CmdItem> batch;
    {
        std::lock_guard<std::mutex> lk(cmdMu);
        if (cmdQ.empty())
            return;
        batch.swap(cmdQ);
    }
    if (!curSession) {
        qWarning("[smtc] 控制命令被丢弃：当前没有可用会话");
        return;
    }

    for (const auto &c : batch) {
        try {
            bool ok = false;
            switch (c.kind) {
            case Cmd::Play:   ok = curSession.TryPlayAsync().get(); break;
            case Cmd::Pause:  ok = curSession.TryPauseAsync().get(); break;
            case Cmd::Next:   ok = curSession.TrySkipNextAsync().get(); break;
            case Cmd::Prev:   ok = curSession.TrySkipPreviousAsync().get(); break;
            case Cmd::Seek:
                // TryChangePlaybackPositionAsync 的单位是 100ns，不是毫秒
                ok = curSession.TryChangePlaybackPositionAsync(c.arg * 10000).get();
                break;
            case Cmd::Toggle: {
                const auto st = curSession.GetPlaybackInfo().PlaybackStatus();
                ok = (st == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
                         ? curSession.TryPauseAsync().get()
                         : curSession.TryPlayAsync().get();
                break;
            }
            }
            // 返回 false = 播放器拒绝（能力位声明了但实现没接，很常见）。
            // **必须记日志**：否则表现是"点了没反应"，排障时无从下手。
            if (!ok)
                qWarning("[smtc] 控制命令 %d 被播放器拒绝（返回 false）", int(c.kind));
        } catch (const winrt::hresult_error &e) {
            qWarning("[smtc] 控制命令 %d 抛异常 0x%08x", int(c.kind),
                     static_cast<unsigned>(e.code()));
        } catch (...) {
            qWarning("[smtc] 控制命令 %d 抛未知异常", int(c.kind));
        }
    }
}

void SmtcMonitor::Impl::runLoop()
{
    // MTA：不参与 UI 消息泵，任何线程都能进；同步 get() 在 MTA 里不会死锁。
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        SmtcMonitor::Snapshot bad;
        bad.lastError = QStringLiteral("WinRT 初始化失败（init_apartment）");
        QMetaObject::invokeMethod(owner, [o = owner, bad] { o->applySnapshot(bad); },
                                  Qt::QueuedConnection);
        return;
    }

    clk.start();

    while (!stop.load()) {
        // ── 会话管理器：拿不到就每 5 秒重试一次 ──
        if (!mgrTried || (!mgr && clk.elapsed() >= kMgrRetryMs)) {
            mgrTried = true;
            clk.restart();
            try {
                mgr = wmc::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                if (mgr)
                    mgrTried = true;
            } catch (const winrt::hresult_error &e) {
                mgr = nullptr;
                SmtcMonitor::Snapshot bad;
                bad.lastError = QStringLiteral("会话管理器不可用（0x%1）：%2")
                                    .arg(static_cast<quint32>(e.code()), 8, 16, QLatin1Char('0'))
                                    .arg(hstr(e.message()));
                QMetaObject::invokeMethod(owner, [o = owner, bad] { o->applySnapshot(bad); },
                                          Qt::QueuedConnection);
            } catch (...) {
                mgr = nullptr;
            }
        }

        if (mgr) {
            drainCommands();   // 先把命令打出去，紧接着 poll 就能看到结果
            SmtcMonitor::Snapshot s = poll();
            QMetaObject::invokeMethod(owner, [o = owner, s] { o->applySnapshot(s); },
                                      Qt::QueuedConnection);
        }

        std::unique_lock<std::mutex> lk(mu);
        cv.wait_for(lk, std::chrono::milliseconds(kPollMs), [this] { return stop.load() || wake; });
        wake = false;
        if (stop.load())
            break;
    }

    mgr = nullptr;
    try {
        winrt::uninit_apartment();
    } catch (...) {
    }
}

#else // 非 Windows（Android 等）：保持接口存在，永远 available=false

SmtcMonitor::Snapshot SmtcMonitor::Impl::poll()
{
    SmtcMonitor::Snapshot s;
    s.lastError = QStringLiteral("SMTC 仅在 Windows 上可用");
    return s;
}

void SmtcMonitor::Impl::drainCommands() {}
void SmtcMonitor::Impl::runLoop() {}

#endif

// ── SmtcMonitor ─────────────────────────────────────────────────────────────

SmtcMonitor *SmtcMonitor::instance()
{
    static SmtcMonitor *inst = nullptr;
    if (!inst)
        inst = new SmtcMonitor();
    return inst;
}

SmtcMonitor::SmtcMonitor(QObject *parent)
    : QObject(parent)
    , d(new Impl)
{
    d->owner = this;
}

SmtcMonitor::~SmtcMonitor()
{
    stop();
    delete d;
    d = nullptr;
}

void SmtcMonitor::start()
{
    if (d->th.joinable())
        return;

    if (auto *app = QCoreApplication::instance()) {
        // 退出时必须停线程：std::thread 还 joinable 就被析构 = std::terminate。
        // ⚠️ 这里**不能**用 Qt::UniqueConnection：带 functor 的 connect 不支持该标志，
        //    实测会直接 fail-fast（0xC0000409），而且它连 warning 都不打 ——
        //    表现就是"一调 start() 进程就没了"。用成员布尔自己保证只连一次。
        if (!m_quitHooked) {
            m_quitHooked = true;
            QObject::connect(app, &QCoreApplication::aboutToQuit, this, [this] { stop(); });
        }
    }

    d->stop.store(false);
    d->th = std::thread([this] { d->runLoop(); });
}

void SmtcMonitor::stop()
{
    if (!d->th.joinable())
        return;
    {
        std::lock_guard<std::mutex> lk(d->mu);
        d->stop.store(true);
    }
    d->cv.notify_all();
    d->th.join();
}

void SmtcMonitor::refreshNow()
{
    {
        std::lock_guard<std::mutex> lk(d->mu);
        d->wake = true;
    }
    d->cv.notify_all();
}

// ── 控制命令（UI 线程侧，只入队）────────────────────────────────────────────
void SmtcMonitor::Impl::post(Cmd kind, qint64 arg)
{
    {
        std::lock_guard<std::mutex> lk(cmdMu);
        if (cmdQ.size() > 32)
            cmdQ.clear();          // 异常情况下防堆积（UI 连点/按键重复）
        cmdQ.push_back(CmdItem{ kind, arg });
    }
    {
        std::lock_guard<std::mutex> lk(mu);
        wake = true;
    }
    cv.notify_all();
}

void SmtcMonitor::play()             { d->post(Impl::Cmd::Play, 0); }
void SmtcMonitor::pause()            { d->post(Impl::Cmd::Pause, 0); }
void SmtcMonitor::togglePlayPause()  { d->post(Impl::Cmd::Toggle, 0); }
void SmtcMonitor::next()             { d->post(Impl::Cmd::Next, 0); }
void SmtcMonitor::previous()         { d->post(Impl::Cmd::Prev, 0); }
void SmtcMonitor::seek(qint64 ms)    { d->post(Impl::Cmd::Seek, qMax<qint64>(0, ms)); }

// 从 worker 线程经 QueuedConnection 进来，永远在 GUI 线程执行 —— 这里改成员是安全的。
void SmtcMonitor::applySnapshot(const Snapshot &s)
{
    const QString key = QStringList{
        s.available ? QStringLiteral("1") : QStringLiteral("0"),
        s.active ? QStringLiteral("1") : QStringLiteral("0"),
        s.playing ? QStringLiteral("1") : QStringLiteral("0"),
        // 能力位进 key：同一首歌在不同状态下（广告/切歌）可用控制会变，UI 要跟着禁/启用
        QStringLiteral("%1%2%3%4%5").arg(s.canPlay).arg(s.canPause).arg(s.canNext)
            .arg(s.canPrevious).arg(s.canSeek),
        s.appId, s.title, s.artist, s.album, s.lastError
    }.join(QLatin1Char('\x1f'));

    const bool contentChanged = (key != d->lastKey);
    const bool newCover = !s.coverPath.isEmpty();

    if (contentChanged) {
        d->lastKey = key;
        m_available = s.available;
        m_active = s.active;
        m_playing = s.playing;
        m_appId = s.appId;
        m_appName = s.appName;
        m_title = s.title;
        m_artist = s.artist;
        m_album = s.album;
        m_lastError = s.lastError;
        m_canPlay = s.canPlay;
        m_canPause = s.canPause;
        m_canNext = s.canNext;
        m_canPrevious = s.canPrevious;
        m_canSeek = s.canSeek;
        m_positionMs = s.posMs;
        m_durationMs = s.durMs;
        if (newCover) {
            m_coverPath = s.coverPath;
            ++m_coverRevision;
        }
        emit changed();
        d->lastPosPushMs = 0;
        // 内容变了顺带把进度也推一次，避免 QML 先看到新歌名、进度条还是旧的
        emit positionChanged();
        return;
    }

    // 内容没变：只按节流推进度。位置本身就每轮都在动，绝不能每 800ms 全量 NOTIFY。
    if (s.playing && qAbs(s.posMs - m_positionMs) >= 250) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - d->lastPosPushMs >= kPositionPushMs) {
            d->lastPosPushMs = now;
            m_positionMs = s.posMs;
            m_durationMs = s.durMs;
            emit positionChanged();
        }
    }
}

bool SmtcMonitor::available() const { return m_available; }
bool SmtcMonitor::active() const { return m_active; }
bool SmtcMonitor::playing() const { return m_playing; }
QString SmtcMonitor::appId() const { return m_appId; }
QString SmtcMonitor::appName() const { return m_appName; }
QString SmtcMonitor::title() const { return m_title; }
QString SmtcMonitor::artist() const { return m_artist; }
QString SmtcMonitor::album() const { return m_album; }
qint64 SmtcMonitor::positionMs() const { return m_positionMs; }
qint64 SmtcMonitor::durationMs() const { return m_durationMs; }
QString SmtcMonitor::coverPath() const
{
    if (m_coverPath.isEmpty())
        return QString();
    return QUrl::fromLocalFile(m_coverPath).toString();
}
int SmtcMonitor::coverRevision() const { return m_coverRevision; }
QString SmtcMonitor::lastError() const { return m_lastError; }
bool SmtcMonitor::canPlay() const { return m_canPlay; }
bool SmtcMonitor::canPause() const { return m_canPause; }
bool SmtcMonitor::canNext() const { return m_canNext; }
bool SmtcMonitor::canPrevious() const { return m_canPrevious; }
bool SmtcMonitor::canSeek() const { return m_canSeek; }
