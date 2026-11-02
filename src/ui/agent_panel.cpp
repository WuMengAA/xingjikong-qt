// QML 控制面板接线层 —— agent_panel 拆分（2026-11-02 · 屎山整理 #3）
// 从 main.cpp（原 6388-6955）迁出，再于本次按「产出数据 / 接信号」二分：
// 数据 builder 与 agentUiFill 搬到 agent_panel_data.cpp；本文件只剩 agentUiWire。
// 函数体与注释**逐字未改**，仅把两处跨 TU 引用换名：
//   s_storeEntries        → agentPanelStoreEntries()      （出访问器，防跨 TU 清空）
//   pluginRegistryUrl()   → agentPanelPluginRegistryUrl()  （static 函数跨不了 TU）
//
// ⚠️ agent_ui.h 是 AUTOMOC 单独编译的 Q_OBJECT 头（不能引用 main.cpp 的任何符号）。
//    而这里用到的 main.cpp 匿名域函数（内部链接，跨不了 TU）由 main() 经
//    g_panelDeps 注入 —— 函数名/参数不变、语义等价（方案 B）。

#include "agent_panel_internal.h"


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
    // 白名单由 channelCanonical() 收口（2026-11-02 口径变更：stable/rc/gray → stable/beta/alpha）。
    // 它既认新名也认旧名（rc→beta、gray→alpha），所以没升级的设备上报旧值、
    // 已升级的设备上报新值，都落进同一套语义。未知值返回空串 ⇒ 不落盘、界面回弹
    // （脏写 channel.txt 会让设备永久查到一个不存在的清单 ⇒ 永远"无更新"，现场看不出来）。
    QObject::connect(ui, &AgentUi::channelChangedRequested, ui, [ui](const QString &ch) {
        const QString v = channelCanonical(ch);
        if (v.isEmpty()) {
            qWarning("[agent-shell] 未知更新渠道「%s」——拒绝落盘（界面回弹当前值）；"
                     "合法值：alpha（灰度）/ beta（测试）/ stable（正式）", qPrintable(ch));
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
        // ★ 三分类必须互斥：cfgDir 与 logDir 其实都在 dataDir 这棵树里，
        //   各自 dirSizeBytes 三次会得到同一个数（2026-11-02 线上截图：三栏都是
        //   532.5 MB、合计 1.56 GB 正好三倍）。分类与去重统一在域里做，
        //   顺带修掉 dataDir("/" 拼) 与 absolutePath("\") 分隔符不同导致 startsWith 恒 false。
        const SettingsDomain::StorageBreakdown bk =
            SettingsDomain::storageBreakdown(dataDir, logDir);
        const QString cfgTxt  = SettingsDomain::humanSize(bk.config) + QStringLiteral("  ·  ") + cfgDir;
        const QString dataTxt = SettingsDomain::humanSize(bk.data) + QStringLiteral("  ·  ") + dataDir;
        const QString logTxt  = SettingsDomain::humanSize(bk.logs) + QStringLiteral("  ·  ") + logDir;
        ui->setStorageTexts(cfgTxt, dataTxt, logTxt, SettingsDomain::humanSize(bk.total()),
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
        QNetworkRequest req((QUrl(agentPanelPluginRegistryUrl())));
        QNetworkReply *rep = nam->get(req);
        QObject::connect(rep, &QNetworkReply::finished, ui, [ui, rep, nam] {
            const QByteArray data = rep->readAll();
            rep->deleteLater(); nam->deleteLater();
            if (rep->error() == QNetworkReply::NoError) {
                const QJsonArray arr = QJsonDocument::fromJson(data).array();
                for (const QJsonValue &v : arr)
                    agentPanelStoreEntries().insert(v.toObject().value(QStringLiteral("id")).toString(), v.toObject());
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
        const QString url = agentPanelStoreEntries().value(id).value(QStringLiteral("downloadUrl")).toString();
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
