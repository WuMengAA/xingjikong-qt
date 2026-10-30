// 星集控 · 管理端自更新（2026-10-08）
//
// 需求（用户 2026-10-08）：管理端要有「更新功能」—— 检查 → 下载 → 自动替换重启。
//
// 设计要点（每条都是"为什么这么写"，改动前请先读）：
//
// 1. **版本比较不在客户端做**。客户端只把本地版本号作为 `?current=` 发给云端，
//    由云端 `src/ota.js` 的 cmpVersion 判 hasUpdate —— 全项目**只有那一份实现**。
//    背景：此前两端各写一遍 C++ 比较，哨兵写错 ⇒「正式版 < 候选版」，
//    结果是云端发了正式版、客户端判成"已是最新"、教室机永远不升级（静默 bug）。
//    这里再写一遍 C++ 比较就是把那个坑重新挖一遍。
//
// 2. **替换自身只能"退出后由外部脚本做"**。exe 与它同目录的 Qt DLL 在运行时是被锁的，
//    进程内 robocopy 一定失败。所以流程是：解压到暂存 → 写一个 .cmd → detached 启动它 →
//    自己退出；.cmd 等进程消失后覆盖、再拉起新版本。
//
// 2b. **exe 直更（2026-10-14）**：云端清单的 url 以 .exe 结尾时走安装器通道——
//     下载 → sha256 校验 → 写 apply-update.cmd（等旧进程退 → `<安装器> /S` 静默安装 →
//     拉起新版本）→ 退出。zip 通道（2）保留兼容。两条通道都由包扩展名路由。
//     exe 通道**不做** installDirWritable 预检：安装器自身要管理员权（UAC），
//     程序目录不可写不该挡死更新（zip 通道才需要目录可写）。
//
// 3. **sha256 是硬门槛**。没有校验的"更新"等于任意代码执行（和被控端 self_update 同一口径）。
//    URL 只认 http(s)，sha256 必须是 64 位小写十六进制，体积有上限 —— 缺一不升。
//
// 4. **不覆盖用户配置**。包里带着一份默认 `viewer.env`（站点地址之类的默认值），
//    直接整目录覆盖会把老师配好的那份冲回默认。替换脚本里明确排除它。
//
// 5. **编码**：批处理按系统 ANSI 读（中文机是 GBK），所以脚本内容用 toLocal8Bit()
//    落盘（与全项目日志出口一致）。用 UTF-8 写会让含中文的安装路径在 cmd 里变乱码。
//
// 6. **更新通道（2026-10-20）**：检查带 `&channel=`（STE_VIEWER_CHANNEL，默认 stable）。
//    灰度管理端（channel=gray）从云端 ota-gray.json 拿候选版 —— 此前这里永远只读
//    stable，灰度设备拿得到 rc、管理端自己反而拿不到，灰度验证断了一条腿。
//
// 7. **周期自动检查 + 托盘气泡（2026-10-20）**：管理端常驻托盘，老师不会主动进
//    设置页点「检查更新」——没有周期检查，新版本只能等老师偶然点进去才发现。
//    启动 30s 后首查、默认每 4h 复查（STE_VIEWER_UPDATE_CHECK_SEC，0 = 关闭）。
//    **只提示、不自动安装**：老师可能正在投屏/监控，更新要重启进程，必须人点。
//    mandatory 版本用更醒的托盘气泡（见 newVersionFound 信号）。

#ifndef STELARITH_UPDATER_H
#define STELARITH_UPDATER_H

#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class QCryptographicHash;
class QTimer;

/**
 * 管理端自更新状态机（界面直接绑这几个属性）：
 *
 *   idle → checking → uptodate            （已是最新，结束）
 *                   → available           （发现新版，等用户点「立即更新」）
 *   available → downloading → verifying → extracting → launching（随后本进程退出）
 *   任何一步出错 → failed（message 里是能看懂的原因）
 */
class Updater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(qint64 sizeBytes READ sizeBytes NOTIFY changed)
    Q_PROPERTY(int percent READ percent NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool mandatory READ mandatory NOTIFY changed)
    /** 本机当前版本（编译期注入，与「关于」页显示的是同一个真源）。 */
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    /** 更新通道（STE_VIEWER_CHANNEL，默认 stable；gray = 灰度候选版）。 */
    Q_PROPERTY(QString channel READ channel CONSTANT)

public:
    /** @param wsUrl 云端的 WebSocket 地址（wss://host/ws/viewer），从中派生 http 基址。 */
    explicit Updater(const QString &wsUrl, QObject *parent = nullptr);
    ~Updater() override;

    QString state() const { return m_state; }
    QString message() const { return m_message; }
    QString latestVersion() const { return m_latestVersion; }
    QString notes() const { return m_notes; }
    qint64 sizeBytes() const { return m_size; }
    int percent() const { return m_percent; }
    bool mandatory() const { return m_mandatory; }
    QString currentVersion() const;
    QString channel() const { return m_channel; }
    /** 有活没干完（检查 / 下载 / 校验 / 解压），界面上按钮该禁掉。 */
    bool busy() const;

    /** 检查更新：GET <cloud>/api/public/ota?product=viewer&current=<本机版本>&channel=<通道>。 */
    Q_INVOKABLE void checkForUpdate();
    /** 一键更新：下载 → sha256 校验 → 解压到暂存 → 写替换脚本 → 退出自身。 */
    Q_INVOKABLE void installUpdate();
    /** 复位到 idle（重新检查前用；下载中不允许复位）。 */
    Q_INVOKABLE void reset();
    /**
     * 周期自动检查（2026-10-20）。firstDelayMs 后首查，之后每 intervalMs 复查；
     * intervalMs ≤ 0 = 不启用。正在忙（检查/下载中）时到点跳过，不打断用户操作。
     * 自动检查发现新版本会发 newVersionFound（由 main.cpp 弹托盘气泡）；
     * 手动检查不发 —— 用户正看着设置页，再弹气泡是噪音。
     */
    void startAutoCheck(int firstDelayMs, int intervalMs);
    void stopAutoCheck();

signals:
    void changed();
    /** 自动检查发现新版本。mandatory = 云端标记必须更新。 */
    void newVersionFound(const QString &version, bool mandatory, const QString &notes);

private:
    void setState(const QString &s, const QString &msg);
    void fail(const QString &why);
    /** 真正干活的检查；autoCheck 决定发现新版本时要不要发 newVersionFound。 */
    void runCheck(bool autoCheck);
    void onCheckFinished();
    void onDownloadReadyRead();
    void onDownloadFinished();
    /** 用系统 tar.exe（Win10 1803+ 自带 bsdtar）解压 zip；失败回落 PowerShell。 */
    bool extractZip(const QString &zip, const QString &dest, QString *why);
    // 异步解压（P1-1 性能修复 2026-10-30）：不阻塞主线程。done(ok, why) 在解压彻底
    // 结束后调用（成功 why 为空；失败 why 是原因）。替代原同步 extractZip 的 UI 冻结。
    void extractZipAsync(const QString &zip, const QString &dest,
                         std::function<void(bool, const QString &)> done);
    bool launchReplacer(const QString &staging, QString *why);
    /** exe 直更：写 apply-update.cmd（等旧进程退 → 安装器 /S → 拉起新版本）。 */
    bool launchExeInstaller(QString *why);
    /** 安装目录可写吗 —— 不可写就早点说，别下完 130MB 才发现。 */
    bool installDirWritable(QString *why) const;
    /** wss://host/ws/viewer → https://host（只换协议、砍掉 /ws/… 之后的部分）。 */
    QString httpBase() const;
    /** 清掉上一次更新留下的暂存/脚本（启动时调一次，失败不影响使用）。 */
    void cleanupOldDirs();

    QString m_wsUrl;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
    QCryptographicHash *m_hash = nullptr;
    QTimer *m_timeout = nullptr;   // 检查超时 / 下载"卡住"超时

    QString m_state = QStringLiteral("idle");
    QString m_message;
    QString m_latestVersion;
    QString m_notes;
    QString m_url;                 // 新版本 zip 地址
    QString m_sha;                 // 新版本 sha256（64 位小写十六进制）
    QString m_channel = QStringLiteral("stable");   // 更新通道（见头注释 6）
    qint64  m_size = 0;            // 声明体积
    qint64  m_got = 0;             // 已下载字节
    int     m_percent = 0;
    bool    m_mandatory = false;
    bool    m_lastCheckAuto = false;   // 本次检查是不是周期自动检查（决定发不发气泡信号）
    QString m_lastNotifiedVersion;     // 已经弹过气泡的版本（同一版本不重复烦用户）
    QTimer *m_autoTimer = nullptr;    // 周期自动检查（见头注释 7）

    QString m_zipPath;             // 下载落盘的包（zip 或 exe 安装器）
    bool    m_isExe = false;       // 本次更新包是 exe 安装器（走 launchExeInstaller）
    QString m_dir;                 // 更新工作目录（缓存区）
};

#endif // STELARITH_UPDATER_H
