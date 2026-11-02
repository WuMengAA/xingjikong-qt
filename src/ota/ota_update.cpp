#include "ota_update.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStorageInfo>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>

#include <windows.h>

#include "ota_state.h"              // 路径/状态机（otaPrevPath/otaPendingPath/otaPaused/otaNote* …）
#include "../system/exam_mode.h"           // ExamMode::inst()（非强制升级要避开考试）
#include "../island/island.h"              // IslandOverlay（发现新版本推送）
#include "../system/winlauncher.h"         // stelarithStartHiddenDetached（回滚脚本静默拉起）

// ── 注入上下文（main() 早期 otaUpdateInit 填充）──
OtaDeps *g_otaDeps = nullptr;

// ── main.cpp 文件作用域外部链接全局：按既有 extern 模式复用 ──
extern const char *kAppVersion;
extern QString g_latestVer, g_updateUrl, g_updateSha, g_updateNotes;
extern bool g_updateChecked;
extern bool g_updateMandatory;
extern QString g_otaPromptedVer;      // 原文件作用域 static，#15 提升为外部链接（仅本域读写）
extern QAction *g_actUpdate;
extern QSystemTrayIcon *g_tray;
extern bool g_unconfigured;

/** 比版本号（semver 子集，2026-10-07 对齐 DeepSeek Harness 同款 X.Y.Z[-(alpha|rc).N]）：
 *  a>b → 1，a<b → -1，相等 → 0。
 *  ① 先比核心三段 X.Y.Z 的数字（缺段补 0 —— 0.10.0 > 0.9.9 这种字符串比会反的地方）；
 *  ② 核心打平后，先比**阶段**："alpha" < "rc" < 正式版（跟 semver 的预发布标识符一致，
 *     阶段名不同就不再比号 —— alpha.5 也小于 rc.1）；
 *  ③ 同核心同阶段才比号（rc.2 > rc.1、alpha.3 > alpha.1）。
 *  例：0.6.23-alpha.1 < 0.6.23-rc.1 < 0.6.23；0.6.23-rc.1 > 0.6.22；0.6.23 < 0.6.24-rc.1。
 *  兼容：非本标准的老后缀（"0.4.0-v1"、"-beta"）照旧当空气，行为跟以前一致。
 *  这套语义的参考实现和断言在 scripts/test-version-naming.mjs（工作区根）—— 改这里之前
 *  先跑一遍那个脚本，它挂了就说明两边语义分叉了。
 *
 *  为什么必须认后缀：候选版也要能被 OTA 识别成「有更新」。旧实现把后缀当空气，
 *  0.6.23 与 0.6.23-rc.1 会判成相等 ⇒ 托盘的「发现新版本」整段失灵。 */
int compareVersion(const QString &a, const QString &b)
{
    // 拆成「核心段」+「阶段 + 阶段号」：阶段是 alpha / rc，号是 -(alpha|rc).N 里的 N；
    // 没有合法后缀（含 -v1/-beta 这些老后缀）时 rank 保持 RANK_RELEASE、号 = -1，下面当"正式版"处理。
    // rank：alpha=0 < rc=1 < 正式=2（同 semver 的 stage 序，正式版最大）。
    enum { RANK_ALPHA = 0, RANK_RC = 1, RANK_RELEASE = 2 };
    auto parse = [](const QString &s, int *rankOut, int *numOut) -> QStringList {
        QString t = s.trimmed();
        // ⚠️ 无阶段的初值必须是 RANK_RELEASE(2)，不能图省事写 0 —— 0 是 RANK_ALPHA。
        // 踩过：初值 0 + parse 里写 -1 两套并存，核心打平后「正式版 < 候选版」，
        // 于是 rc 转正发正式版时 compareVersion(云端, 本机) 判成 -1 ⇒ 托盘不提示升级（B1/B2/B17 三条断言抓的）。
        *rankOut = RANK_RELEASE;
        *numOut = -1;
        const int dash = t.indexOf(QLatin1Char('-'));
        if (dash > 0) {
            const QString suf = t.mid(dash + 1);                 // 形如 "rc.3" / "alpha.2"
            int dot = suf.indexOf(QLatin1Char('.'));
            if (dot > 0) {
                const QString name = suf.left(dot);
                const QString num = suf.mid(dot + 1);
                bool ok = false;
                const int n = num.toInt(&ok);
                if (ok && n >= 1) {
                    if (name == QLatin1String("alpha")) *rankOut = RANK_ALPHA;
                    else if (name == QLatin1String("rc")) *rankOut = RANK_RC;
                    if (*rankOut >= 0) *numOut = n;
                }
            }
            t = t.left(dash);                                    // 后缀摘掉，核心段交给下面比
        }
        return t.split(QLatin1Char('.'));
    };
    auto seg = [](const QStringList &parts, int i) -> int {
        if (i >= parts.size()) return 0;                          // 段数不够 → 补 0
        int n = 0; bool got = false;
        for (const QChar c : parts.at(i)) {
            if (c.isDigit()) { n = n * 10 + c.digitValue(); got = true; }
            else if (got) break;                                  // 数字之后的东西（如 "-beta"）忽略
        }
        return got ? n : 0;
    };

    int ra = RANK_RELEASE, rb = RANK_RELEASE, na = -1, nb = -1;
    const QStringList sa = parse(a, &ra, &na), sb = parse(b, &rb, &nb);
    for (int i = 0; i < 3; ++i) {
        const int x = seg(sa, i), y = seg(sb, i);
        if (x != y) return x > y ? 1 : -1;
    }
    // 核心打平：先分阶段（alpha < rc < 正式），同阶段再分号。
    if (ra != rb) return ra > rb ? 1 : -1;
    if (na < 0 || nb < 0) return 0;
    return na == nb ? 0 : (na > nb ? 1 : -1);
}

// startSelfUpdate 经 g_otaDeps 注入（定义仍在 main.cpp 匿名域，内部链接）；
// maybeAutoUpdate 在下文定义（本文件内 static），这里提前声明让 applyUpdateInfo 直接触发自动升级。
static void maybeAutoUpdate();

/** 消化云端给的升级信息：记下来 + 该提示就提示 + 该自动装就自动装。 */
void applyUpdateInfo(const QJsonObject &pay)
{
    g_latestVer   = pay.value(QStringLiteral("latestVersion")).toString().trimmed();
    g_updateUrl   = pay.value(QStringLiteral("updateUrl")).toString().trimmed();
    g_updateSha   = pay.value(QStringLiteral("updateSha256")).toString().trimmed();
    g_updateNotes = pay.value(QStringLiteral("updateNotes")).toString().trimmed();
    g_updateChecked = true;
    // mandatory 取值三种：true / false / null（没发布过版本）。
    // 只有 **true** 才算"云端要求强制升级"—— null 是"未知"，绝不能当成强制。
    g_updateMandatory = pay.value(QStringLiteral("updateMandatory")).toBool();

    if (g_latestVer.isEmpty()) {
        // 如实说：云端还没发布过版本（ota.json 不存在）。这不是"已是最新"。
        qInfo("[agent-qt] 升级检查：云端尚未发布过版本（无 ota.json）→ 本机 v%s 继续跑", kAppVersion);
        return;
    }
    if (compareVersion(g_latestVer, QString::fromLatin1(kAppVersion)) > 0) {
        const bool canUpgrade = !g_updateUrl.isEmpty() && g_updateSha.length() == 64;
        if (g_updateMandatory) {
            qInfo("[agent-qt] ⬆ 云端 v%s 标注为**强制升级**（本机 v%s，可升=%s）—— 到点自动装，不用老师点",
                  qPrintable(g_latestVer), kAppVersion, canUpgrade ? "yes" : "no");
            // 延迟 8 秒再动手：让 registered 握手彻底收尾，别在云端还在发别的东西时抢带宽。
            QTimer::singleShot(8000, [] { maybeAutoUpdate(); });
        } else {
            qInfo("[agent-qt] ⬆ 发现新版本 v%s（本机 v%s）%s —— 右键托盘 →「设置与信息…」可升级",
                  qPrintable(g_latestVer), kAppVersion,
                  canUpgrade ? "" : "（⚠ 清单缺 url/sha256，先别升，让管理员补 ota.json）");
            // 2026-10-17：非强制也要能自己装上去 —— 延迟后再判安全窗口（见 maybeAutoUpdate）。
            // 不在这里直接装：刚弹完提示就重启设备，观感太差。
            // 延迟做成可配（STE_QT_OTA_AUTO_DELAY_SEC，默认 30s）：探针要在几十秒内跑完一轮，
            // 写死 30s 会让"判据到底生效没"变得难以取证。
            {
                int delaySec = qEnvironmentVariableIntValue("STE_QT_OTA_AUTO_DELAY_SEC");
                if (delaySec <= 0) delaySec = 30;
                delaySec = qBound(1, delaySec, 3600);
                QTimer::singleShot(delaySec * 1000, [] { maybeAutoUpdate(); });
            }
        }
        // 2026-10-17：同一个版本**只主动提示一次**（周期性复查每 30 分钟会再来一遍；
        // 每次都弹岛/弹托盘就成了骚扰）。强制升级那条路不受影响 —— maybeAutoUpdate
        // 自带 ota-state 退避与 ota-pause 刹车，该重试还会重试。
        const bool firstPrompt = (g_otaPromptedVer != g_latestVer);
        if (firstPrompt) g_otaPromptedVer = g_latestVer;

        // P0-3（2026-10-14）：托盘气泡容易错过 ⇒ 同时经灵动岛主动推送（带 changelog + 操作按钮）。
        // 非强制更新不打扰太久：20s 后自动收起；点了「立即更新」复用既有 self_update 链路。
        if (firstPrompt && g_tray && IslandOverlay::instance()) {
            const QString desc = g_updateNotes.isEmpty()
                ? QStringLiteral("当前 v%1，点「立即更新」即可静默升级。")
                      .arg(QString::fromLatin1(kAppVersion))
                : g_updateNotes;
            QStringList acts{ QStringLiteral("立即更新"), QStringLiteral("稍后") };
            IslandOverlay::instance()->showWithActions(
                QStringLiteral("发现新版本 v%1").arg(g_latestVer), desc,
                QStringLiteral("download"), IslandOverlay::Form::Centered, acts,
                [](const QString &a) {
                    if (a == QStringLiteral("立即更新")) {
                        QJsonObject p;
                        p.insert(QStringLiteral("url"), g_updateUrl);
                        p.insert(QStringLiteral("sha256"), g_updateSha);
                        p.insert(QStringLiteral("version"), g_latestVer);
                        g_otaDeps->startSelfUpdate(QStringLiteral("island"), p);   // 与管理端同一条 self_update 链路
                    }
                }, 20000, false);
        }
        if (firstPrompt && g_actUpdate) {
            g_actUpdate->setVisible(true);
            g_actUpdate->setText(g_updateMandatory
                                     ? QStringLiteral("将自动升级到 v%1（云端强制）").arg(g_latestVer)
                                     : QStringLiteral("发现新版本 v%1（点此查看）").arg(g_latestVer));
        }
        if (firstPrompt && g_tray) {
            g_tray->showMessage(
                QStringLiteral("星集控 · 被控端"),
                g_updateMandatory
                    ? QStringLiteral("云端要求升级到 v%1（当前 v%2）。\n联网后自动安装，不用手动点——"
                                     "过程中会短暂重启，一两分钟回来。")
                          .arg(g_latestVer, QString::fromLatin1(kAppVersion))
                    : QStringLiteral("有新版本 v%1（当前 v%2）。\n右键托盘图标 →「设置与信息…」即可升级。")
                          .arg(g_latestVer, QString::fromLatin1(kAppVersion)),
                QSystemTrayIcon::Information, 12000);
        }
    } else {
        qInfo("[agent-qt] 升级检查：已是最新（云端 v%s，本机 v%s）",
              qPrintable(g_latestVer), kAppVersion);
    }
}


/* ══════════════════════════════════════════════════════════════════════════
 * 强制自动升级（2026-10-06）
 *
 * 以前的行为：云端标 mandatory 也没用 —— 只有托盘提示 + 面板里一个「立即升级」按钮，
 * 必须有人在教室机右键点一下才装。**全校教室机没人点 = 永远升不上来**，
 * 于是 mandatory 只是一个好看的开关。现在补齐成一条完整链路：
 *
 *   云端 ota.json.mandatory=true
 *     → register 回执带 updateMandatory
 *     → 本机判定"到点了"就**自己下载、自己校验、自己静默装**（老师全程不用碰）
 *     → 装完新版本自己起来；起不来（或起来注册不上）就把升级前的旧 exe 放回去
 *
 * 三道保命设计（缺一道都不敢让机器无人值守换自己）：
 *   ① **退避**：失败一次等 3 分钟，再失败 15 分钟 / 1 小时 / 4 小时 / 12 小时封顶。
 *      网络抖一下不至于半夜反复拉包。
 *   ② **回滚**：装之前先备一份当前 exe（留在数据目录，不随安装目录被覆盖）+
 *      写 pending 标记 + 删掉"最近一次启动成功"标记。
 *   ③ **看门狗**：回滚判定写在**升级前生成**的 ota-relaunch.bat 里 ——
 *      它体内是**旧版本的字节**，所以哪怕新 exe 一启动就崩，狗还在，照样能把机器救回来。
 *      （只看 agent 自己是不行的：新 exe 崩了，它自己也已经没了。）
 *
 * 判定"新版本起没起来"只看一件事：**有没有连上云端注册成功**（register 成功即写 ota-last-ok.txt）。
 * 网络本来就断的机器不算新版本坏了 —— 见过不到云端的机器被误判回滚，白降一版。
 * 所以 `g_lastOnlineAt == 0`（从没连通过）时永不回滚。
 *
 * 紧急刹车（强制升级最要紧的东西）：数据目录里放一个 `ota-pause`（空文件）即暂停自动升级。
 * 某个版本真把机器搞挂时，运维能不卸包、不重编，往机器上扔一个文件就止血。
 * ══════════════════════════════════════════════════════════════════════════ */

// otaIsBusy（"上一个下载任务还在跑吗"）经 g_otaDeps 注入 —— 定义在 main.cpp 匿名域（内部链接）。
// 原本这里用「函数声明绕开变量作用域」的写法：g_otaBusy 的真身在匿名 namespace 里，
// 在前面再来一次同名 static 变量会被判成两个实体（C2086 重定义）。迁出后该绕法不再需要。

// 回滚判定窗口：升级后新版本这么多秒内没注册成功 → 回滚。
// ⚠️ extern 是必须的：main.cpp 启动时的回滚看门狗也读它，而命名空间 const 默认内部链接。
extern const int kOtaRollbackMs = 240 * 1000;
// 装之前要求的数据盘余量（安装包 ~70MB，取 4 倍冗余：下载 + NSIS 解压 + 备份）—— 仅本文件内使用
static const qint64 kOtaMinFreeBytes = 400LL * 1024 * 1024;

/** 备份当前 exe 到数据目录（回滚的命根子）。失败返回空串 —— 见调用处宁可不装。 */
QString otaBackupExe()
{
    const QString src = QCoreApplication::applicationFilePath();
    const QString dst = otaPrevPath();
    QDir().mkpath(QFileInfo(dst).absolutePath());

    // ⚠️ 2026-10-18：目标位置可能被上一版留着（或被杀软/别的实例占着句柄）。
    //   原来这里无视 remove() 的返回值就往下走 ⇒ 后面的 copy 必然失败，日志却只有一句
    //   "备份失败"，分不清是"删不掉"还是"拷不动"。现在删不掉就改名挪开，并分别记账。
    if (QFile::exists(dst) && !QFile::remove(dst)) {
        const QString aside = dst + QStringLiteral(".stale");
        QFile::remove(aside);
        if (!QFile::rename(dst, aside)) {
            qWarning("[agent-qt] ⚠ 旧备份既删不掉也挪不开（%s）", qPrintable(dst));
            return QString();
        }
        qWarning("[agent-qt] ⚠ 旧备份删不掉，已挪到 %s", qPrintable(aside));
    }

    if (!QFile::copy(src, dst)) {
        qWarning("[agent-qt] ⚠ 备份当前 exe 失败（%s → %s）", qPrintable(src), qPrintable(dst));
        return QString();
    }
    if (QFileInfo(dst).size() != QFileInfo(src).size()) {
        qWarning("[agent-qt] ⚠ 备份大小对不上（%lld vs %lld），不认这个备份",
                 (long long)QFileInfo(dst).size(), (long long)QFileInfo(src).size());
        QFile::remove(dst);
        return QString();
    }
    // 备份是给回滚用的，权限要跟原件一致；设不上也不致命（安装器以管理员身份覆盖时会重设）。
    QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner);
    qInfo("[agent-qt] 💾 已备份当前 exe（%lld 字节）用于回滚 → %s",
          (long long)QFileInfo(dst).size(), qPrintable(dst));
    return dst;
}

void otaWritePending(const QString &prevExe, const QString &targetVer)
{
    QJsonObject pend;
    pend.insert(QStringLiteral("target"), targetVer);
    pend.insert(QStringLiteral("prevExe"), prevExe);
    pend.insert(QStringLiteral("ts"), (double)QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(otaBaseDir());
    QFile f(otaPendingPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("[agent-qt] ⚠ 写不下 pending 标记（%s）—— 看门狗分不清新旧版本", qPrintable(otaPendingPath()));
        return;
    }
    f.write(QJsonDocument(pend).toJson(QJsonDocument::Indented));
    f.flush();
}

/** 新版本起来并注册成功 → 标记 + 清掉 pending（清不掉就会被人造的启动失败误回滚）。 */
void otaMarkStartedOk()
{
    QDir().mkpath(otaBaseDir());
    QFile f(otaOkPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QStringLiteral("v%1 %2\n")
                    .arg(QString::fromLatin1(kAppVersion),
                         QDateTime::currentDateTime().toString(Qt::ISODate))
                    .toUtf8());
        f.flush();
    }
    if (QFile::exists(otaPendingPath())) {
        QFile::remove(otaPendingPath());
        qInfo("[agent-qt] ✅ 新版本 v%s 已连上云端，本次升级确认成功（回滚标记已清）", kAppVersion);
    }
    otaNoteSuccess();
}

/** 回滚：把旧 exe 放回去并拉起来。当前进程（新版本）随后退出。 */
void otaRollback()
{
    const QString prev = otaPrevPath();
    const QString exe  = QCoreApplication::applicationFilePath();
    if (!QFile::exists(prev)) {
        qWarning("[agent-qt] ⚠ 回滚备份不在（%s）—— 只能让这台设备保持现状，等下次升级 retry", qPrintable(prev));
        QFile::remove(otaPendingPath());
        otaNoteFailure(QStringLiteral("回滚备份不存在"));
        return;
    }
    if (QFileInfo(prev).size() == 0) {
        qWarning("[agent-qt] ⚠ 回滚备份是 0 字节（%s），拒绝用空壳覆盖", qPrintable(prev));
        QFile::remove(otaPendingPath());
        otaNoteFailure(QStringLiteral("回滚备份为 0 字节"));
        return;
    }
    qWarning("[agent-qt] 🔙 回滚：用 %s 覆盖 %s，再把它拉起来", qPrintable(prev), qPrintable(exe));
    // 覆盖自己正在跑的 exe 一定失败（文件被占），所以交出一个独立 cmd 去干：
    // 杀进程 → 等 3 秒（自己也退干净）→ 拷回旧版 → 清 pending → 拉起旧版。
    // 2026-10-21（审查 #10）：taskkill 的 /IM 原来是硬编码 stelarith-agent-qt.exe ——
    //   exe 被重命名（学校定制/重打包）时 taskkill 失败 ⇒ && 链断 ⇒ 后续 copy/start
    //   全不执行，回滚静默失效。改为从 applicationFilePath() 取当前进程的文件名拼。
    const QString exeName = QFileInfo(exe).fileName();
    const QString cmd = QStringLiteral(
        "taskkill /F /IM \"%1\" >nul 2>&1 && "
        "ping -n 4 127.0.0.1 >nul && "
        "copy /Y \"%2\" \"%3\" >nul && "
        "del /f /q \"%4\" && "
        "start \"\" \"%3\"");
    // #368 需求①：用静默 detached 拉起，杜绝回滚脚本弹出 cmd 黑窗
    stelarithStartHiddenDetached(QStringLiteral("cmd.exe"),
                                 {QStringLiteral("/c"), cmd.arg(exeName, prev, exe, otaPendingPath())});
    otaNoteFailure(QStringLiteral("新版本未能在 %d 秒内连上云端，已回滚").arg(kOtaRollbackMs / 1000));
    QTimer::singleShot(1500, [] { QCoreApplication::quit(); });   // 让位给刚拉起来的旧版
}

/* 桌面空闲时长（毫秒）—— 判"安全窗口"用（2026-10-17 · 修 OTA 不生效第三层）。
 * 用 GetLastInputInfo 而不是自己挂钩子：它是系统级"最后一次键鼠输入"，
 * 不依赖本进程有没有焦点，也不受被控端藏在托盘影响。
 * ⚠️ dwTime 是 32 位 GetTickCount 值 ⇒ 用 32 位差值（无符号自然回绕正确），
 *    不要拿 GetTickCount64 去减它（跨 49 天会算出天文数字）。
 * 取不到（远程/无输入会话）时返回 0 = 当作"正在使用"，宁可不升。 */
static qint64 desktopIdleMs()
{
    LASTINPUTINFO li{};
    li.cbSize = sizeof(LASTINPUTINFO);
    if (!GetLastInputInfo(&li)) return 0;
    const DWORD now = GetTickCount();
    return qint64(DWORD(now - li.dwTime));
}

static void maybeAutoUpdate()
{
    // ① 强制更新照旧"到点就装"；**非强制改判安全窗口**（2026-10-17 修）。
    //    病灶：mandatory=false 只弹提示 ⇒ 教室机无人值守、没人点「立即更新」
    //    ⇒ 版本永远卡在旧版。实测取证：stable 清单停在 0.6.37，而在线设备的日志
    //    一直只有"发现新版本…右键托盘可升级"，没有一个人会去右键。
    //    补法：非强制在 **非考试 + 桌面空闲 ≥ N 分钟** 时自动装 —— 老师在用电脑时
    //    绝不重启它；空闲 = 没人在看课件，被控端重启一两分钟不打断课堂。
    //    参数 STE_QT_OTA_AUTO_IDLE_MIN：默认 10（分钟），填 0 = 关掉非强制自动装。
    if (!g_updateMandatory) {
        const QString envIdle = qEnvironmentVariable("STE_QT_OTA_AUTO_IDLE_MIN").trimmed();
        const int idleMin = envIdle.isEmpty() ? 10 : envIdle.toInt();
        if (idleMin <= 0) return;                          // 管理员显式关掉了非强制自动装
        if (ExamMode::inst().active()) {
            // 考试期间绝不重启被控端（会打断考试计时/拦截逻辑），等考完再说。
            // ⚠️ 这条**不受** STE_QT_OTA_AUTO_FORCE 影响：考试优先级高于维护便利。
            qInfo("[agent-qt] ⏸ 非强制升级 v%s 暂缓：考试模式进行中",
                  qPrintable(g_latestVer));
            return;
        }
        // 跳过空闲判据：只给「管理员维护窗口」与「探针取证」用。
        // 常态化设它 = 正在用电脑时也会被重启，所以每次跳过都打 warning 留痕。
        if (qEnvironmentVariable("STE_QT_OTA_AUTO_FORCE") == QLatin1String("1")) {
            qWarning("[agent-qt] ⚠ STE_QT_OTA_AUTO_FORCE=1 ⇒ 跳过空闲判据（维护窗口/探针专用）");
        } else {
            const qint64 idle = desktopIdleMs();
            if (idle < qint64(idleMin) * 60000LL) {
                qInfo("[agent-qt] ⏸ 非强制升级 v%s 等到空闲：桌面只空闲 %lld 秒（要求 ≥ %d 分钟）",
                      qPrintable(g_latestVer), (long long)(idle / 1000), idleMin);
                return;
            }
            qInfo("[agent-qt] ⬆ 非强制升级 v%s → v%s：桌面已空闲 %lld 秒、非考试 ⇒ 自动装"
                  "（STE_QT_OTA_AUTO_IDLE_MIN 可调，0=关）",
                  kAppVersion, qPrintable(g_latestVer), (long long)(idle / 1000));
        }
    }
    if (g_unconfigured) {                                 // ② 没配好就别自作主张（升完照样连不上）
        qInfo("[agent-qt] 升级跳过：本机还没配好云端（未配置）");
        return;
    }
    if (g_otaDeps->otaIsBusy()) {                                    // ③ 上一次下载还没完
        qInfo("[agent-qt] 升级跳过：上一个更新任务还在跑");
        return;
    }
    if (g_updateUrl.isEmpty() || g_updateSha.length() != 64) {   // ④ 清单不完整 = 不许升
        qWarning("[agent-qt] 升级跳过：清单缺 url 或 sha256（不让管理员补 ota.json 就别硬升）");
        return;
    }
    if (compareVersion(g_latestVer, QString::fromLatin1(kAppVersion)) <= 0) return;   // ⑤ 已经是最新

    if (otaPaused()) {                                    // ⑥ 人工紧急刹车
        qWarning("[agent-qt] ⛔ 升级已在本机暂停（存在 %s）—— 管理员排查用，删掉这个文件即恢复",
                 qPrintable(otaPausePath()));
        // M3（2026-10-20）：暂停/恢复是管理员**从外部**拨的开关（往数据目录扔/删一个文件），
        // 端侧无从"感知事件"，只能靠快照比对：paused 翻转即上报一次（变化才发）。
        otaReportHealth(QStringLiteral("paused"));
        return;
    }

    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 nextAt = otaLoadState().value(QStringLiteral("nextAt")).toVariant().toLongLong();
        if (nextAt > now) {
            qInfo("[agent-qt] ⏳ 升级还没到点：%lld 秒后再试（上次结果 %s）",
                  (nextAt - now) / 1000, qPrintable(otaLoadState().value(QStringLiteral("lastError")).toString()));
            return;
        }
    }
    {
        QStorageInfo si(qEnvironmentVariable("LOCALAPPDATA"));
        si.refresh();
        if (si.bytesAvailable() < kOtaMinFreeBytes) {      // ⑦ 盘快满了：宁可不升，也别装到一半没地方
            qWarning("[agent-qt] ⚠ 升级跳过：数据盘只剩 %lld 字节（要求 ≥ %lld）",
                     (long long)si.bytesAvailable(), (long long)kOtaMinFreeBytes);
            return;
        }
    }

    qInfo("[agent-qt] 🔒 %s：v%s → v%s，开始自动下载安装（无需人工操作）",
          g_updateMandatory ? "云端要求强制升级" : "空闲窗口自动升级",
          kAppVersion, qPrintable(g_latestVer));
    QJsonObject p;
    p.insert(QStringLiteral("url"), g_updateUrl);
    p.insert(QStringLiteral("sha256"), g_updateSha);
    p.insert(QStringLiteral("version"), g_latestVer);
    g_otaDeps->startSelfUpdate(QStringLiteral("auto"), p);
}

// ── 装配（main() 调用一次）──
void otaUpdateInit(const OtaDeps &deps)
{
    static OtaDeps s_deps = deps;   // 拷贝一次，之后经 g_otaDeps 只读
    g_otaDeps = &s_deps;
}
