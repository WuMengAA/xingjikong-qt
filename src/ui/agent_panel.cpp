// QML 控制面板（U5-A3）数据接线 —— main 拆分 #9（2026-10-30）
// 从 main.cpp（原 6388-6955：四页数据 builder + agentUiFill + agentUiWire）迁出，
// 函数/注释逐字保留，仅把 7 处匿名域依赖函数改为经 g_panelDeps 访问（方案 B）：
//   compareVersion / otaSelfCheck / saveAgentEnv / openConfigDialog /
//   updateTray(+TrayState) / connectNow / startSelfUpdate。
// 函数名/参数不变、语义等价；main() 负责填充 g_panelDeps（同一 TU 才能取匿名域函数地址）。
//
// 为什么用依赖注入而不是把这些函数提升文件作用域：它们互相连锁（配置向导→绑定→
// OTA→连接），整簇外迁侵入大；方案 B 只改本文件 7 个调用点，改动集中、可回退。
//
// 状态全局（g_configured/g_registered/g_ws/… 与 kAppVersion）仍驻 main（文件作用域
// 外部链接），本模块按既有 extern 模式复用（同 g_uid/agentEnvPath 模式，见 remote_control）。

#include "agent_panel.h"

#include "../agent_ui.h"          // AgentUi（friend 声明在 agent_ui.h）+ AgentShell
#include "../core/AgentSettings.h" // AgentSettings（本地功能开关）
#include "../modules/modulehost.h" // ModuleHost（内置模块层）
#include "../island.h"            // IslandOverlay（岛联动）
#include "../media/MediaSession.h" // MediaSession（播放时段判定）
#include "../settings_domain.h"   // SettingsDomain（存储/备份/重置）
#include "../timesync_domain.h"   // TimesyncDomain（时间配置）
#include "../ota/ota_state.h"     // otaPaused/otaLoadState（升级状态快照）
#include "../ota/channel_persistence.h" // channelFilePath/persistChannel（更新渠道）
#include "../p3log/p3_reader.h"   // buildNoiseLogText/buildStudyLogText/openFolder
#include "../p3log/p3_record_view.h" // buildNoiseRecordView/buildStudyRecordView（记录页行）
#include "../winlauncher.h"       // stelarithHideChildConsole（插件解压静默）
#include "../plugin/pluginhost.h" // PluginHost（g_pluginHost 类型）

#include <QWebSocket>
#include <QDesktopServices>
#include <QFileInfo>
#include <QDateTime>
#include <QTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QAbstractSocket>

// ── 复用 main 的状态全局（提升文件作用域外部链接，同 g_uid/agentEnvPath 模式）──
extern const char *kAppVersion;
extern QString g_uid;
extern QWebSocket *g_ws;
extern bool g_registered;
extern QString g_logPath;
extern bool g_configured;
extern bool g_reconnectNow;
extern bool g_updateChecked;
extern QString g_latestVer, g_updateUrl, g_updateSha, g_updateNotes;
extern bool g_updateMandatory;
extern QString g_channel;
extern int g_backoffMs;
extern QString g_classCode;
extern QString g_deviceRole;
extern PluginHost *g_pluginHost;

// ── 复用 main 已提升文件作用域的开机自启/桌面快捷方式函数（main 拆分 #9）──
extern bool agentAutostartEnabled();
extern bool agentSetAutostart(bool on);
extern bool agentDesktopShortcutExists();
extern bool agentSetDesktopShortcut(bool on);

// agentEnvPath：main.cpp 前向声明（302，外部链接），定义见其文件末尾 namespace 之后。
extern QString agentEnvPath();

/* ── A-3 第二批：四页的数据 builder（static，只产出数据不碰 AgentUi 成员）──── */

// 自检四项（判据照 runSelfCheck 一字不差）
static QVariantList buildSelfCheckItems()
{
    QVariantList items;
    auto add = [&items](const QString &name, bool ok, const QString &fix) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("ok"), ok);
        m.insert(QStringLiteral("fix"), ok ? QString() : fix);
        items.append(m);
    };
    add(QStringLiteral("配置完整（云端地址 + 设备令牌）"), g_configured, QStringLiteral("去配置 / 绑定"));
    const bool connected = g_ws && g_ws->state() == QAbstractSocket::ConnectedState;
    add(QStringLiteral("已连接云端"), connected, QStringLiteral("重试连接"));
    add(QStringLiteral("已注册（云端已确认）"), g_registered, QStringLiteral("重试连接"));
    add(QStringLiteral("班级已绑定（管理端按班可见）"), !g_classCode.isEmpty(), QStringLiteral("绑定班级"));
    return items;
}

// 插件列表（本地快照 + 云端条目缓存；口径照 openPluginStore::render）
static QHash<QString, QJsonObject> s_storeEntries;   // id -> 云端条目（pluginsRefresh 刷新）
static QString pluginRegistryUrl()
{
    QString u = qEnvironmentVariable("STE_PLUGIN_REGISTRY");
    return u.isEmpty() ? QStringLiteral("https://control.245959623.xyz/api/plugins") : u;
}
static QVariantList buildPluginList()
{
    QVariantList out;
    QHash<QString, QJsonObject> localById;
    if (g_pluginHost) {
        const QJsonArray local = g_pluginHost->snapshot();
        for (const QJsonValue &v : local) {
            const QJsonObject o = v.toObject();
            localById.insert(o.value(QStringLiteral("id")).toString(), o);
        }
    }
    QStringList ids;
    for (auto it = s_storeEntries.begin(); it != s_storeEntries.end(); ++it) ids << it.key();
    for (const QString &id : localById.keys()) if (!ids.contains(id)) ids << id;
    for (const QString &id : ids) {
        const QJsonObject cloud = s_storeEntries.value(id);
        const QJsonObject loc = localById.value(id);
        const bool inst = localById.contains(id);
        const bool en = loc.value(QStringLiteral("enabled")).toBool();
        const bool run = loc.value(QStringLiteral("running")).toBool();
        const QString status = inst ? (en ? (run ? QStringLiteral("● 运行中")
                                                 : QStringLiteral("○ 已装未运行"))
                                         : QStringLiteral("× 已禁用"))
                                    : QStringLiteral("— 未安装");
        QVariantMap m;
        m.insert(QStringLiteral("id"), id);
        m.insert(QStringLiteral("name"), cloud.value(QStringLiteral("name")).toString(loc.value(QStringLiteral("name")).toString(id)));
        m.insert(QStringLiteral("version"), cloud.value(QStringLiteral("version")).toString(loc.value(QStringLiteral("version")).toString()));
        m.insert(QStringLiteral("author"), cloud.value(QStringLiteral("author")).toString());
        m.insert(QStringLiteral("description"), cloud.value(QStringLiteral("description")).toString());
        m.insert(QStringLiteral("installed"), inst);
        m.insert(QStringLiteral("enabled"), en);
        m.insert(QStringLiteral("status"), status);
        out.append(m);
    }
    return out;
}

// 记录文本与记录行（照 openNoiseLogDialog 口径）：文本在 p3_reader（#6b），按天/按次的行在 p3_record_view（10-31）。

void agentUiFill(AgentUi *ui)
{
    ui->m_appVersion = QString::fromLatin1(kAppVersion);
    ui->m_uid = g_uid;
    ui->m_cloudUrl = qEnvironmentVariable("STE_QT_WS_URL");
    ui->m_deviceRole = g_deviceRole;
    ui->m_classCode = g_classCode;
    ui->m_connState = g_registered ? QStringLiteral("已连接（云端已确认）")
                                   : QStringLiteral("未连接 / 正在重连");
    ui->m_dataDir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong");
    ui->m_logPath = g_logPath;

    // 升级状态四态 + canUpgrade（照 openInfoDialog 的口径，一字不差）
    const QString cur = QString::fromLatin1(kAppVersion);
    ui->m_hasNewer = g_updateChecked && !g_latestVer.isEmpty() && g_panelDeps->compareVersion(g_latestVer, cur) > 0;
    ui->m_canUpgrade = ui->m_hasNewer && !g_updateUrl.isEmpty() && g_updateSha.length() == 64;
    if (!g_updateChecked)            ui->m_latestText = QStringLiteral("还没查到（等云端回执）");
    else if (g_latestVer.isEmpty())  ui->m_latestText = QStringLiteral("云端还没发布过版本");
    else if (ui->m_hasNewer)         ui->m_latestText = QStringLiteral("有新版本 v%1 可升级").arg(g_latestVer);
    else                             ui->m_latestText = QStringLiteral("已是最新（云端 v%1）").arg(g_latestVer);
    ui->m_updateNotes = g_updateNotes;

    ui->m_otaModeText = g_updateMandatory
        ? QStringLiteral("强制：到点自动装（装完重启一次）")
        : QStringLiteral("可选：只有点「立即升级」才装");
    ui->m_otaPaused = otaPaused();
    ui->m_otaAutoText = ui->m_otaPaused
        ? QStringLiteral("已暂停（点「恢复自动升级」解除）")
        : QStringLiteral("到点自动装");
    {
        const QJsonObject st = otaLoadState();
        const QString lastRes = st.value(QStringLiteral("lastResult")).toString();
        if (lastRes.isEmpty())
            ui->m_otaLastText = QStringLiteral("（这台设备还没自动升级过）");
        else if (lastRes == QLatin1String("ok"))
            ui->m_otaLastText = QStringLiteral("成功（已升到 v%1）")
                                    .arg(st.value(QStringLiteral("lastVer")).toString());
        else
            ui->m_otaLastText = QStringLiteral("失败 ×%1：%2")
                .arg(st.value(QStringLiteral("failCount")).toInt(0))
                .arg(st.value(QStringLiteral("lastError")).toString());
        const qint64 nextAt = st.value(QStringLiteral("nextAt")).toVariant().toLongLong();
        ui->m_otaNextText = nextAt > QDateTime::currentMSecsSinceEpoch()
            ? QDateTime::fromMSecsSinceEpoch(nextAt).toString(QStringLiteral("MM-dd HH:mm"))
            : QStringLiteral("立刻（有网就试）");
    }

    // ── 2026-10-28 B4：更新渠道（临时测试开关）──
    // 快照取**当前生效**的 g_channel（不是环境变量那份）：切过之后界面要显示新值。
    ui->m_updateChannel = g_channel;
    ui->m_channelOptions = AgentUi::channelOptionList(g_channel);   // 选中态样式也在这里定
    ui->m_channelNoteText = QStringLiteral("生效渠道写在 %1；切换后立刻按新渠道重查一次更新，"
                                           "重启/升级后仍沿用。此按钮为临时手段，"
                                           "服务端按机型灰度上线后退役。")
                                .arg(channelFilePath());

    ui->m_notifyEnabled   = AgentSettings::notifyEnabled();    ui->m_voiceEnabled    = AgentSettings::voiceEnabled();
    ui->m_noiseEnabled    = AgentSettings::noiseEnabled();
    ui->m_islandIdle      = AgentSettings::islandIdle();
    ui->m_lightMode       = AgentSettings::lightMode();
    ui->m_pluginIslandPush = AgentSettings::pluginIslandPush();   // P0-0：第三方推岛开关
    ui->m_autostart       = agentAutostartEnabled();
    ui->m_desktopShortcut = agentDesktopShortcutExists();

    // ── A-3 第二批：自检 / 插件 / 记录 / 播放时段 ──
    ui->applySelfCheckItems(buildSelfCheckItems());
    ui->m_plugins        = buildPluginList();
    ui->m_modules        = ModuleHost::instance()->snapshot().toVariantList();
    // 2026-10-31：记录页从「一段等宽文本」改为「汇总 + 按天/按次的行」；汇总与行都在
    // C++ 算好（QML 零统计）。拆解下沉到 p3_record_view（本文件 645 行/红线 600，不许加行）。
    applyNoiseRecordView(ui);
    applyStudyRecordView(ui);
    ui->m_playScheduleEnabled = AgentSettings::playScheduleEnabled();
    ui->m_playWindowsText     = AgentSettings::playWindows().join(QStringLiteral("\n"));
    {
        const QString cur = (IslandOverlay::instance() && IslandOverlay::instance()->mediaOn())
            ? QStringLiteral("正在播放：%1").arg(IslandOverlay::instance()->mediaTitle())
            : QStringLiteral("当前无媒体在播");
        ui->m_playStateText = QStringLiteral("当前状态：%1 · 时段内允许播放：%2")
                                  .arg(cur).arg(MediaSession::isPlayAllowedNow() ? QStringLiteral("是") : QStringLiteral("否"));
    }

    // ── 2026-10-28 模块三 · 通用分类（按蓝图原文六项：开机自启/url/安全模式/时间/存储/高级）──
    // 前两项（开机自启、桌面快捷方式）在文件上半部已有，这里补后四项的展示值。
    ui->m_serverUrl    = qEnvironmentVariable("STE_QT_WS_URL").trimmed();
    ui->m_safeModeAuto = AgentSettings::safeModeAuto();
    ui->m_safeModeNow  = SettingsDomain::safeModeActive();
    ui->m_lastExitText = SettingsDomain::lastExitText();
    ui->m_timeAutoSync = AgentSettings::timeAutoSync();
    {
        // 时间：判定时间取 TimesyncDomain::now()，**不是** QDateTime::currentDateTime()
        // —— 后者没算人工偏移，页面上就会显示"改了偏移但时间没动"。
        const TimesyncDomain::Status st = TimesyncDomain::status();
        ui->m_timeNowText = TimesyncDomain::now().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (st.ntpOk)
            ui->m_timeStateText = QStringLiteral("已与 NTP 对齐");
        else if (!st.error.isEmpty())
            ui->m_timeStateText = QStringLiteral("对时失败：") + st.error;
        else
            ui->m_timeStateText = QStringLiteral("还没对上（等首轮同步，或手动点「立即对时」）");

        if (!st.server.isEmpty())
            ui->m_timeServerText = st.server;
        else if (!st.lastAttempt.isEmpty())
            ui->m_timeServerText = st.lastAttempt + QStringLiteral("（未成功）");
        else
            ui->m_timeServerText = QStringLiteral("（还没尝试过）");

        // 偏差：deltaMs = 真时 − 系统钟；正 = 系统钟慢
        const qint64 absMs = qAbs(st.deltaMs);
        if (!st.ntpOk)
            ui->m_timeSkewText = QStringLiteral("未知（未成功对时）");
        else if (absMs < 500)
            ui->m_timeSkewText = QStringLiteral("系统钟与本机时钟一致");
        else
            ui->m_timeSkewText = QStringLiteral("系统钟比标准时间%1 %2 秒")
                                     .arg(st.deltaMs > 0 ? QStringLiteral("慢") : QStringLiteral("快"))
                                     .arg(QString::number(double(absMs) / 1000.0, 'f', 1));

        ui->m_timeOffsetText = QStringLiteral("%1%2 分钟")
                                   .arg(st.offsetMin >= 0 ? QStringLiteral("+") : QString())
                                   .arg(st.offsetMin);
        ui->m_timeLastSyncText = st.lastSyncMs > 0
            ? QDateTime::fromMSecsSinceEpoch(st.lastSyncMs).toString(QStringLiteral("MM-dd HH:mm:ss"))
            : QStringLiteral("从未成功同步");
    }
}

void agentUiWire(AgentUi *ui)
{
    agentUiFill(ui);

    // 打开面板 / QML 主动刷新：重读快照（连接/升级状态是"点开那一刻"的）。
    QObject::connect(ui, &AgentUi::refreshRequested, ui, [ui] {
        agentUiFill(ui);
        ui->publish();
    });

    // ── 2026-10-28 B4：更新渠道切换（临时测试开关）──────────────────────────
    // 用户点单：「被控端设置界面添加临时的测试更新渠道切换按钮」，要求切换逻辑在 C++。
    // 生效面两条，都必须走：
    //   ① g_channel —— register 上报给云端（云端据此决定推哪个清单）与 otaSelfCheck 的查询参数；
    //   ② channel.txt —— 落盘，重启/OTA 升级后仍沿用该渠道（否则升级一次就悄悄掉回 stable）。
    // 白名单三个渠道名：与云端 manifestFileFor 的三份清单一一对应；不认识的值**不落盘**
    // （脏写 channel.txt 会让设备永久查到一个不存在的清单 ⇒ 永远"无更新"，且现场看不出来）。
    QObject::connect(ui, &AgentUi::channelChangedRequested, ui, [ui](const QString &ch) {
        const QString v = ch.trimmed();
        const bool known = (v == QLatin1String("stable") || v == QLatin1String("rc")
                            || v == QLatin1String("gray"));
        if (!known) {
            qWarning("[agent-shell] 未知更新渠道「%s」——拒绝落盘（界面回弹当前值）",
                     qPrintable(ch));
            agentUiFill(ui);
            ui->publish();
            return;
        }
        if (v == g_channel) {           // 幂等：同值不重查、不重写（省一次网络）
            agentUiFill(ui);
            ui->publish();
            return;
        }
        g_channel = v;
        persistChannel(v);
        // 上一渠道的查询结果必须作废：不然界面还写着"已是最新（云端 v0.6.50）"，
        // 而那个版本号其实是另一个渠道的 —— 比不显示更容易误导。
        g_updateChecked = false;
        g_latestVer.clear();
        g_updateUrl.clear();
        g_updateSha.clear();
        g_updateNotes.clear();
        qInfo("[agent-qt] 更新渠道已切换为 %s（channel.txt 已落盘），立即重查一次", qPrintable(v));
        g_panelDeps->otaSelfCheck();    // 立刻按新渠道查（不用等 30min 周期）
        agentUiFill(ui);
        ui->publish();
    });

    // 开关：真实写（QSettings / 岛联动 / 提权），写完**重读快照再 publish**
    // —— 提权被拒时界面自动回弹，不会"勾上了、其实没改"。
    QObject::connect(ui, &AgentUi::switchChanged, ui,
                     [ui](const QString &key, bool v) {
        if (key == QLatin1String("notifyEnabled"))       AgentSettings::setNotifyEnabled(v);
        else if (key == QLatin1String("voiceEnabled"))   AgentSettings::setVoiceEnabled(v);
        else if (key == QLatin1String("noiseEnabled"))   AgentSettings::setNoiseEnabled(v);
        else if (key == QLatin1String("islandIdle")) {
            AgentSettings::setIslandIdle(v);
            if (IslandOverlay::instance()) IslandOverlay::instance()->setIdleEnabled(v);
        } else if (key == QLatin1String("lightMode")) {
            AgentSettings::setLightMode(v);
            if (IslandOverlay::instance()) IslandOverlay::instance()->setLightMode(v);
        } else if (key == QLatin1String("pluginIslandPush")) {
            // P0-0：只落 QSettings —— pluginhost 每次推岛都现问 AgentSettings（没有缓存），
            // 所以下一次插件推岛即生效，不需要在这里通知宿主。
            AgentSettings::setPluginIslandPush(v);
            qInfo("[agent-shell] 第三方推岛开关 → %s", v ? "开" : "关");
        } else if (key == QLatin1String("autostart")) {
            if (!agentSetAutostart(v))
                qWarning("[agent-shell] 开机自启没改成（多半 UAC 被拒）—— 界面将回弹真实状态");
        } else if (key == QLatin1String("desktopShortcut")) {
            if (!agentSetDesktopShortcut(v))
                qWarning("[agent-shell] 桌面快捷方式没改成（多半 UAC 被拒）—— 界面将回弹真实状态");
        } else if (key == QLatin1String("playScheduleEnabled")) {
            AgentSettings::setPlayScheduleEnabled(v);
        } else if (key == QLatin1String("safeModeAuto")) {
            // 2026-10-28 模块三：安全模式开关只落盘；本次是否进安全模式在启动时已定，
            // 改开关影响**下一次**启动（在面板里就地切"本次"没有意义，且容易让人以为
            // 插件当场回来了）。文案由 safeModeStateText 讲清楚。
            AgentSettings::setSafeModeAuto(v);
            qInfo("[agent-shell] 安全模式（异常退出后自动进）→ %s", v ? "开" : "关");
        } else if (key == QLatin1String("timeAutoSync")) {
            // 自动校时：关掉只停周期同步，人工偏移保留（偏移是老师自己调的，不该被开关清掉）。
            AgentSettings::setTimeAutoSync(v);
            if (v) TimesyncDomain::start();
            qInfo("[agent-shell] 自动校时 → %s", v ? "开" : "关（只保留人工偏移）");
        }
        agentUiFill(ui);
        ui->publish();
    });

    QObject::connect(ui, &AgentUi::openLogDirRequested, ui, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(g_logPath).absolutePath()));
    });
    QObject::connect(ui, &AgentUi::openDataDirRequested, ui, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong")));
    });
    // 2026-10-28 模块三：托盘右键的「打开配置目录」移植进设置页（通用 › 高级）。
    // 落点与托盘菜单**同一处**（agent.env 所在目录），不另造一条路径。
    QObject::connect(ui, &AgentUi::openConfigDirRequested, ui, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(agentEnvPath()).absolutePath()));
    });

    // ══ 2026-10-28 模块三 · 通用分类（严格按蓝图原文六项）════════════════════
    // ① url 配置：写 agent.env + 覆盖进程环境（saveAgentEnv 内部 qputenv）+
    //    断线回调里立刻重连（g_reconnectNow，不等指数退避）。
    //    校验只认 ws:// 与 wss://：写成 http:// 会让 QUdpSocket/QWebSocket 一起静默失败，
    //    现场表现为"一直在重连"却不报错 —— 比直接拒绝更糟。
    QObject::connect(ui, &AgentUi::serverUrlApplyRequested, ui, [ui](const QString &url) {
        const QString v = url.trimmed();
        if (!v.startsWith(QLatin1String("ws://")) && !v.startsWith(QLatin1String("wss://"))) {
            ui->setActionResultText(QStringLiteral("地址必须以 ws:// 或 wss:// 开头（当前未保存）"));
            ui->publish();
            return;
        }
        // token/uid 保持不动：改地址通常只是换域名/端口，不该顺手把绑定冲掉。
        const bool ok = g_panelDeps->saveAgentEnv(v,
                                     qEnvironmentVariable("STE_QT_WS_TOKEN"),
                                     qEnvironmentVariable("STE_QT_UID"),
                                     QString());
        ui->setActionResultText(ok
            ? QStringLiteral("已保存并立即重连：%1").arg(v)
            : QStringLiteral("保存失败（写不进 agent.env，见日志）"));
        if (ok) {
            g_reconnectNow = true;
            if (g_ws) g_ws->close();   // 断线回调里看到 g_reconnectNow 就立刻重连
        }
        agentUiFill(ui);
        ui->publish();
    });
    // ② 时间配置：立即校时 / 人工偏移步进 / 归零（全部走 TimesyncDomain 真实现）
    QObject::connect(ui, &AgentUi::timeSyncRequested, ui, [ui] {
        const bool started = TimesyncDomain::syncNow();
        ui->setActionResultText(started
            ? QStringLiteral("正在与 NTP 对时…（结果几秒后回来）")
            : QStringLiteral("上一轮对时还没结束，稍后再试"));
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::timeOffsetAdjustRequested, ui, [ui](int deltaMinutes) {
        const int next = TimesyncDomain::offsetMinutes() + deltaMinutes;
        if (!TimesyncDomain::setOffsetMinutes(next)) {
            ui->setActionResultText(QStringLiteral("人工偏移超出范围（-720 ~ +720 分钟）"));
        } else {
            ui->setActionResultText(QStringLiteral("人工偏移已设为 %1%2 分钟")
                                         .arg(next >= 0 ? QStringLiteral("+") : QString())
                                         .arg(next));
        }
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::timeOffsetResetRequested, ui, [ui] {
        TimesyncDomain::setOffsetMinutes(0);
        ui->setActionResultText(QStringLiteral("人工偏移已归零（只用 NTP 校时结果）"));
        agentUiFill(ui);
        ui->publish();
    });
    // ③ 存储与备份
    QObject::connect(ui, &AgentUi::storageRefreshRequested, ui, [ui] {
        const QString dataDir = qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong");
        const QString cfgDir  = QFileInfo(agentEnvPath()).absolutePath();
        const QString logDir  = QFileInfo(g_logPath).absolutePath();
        const qint64 cfg  = SettingsDomain::dirSizeBytes(cfgDir);
        const qint64 data = SettingsDomain::dirSizeBytes(dataDir);
        const qint64 logs = SettingsDomain::dirSizeBytes(logDir);
        const QString cfgTxt  = SettingsDomain::humanSize(cfg) + QStringLiteral("  ·  ") + cfgDir;
        const QString dataTxt = SettingsDomain::humanSize(data) + QStringLiteral("  ·  ") + dataDir;
        const QString logTxt  = SettingsDomain::humanSize(logs) + QStringLiteral("  ·  ") + logDir;
        // 总计按"去重"算：数据目录通常已包含日志目录，重复加会把数报大（现场会质疑）。
        qint64 total = cfg + data;
        if (!logDir.startsWith(dataDir, Qt::CaseInsensitive)) total += logs;
        ui->setStorageTexts(cfgTxt, dataTxt, logTxt, SettingsDomain::humanSize(total),
                            QStringLiteral("统计于 %1")
                                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
        emit ui->storageChanged();
    });
    QObject::connect(ui, &AgentUi::backupRequested, ui, [ui] {
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
                                .filePath(QStringLiteral("星集控备份-%1").arg(stamp));
        QString err;
        const int n = SettingsDomain::backupConfig(
            qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong"), dir, &err);
        if (n > 0) {
            ui->setActionResultText(QStringLiteral("已备份 %1 个文件 → %2").arg(n).arg(dir));
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        } else {
            ui->setActionResultText(err.isEmpty()
                ? QStringLiteral("没有可备份的配置文件")
                : QStringLiteral("备份失败：%1").arg(err));
        }
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::clearLogsRequested, ui, [ui] {
        const qint64 freed = SettingsDomain::clearOldLogs(
            qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong"));
        ui->setActionResultText(QStringLiteral("已清理旧日志，释放 %1")
                                     .arg(SettingsDomain::humanSize(freed)));
        agentUiFill(ui);
        ui->publish();
    });
    // ④ 高级选项：重置设置（QML 侧已二次确认；这里只执行 + 说明下一步）
    QObject::connect(ui, &AgentUi::resetAllRequested, ui, [ui] {
        const int n = SettingsDomain::resetSettings(
            qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/xingjikong"));
        ui->setActionResultText(QStringLiteral("已重置 %1 个配置文件。请退出被控端并重新打开 —— "
                                                "设备身份（用途/归属码）已保留，不用重新绑定。").arg(n));
        agentUiFill(ui);
        ui->publish();
    });
    // 「立即升级」：走的就是云端下发 self_update 用的同一条链路（含四条护栏）。
    QObject::connect(ui, &AgentUi::upgradeNowRequested, ui, [ui] {
        if (!ui->canUpgrade()) return;
        QJsonObject p;
        p.insert(QStringLiteral("url"),     g_updateUrl);
        p.insert(QStringLiteral("sha256"),  g_updateSha);
        p.insert(QStringLiteral("version"), g_latestVer);
        qInfo("[agent-qt] 用户在 QML 控制面板点了「立即升级」→ v%s", qPrintable(g_latestVer));
        g_panelDeps->startSelfUpdate(QStringLiteral("manual"), p);
        agentUiFill(ui);
        ui->publish();
    });
    // 紧急刹车：往数据目录扔一个空文件就停手（删掉即恢复）。
    QObject::connect(ui, &AgentUi::toggleOtaPauseRequested, ui, [ui] {
        if (otaPaused()) {
            QFile::remove(otaPausePath());
            qInfo("[agent-qt] 操作员恢复了本机自动升级");
        } else {
            QDir().mkpath(otaBaseDir());
            QFile f(otaPausePath());
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QStringLiteral("paused by operator %1\n")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8());
                f.flush();
            }
            qWarning("[agent-qt] ⛔ 操作员暂停了本机自动升级（%s）", qPrintable(otaPausePath()));
        }
        agentUiFill(ui);
        ui->publish();
    });

    // ── A-3 第二批：自检 / 插件市场 / 记录 / 播放时段 ──────────────────
    QObject::connect(ui, &AgentUi::selfCheckRequested, ui, [ui] {
        agentUiFill(ui);
        ui->publish();
    });
    // 一键修复（判据与 doFix 一字不差）：配置缺/班级缺 → 配置向导；否则立即重连。
    QObject::connect(ui, &AgentUi::selfCheckFixRequested, ui, [ui] {
        if (!g_configured || g_classCode.isEmpty()) {
            g_panelDeps->openConfigDialog();
        } else {
            g_backoffMs = 1000;
            if (g_ws && g_ws->state() != QAbstractSocket::UnconnectedState) {
                g_reconnectNow = true; g_ws->close();
            } else {
                g_panelDeps->updateTray(TrayState::Disconnected, QStringLiteral("正在重连…"));
                g_panelDeps->connectNow();
            }
        }
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::pluginsRefreshRequested, ui, [ui] {
        QNetworkAccessManager *nam = new QNetworkAccessManager(ui);
        QNetworkRequest req((QUrl(pluginRegistryUrl())));
        QNetworkReply *rep = nam->get(req);
        QObject::connect(rep, &QNetworkReply::finished, ui, [ui, rep, nam] {
            const QByteArray data = rep->readAll();
            rep->deleteLater(); nam->deleteLater();
            if (rep->error() == QNetworkReply::NoError) {
                const QJsonArray arr = QJsonDocument::fromJson(data).array();
                for (const QJsonValue &v : arr)
                    s_storeEntries.insert(v.toObject().value(QStringLiteral("id")).toString(), v.toObject());
            }   // 离线也能看本地已装（照 openPluginStore 口径）
            agentUiFill(ui);
            ui->publish();
        });
        agentUiFill(ui);   // 先给本地快照
        ui->publish();
    });
    // ── 内置模块层：刷新 / 启停（不可卸载——它编译在 agent 里）──
    QObject::connect(ui, &AgentUi::modulesRefreshRequested, ui, [ui] {
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::moduleEnableRequested, ui, [ui](const QString &id, bool enable) {
        ModuleHost::instance()->setEnabled(id, enable);
        agentUiFill(ui);
        ui->publish();
    });
    // 安装：已装 ⇒ 启用；未装 ⇒ 下载 zip → 解压 → installAndLaunch（照 openPluginStore）。
    QObject::connect(ui, &AgentUi::pluginInstallRequested, ui, [ui](const QString &id) {
        if (!g_pluginHost) return;
        bool installed = false;
        for (const QJsonValue &v : g_pluginHost->snapshot())
            if (v.toObject().value(QStringLiteral("id")).toString() == id) { installed = true; break; }
        if (installed) { g_pluginHost->enable(id, true); agentUiFill(ui); ui->publish(); return; }
        const QString url = s_storeEntries.value(id).value(QStringLiteral("downloadUrl")).toString();
        if (url.isEmpty()) {
            qWarning("[agent-shell] 插件 %s 暂无可下载包（需先在商店后台上传）", qPrintable(id));
            return;
        }
        QNetworkAccessManager *nam = new QNetworkAccessManager(ui);
        QNetworkReply *rep = nam->get(QNetworkRequest(QUrl(url)));
        QObject::connect(rep, &QNetworkReply::finished, ui, [ui, rep, nam, id] {
            const QByteArray data = rep->readAll();
            rep->deleteLater(); nam->deleteLater();
            if (rep->error() != QNetworkReply::NoError) {
                qWarning("[agent-shell] 插件 %s 下载失败", qPrintable(id));
                return;
            }
            const QString tmp = QDir::tempPath() + QStringLiteral("/stelarith-plugin-%1.zip").arg(id);
            { QFile f(tmp); if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); } }
            const QString dir = g_pluginHost->pluginDir() + QLatin1Char('/') + id;
            QDir().mkpath(dir);
            // #368 需求①：进程内 powershell 解压静默，挂 CREATE_NO_WINDOW
            {
                QProcess p;
                stelarithHideChildConsole(p);
                p.start(QStringLiteral("powershell"),
                        QStringList() << QStringLiteral("-Command")
                                      << QStringLiteral("Expand-Archive -Force '%1' '%2'").arg(tmp, dir));
                p.waitForFinished(-1);
            }
            g_pluginHost->installAndLaunch(id);
            qInfo("[agent-shell] 已安装并启动插件：%s", qPrintable(id));
            agentUiFill(ui);
            ui->publish();
        });
    });
    QObject::connect(ui, &AgentUi::pluginEnableRequested, ui, [ui](const QString &id, bool enable) {
        if (g_pluginHost) g_pluginHost->enable(id, enable);
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::pluginUninstallRequested, ui, [ui](const QString &id) {
        if (g_pluginHost) g_pluginHost->uninstall(id);
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::pluginSampleInstallRequested, ui, [ui] {
        if (!g_pluginHost) return;
        const QString src = QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/samples/echo");
        const QString dst = g_pluginHost->pluginDir() + QStringLiteral("/echo");
        if (!QFile::exists(src)) {
            qWarning("[agent-shell] 未找到内置示例（plugins/samples/echo）");
            return;
        }
        QDir().mkpath(dst);
        for (const QString &f : QStringList() << QStringLiteral("manifest.json") << QStringLiteral("echo.py"))
            QFile::copy(src + QLatin1Char('/') + f, dst + QLatin1Char('/') + f);
        g_pluginHost->installAndLaunch(QStringLiteral("echo"));
        qInfo("[agent-shell] 已安装示例插件 echo");
        agentUiFill(ui);
        ui->publish();
    });
    QObject::connect(ui, &AgentUi::noiseFolderRequested, ui, [] { openFolder(QStringLiteral("noise_log")); });
    QObject::connect(ui, &AgentUi::studyFolderRequested, ui, [] { openFolder(QStringLiteral("study_log")); });
    QObject::connect(ui, &AgentUi::playScheduleSaveRequested, ui, [ui] {
        AgentSettings::setPlayScheduleEnabled(ui->playScheduleEnabled());
        QStringList list;
        for (const QString &line : ui->playWindowsText().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString s = line.trimmed();
            if (!s.isEmpty()) list.append(s);
        }
        AgentSettings::setPlayWindows(list);
        qInfo("[play] 🎵 播放时段已保存（QML 面板）：enabled=%d, windows=%d",
              ui->playScheduleEnabled() ? 1 : 0, list.size());
        agentUiFill(ui);
        ui->publish();
    });
}
