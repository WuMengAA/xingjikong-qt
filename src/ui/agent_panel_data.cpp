// QML 控制面板数据层 —— agent_panel 拆分（2026-11-02 · 屎山整理 #3）
// 从 agent_panel.cpp 原样搬出：三个 builder（自检项 / 插件列表 / 插件商店 URL）+
// 插件商店条目缓存 + agentUiFill。函数体与注释**逐字未改**，只把共享的 include/extern
// 收进 agent_panel_internal.h（两边同一份清单，杜绝两处 extern 清单各写各的漂移）。
//
// 为什么按「产出数据 / 接信号」切：这条线是天然缝 —— 数据层只读状态全局、只写 AgentUi
// 快照成员，不接任何信号；接线层只 connect 与转发，不产出数据。切错成「按页切」
// 会让同一次 fill 被拆到两处，状态快照一半新一半旧，界面上就是「某几项不刷新」。

#include "agent_panel_internal.h"


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

// ── 跨 TU 出口（定义在此，声明在 agent_panel_internal.h）───────────────────
// 出访问器而不是 extern 变量：变量跨 TU 暴露后谁都能清空它，
// 而清空会让「已装插件」从界面上集体消失且没有任何报错。
QHash<QString, QJsonObject> &agentPanelStoreEntries()
{
    return s_storeEntries;
}

QString agentPanelPluginRegistryUrl()
{
    return pluginRegistryUrl();
}
