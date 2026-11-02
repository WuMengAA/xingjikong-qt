// 灵动岛统一入口（2026-10-07 · #98）：被控端一切通知统一走 IslandOverlay。
// 2026-10-30 main 拆分 #7：从 main.cpp（原 3541-3808）整块迁出，函数/注释逐字保留。
//
// 调用方按"这条内容要不要停下来看清"来选形态：
//   · 顶部居中胶囊 Capsule、Collapsed(56)↔Expanded(168) 弹性缓动、出现/消失都带动画；
//   · 默认鼠标穿透（WA_TransparentForMouseEvents）—— 不挡后面的课件/白板；
//   · 需要"用户点一下"的（收到文件想看清楚 / 通知确认）走 showWithActions + setInteractive(true)。
// 注：2026-10-14 P1 起 NotifyWindow 已删除，灵动岛是被控端唯一展示入口。
//
// 状态全局仍驻 main（文件作用域外部链接），本模块按既有 extern 模式复用。

#include "notify_entry.h"

#include "../system/danmaku.h"
#include "../automation/loop_reminder.h"
#include "../automation/notification_gate.h"
#include "provider_registry.h"   // 2026-11-01：提供方/渠道门控
#include "../core/AgentSettings.h"
#include "../tts/tts_speaker.h"
#include <functional>     // std::function
#include <QJsonArray>
#include <QSystemTrayIcon>

// ── 复用 main 的状态全局（提升文件作用域外部链接，同 g_uid / agentEnvPath 模式）──
extern QSystemTrayIcon *g_tray;
extern bool g_remoteControl;
extern LoopReminder g_loopReminder;
extern NotificationGate g_notifGate;
extern void sendActionReceipt(const QString &id, const QString &action, const QString &result,
                              const QString &error, const QJsonObject &data);

void islandShow(const QString &title, const QString &desc, const QString &icon, int ms, bool interactive,
                const QString &openPath, IslandOverlay::Form form, bool fromAdmin)
{
    // ── 2026-10-19 远控期崩溃规避（P0 生产事故）──────────────────────────
    // 真机三份 WER 全内存 dump 实证：远程控制开始后约 4-26s，agent 进程 0xC0000005，
    // 出错模块 Qt6Core.dll+0x38c0（QObject::d_func，rcx=0）。栈是纯 QML 路径：
    //   QObject::d_func ← QQmlDelegateModelItem::destroyObjectLater
    //   ← QQmlDelegateModelPrivate::destroyCacheItem ← QQmlDelegateModel::release
    //   ← QQuickRepeater::clear ← QQuickRepeater::setModel ← QQmlBinding 重算
    //   ← QQmlData::destroyed ← QObject::~QObject
    // 即：某对象析构 → QML 绑定重算 → 某个 Repeater 的 model 被整表替换 →
    //     Repeater 清空 delegate 时，缓存里已有"object 已销毁"的 delegate ⇒ 对 null 调
    //     d_func。岛（island）是唯一每帧都在更新 model 的 QML 视图，showIsland 每次都会
    //     upsertTask（任务表整表替换）+ 8s 后 dismiss→enterIdle→archiveToHistory，
    //     正好是这条链的入口。GDI 时代的两份 dump（10-18 22:56 / 23:29）崩溃点完全相同
    //     ⇒ 与 DXGI 抓屏无关，是岛 QML 的既有结构性缺陷，远控只是把它踩出来。
    // 处置：远控会话期间岛一律不写（改走托盘气泡，学生知情边界不破），
    //       待岛 QML 侧换成 ListModel 增量更新（Repeater → ListModel 单批改造）后再放开。
    if (g_remoteControl) {
        if (g_tray)
            g_tray->showMessage(title, desc.isEmpty() ? title : desc,
                                QSystemTrayIcon::Information, ms > 0 ? ms : 5000);
        return;
    }
    IslandOverlay *isl = IslandOverlay::instance();
    if (openPath.isEmpty()) isl->clearOpenPath();
    else                    isl->setOpenPath(openPath);
    // 交互态（可点）清掉旧的 openPath，免得下一条提示点一下去翻上一份文件。
    isl->setInteractive(interactive);
    isl->showIsland(title, desc, icon, ms, form, fromAdmin);
}

void islandShowAt(const QString &title, const QString &desc, const QString &icon,
                  int ms, IslandOverlay::Form form, bool fromAdmin, qint64 noticeAtMs)
{
    // ⚠️ 复用 IslandOverlay 的「下一条通知发生时刻」一次性注入位（m_noticeHintMs，
    //    setNoticeTimeForNext）：showIsland 会读它并清零——所以必须紧贴着每次
    //    showIsland 设置，不能提前设。
    IslandOverlay::instance()->setNoticeTimeForNext(noticeAtMs);
    islandShow(title, desc, icon, ms, false, QString(), form, fromAdmin);
}

void trayNotify(const QString &title, const QString &body,
                QSystemTrayIcon::MessageIcon icon, int ms)
{
    // 托盘气泡兜底：灵动岛是贴桌面顶层的胶囊，被控端缩进托盘 / 教室投影投在别处 /
    // 用户正在全屏课件里，那一小条容易整个漏掉。像"文件落到本机了"这种结果型事件，
    // 再往 Windows 通知中心推一条才兜得住（tray 没起来就静默跳过，不报错）。
    if (!g_tray) return;
    g_tray->showMessage(title, body.isEmpty() ? title : body, icon, ms);
}

QString notifyFromParams(const QJsonObject &params, const QString &id)
{
    const QString kind = params.value(QStringLiteral("kind")).toString().trimmed().toLower();
    const QString title = params.value(QStringLiteral("title")).toString().trimmed();
    if (title.isEmpty())
        return QStringLiteral("notify 缺 title");
    const QString content = params.value(QStringLiteral("content")).toString();
    // ── 考试模式门控（2026-10-09 接线 · §6.2）：考试激活期间一切通知静默丢弃 ──
    // 不弹、不响、不记 error —— 学生机在考试中收到任何通知都应像没收到一样。
    // 2026-10-21（审查 #7）：返回 "skipped:" 前缀 = 门控静默丢弃。调用方识别该前缀
    //   转 **skipped** 回执（云端已有该语义，重复下发保护就在用）—— 原来返回普通
    //   错误串被当成 failed，管理端会看到一条失败并可能重试/报警，和"像没收到
    //   一样"正好相反。
    const bool gated = !g_notifGate.allow(
        kind == QLatin1String("danmaku") ? NotificationGate::Danmaku
        : kind == QLatin1String("loop") ? NotificationGate::LoopRemind
        : NotificationGate::Island);
    if (gated) return QStringLiteral("skipped:exam-active-gated");
    // P0-1（2026-10-14）：本地「通知与弹窗」开关关了就整体静默（考试门控优先，已在上方处理）。
    if (!AgentSettings::notifyEnabled()) return QStringLiteral("notify-disabled-by-local-setting");
    // C 批（2026-10-27）：岛黑名单 —— 把"老弹烦人通知的来源"静默丢弃（不弹/不响/不入历史）。
    // 来源键取自通知载荷 `from`（应用名/插件 id/通道名，由下发方给，空 = 不适用）。
    // 与系统统一配置：黑名单落 QSettings（设备级），岛和管理端读到的是同一份。
    {
        const QString from = params.value(QStringLiteral("from")).toString().trimmed();
        if (!from.isEmpty() && AgentSettings::notifyBlacklist().contains(from))
            return QStringLiteral("skipped:blacklisted");
    }
    const int seconds = params.value(QStringLiteral("seconds")).toInt(0);
    // flags.speech → TTS 朗读（缺省不读，避免每教室都响）
    const QJsonObject flags = params.value(QStringLiteral("flags")).toObject();
    const bool tts = flags.value(QStringLiteral("speech")).toBool(false)
                     || flags.value(QStringLiteral("speech_enabled")).toBool(false);
    // flags.severity → 全屏配色（2026-10-05 补）：站点已发对字段，被控端此前不读。
    // 站点白名单已收敛为 remind/inform/urgent 三档；未知值一律回落 remind（不做任意串插进样式）。
    const QString severity = flags.value(QStringLiteral("severity")).toString().trimmed().toLower();
    // flags.emergency_confirm → 紧急锁定：不自动关 + 强制置顶（2026-10-05 补）
    // 站点此前在 params 顶层也发过 emergency_confirm（broadcast.ts 的注入分支），此处一并兜底。
    const bool emergency = flags.value(QStringLiteral("emergency_confirm")).toBool(false)
                           || flags.value(QStringLiteral("topmost")).toBool(false)
                           || params.value(QStringLiteral("emergency_confirm")).toBool(false);
    // 确认 + 快捷回复（2026-10-07 · 通知被控端确认快捷回复）：
    // flags.confirm → 需要学生点确认；flags.replies → 快捷回复话术列表（如 ["收到","好的"]）。
    const bool needConfirm = flags.value(QStringLiteral("confirm")).toBool(false)
                             || params.value(QStringLiteral("confirm")).toBool(false);
    QStringList replies;
    const QJsonArray repArr = flags.value(QStringLiteral("replies")).toArray();
    if (!repArr.isEmpty()) {
        for (const QJsonValue &v : repArr) replies.append(v.toString());
    } else {
        // 默认快捷回复（不传 replies 时给一组通用话术）
        replies = { QStringLiteral("\u6536\u5230"), QStringLiteral("\u597d\u7684"),
                    QStringLiteral("\u7a0d\u540e\u5904\u7406") };
    }

    // 形态分流（P1 · 2026-10-14）：kind 直接映射灵动岛形态，**不再经 NotifyWindow**
    // （岛已覆盖全屏/居中/胶囊/确认/紧急，是被控端唯一展示入口）。
    //   popup / 未知 → Centered（屏幕正中卡片，安全默认）
    //   fullscreen → Fullscreen（铺满整屏）
    //   banner → 顶部通栏（长文案单行横幅）｜ mini → 右下角角标（常驻轻状态，不挡课件）
    //   2026-10-27（用户裁决）：**被控端的岛保留并扩充** —— 产品笔记里
    //   「通知只保留 弹窗 / 岛上 / 跑马灯 / 全屏」这条为准，kind=island 是四形态之一，
    //   恢复为顶部胶囊（Capsule）。此前 2026-10-26 P0 曾把它归一为弹窗，那是按
    //   笔记另一处「灵动岛无用不如通知方便」做的 —— 那一处指的是**管理端**的岛
    //   （已删，见 viewer 侧），被控端的岛是学生的信息出口，不跟着删。
    // ⚠️ 未知 kind 必须继续落 Centered —— 管理端老版本发的 kind 不能被新枚举带偏。
    IslandOverlay::Form f = IslandOverlay::Form::Centered;
    if (kind == QLatin1String("island"))          f = IslandOverlay::Form::Capsule;
    else if (kind == QLatin1String("fullscreen")) f = IslandOverlay::Form::Fullscreen;
    else if (kind == QLatin1String("banner"))     f = IslandOverlay::Form::Banner;
    else if (kind == QLatin1String("mini"))       f = IslandOverlay::Form::Mini;

    // ── 弹幕（2026-10-09 接线 · §1.2/§6.1）：kind=danmaku → 字幕式横向滚动 ──
    if (kind == QLatin1String("danmaku")) {
        // 2026-10-15 组件5：g_danmaku 全局指针 → 单例访问器（与学生祝福弹幕同层）
        DanmakuWidget::danmakuWidget()->push(content.isEmpty() ? title : (title + QStringLiteral("：") + content));
        // 同样进托盘通知留存（与 popup 行为一致，别丢静默列表）
        if (g_tray) {
            g_tray->showMessage(title, content.isEmpty() ? title : content,
                                QSystemTrayIcon::Information, 10000);
        }
        return QString();
    }

    // ── 循环提醒（2026-10-09 接线 · §1.2 loopRemind）：flags.loop / params.repeat ──
    // 展示超时未确认 → 间隔重现；只有学生点确认才终止。id 用 notice_id 或 title（幂等刷新）。
    const bool loop = flags.value(QStringLiteral("loop")).toBool(false)
                      || params.value(QStringLiteral("repeat")).toBool(false)
                      || params.value(QStringLiteral("loopRemind")).toBool(false);
    if (loop) {
        const QString loopId = params.value(QStringLiteral("notice_id")).toString(
            params.value(QStringLiteral("id")).toString());
        LoopReminder::Config cfg;
        cfg.intervalSec = params.value(QStringLiteral("intervalSec")).toInt(
                              flags.value(QStringLiteral("interval_sec")).toInt(60));
        // 确认回调：学生点「确认」→ 终止循环（唯一正常退出路径），并二次回执云端
        std::function<void(const QString &)> loopConfirm = [loopId, id](const QString &reply) {
            g_loopReminder.confirm(loopId);
            if (!id.isEmpty()) {
                QJsonObject d;
                d.insert(QStringLiteral("confirmed"), true);
                d.insert(QStringLiteral("reply"), reply);
                sendActionReceipt(id, QStringLiteral("notify"), QStringLiteral("confirmed"),
                                  QString(), d);
            }
        };
        // 立即展示第一轮：并入灵动岛（确认按钮现在在**岛本体**上），点「确认」→ loopConfirm
        {
            QStringList loopActs;
            loopActs << QStringLiteral("确认");
            for (const QString &rr : replies)
                if (!rr.trimmed().isEmpty() && !loopActs.contains(rr)) loopActs << rr;
            IslandOverlay::instance()->setSeverity(severity);
            IslandOverlay::instance()->showWithActions(title, content, QStringLiteral("bell"),
                                                       IslandOverlay::Form::Centered, loopActs,
                                                       loopConfirm, 0, true);
        }
        if (tts)
            speakText(content.trimmed().isEmpty() ? title
                                                  : (title + QStringLiteral("：") + content));
        if (g_tray) {
            g_tray->showMessage(title, content.isEmpty() ? title : content,
                                QSystemTrayIcon::Information, 10000);
        }
        return QString();
    }

    // 确认/快捷回复回调：把结果作为**二次回执**发给云端（原 notify 回执已发 done，
    // 这条补充 confirmed+reply，云端 recordResult 按 id 配对；管理端据此显示"已确认"）。
    std::function<void(const QString &reply)> onConfirm;
    if (needConfirm || emergency) {
        onConfirm = [id](const QString &reply) {
            if (id.isEmpty()) return;
            QJsonObject d;
            d.insert(QStringLiteral("confirmed"), true);
            d.insert(QStringLiteral("reply"), reply);
            sendActionReceipt(id, QStringLiteral("notify"), QStringLiteral("confirmed"), QString(), d);
        };
    }

    // ── #98：形态分流（2026-10-09）────────────────────────────────────────
    // 用户要的是"弹窗则居中，全屏则放大全屏"。所以**纯展示类**的通知走灵动岛：
    //   popup      → 灵动岛 Centered（屏幕正中卡片）
    //   fullscreen → 灵动岛 Fullscreen（铺满整屏，带放大过渡）
    //   island     → 灵动岛 Capsule（顶部胶囊）
    // ⚠️ 需要交互的（要确认 / 有快捷回复 / 紧急"点一下才关"）走灵动岛的 showWithActions：
    //    岛本体自带按钮行（2026-10-09 起「确认」已并入岛），不再依赖 NotifyWindow。
    // ── 时长：>0 用给定秒数；0/缺省 → 按字数自适应（约 5 秒起）────────────────────
    // ⚠️ 2026-10-09 修 bug：原来直接把 seconds*1000 传给灵动岛，而灵动岛按
    //    "durationMs>0 才自动收"判定 ⇒ seconds=0 时永远不消失、一直置顶。
    //    这里把"0=自适应"的契约在**传参前**兑现，公式复刻 NotifyWindow 那份
    //    （短 5s、长内容按字数抬升），避免两处口径漂移。
    const int durMs = seconds > 0
        ? qMin(seconds, 3600) * 1000
        : int(qMax(5000.0, 2500.0 + (title + QStringLiteral("：") + content).length() * 120.0));

    // ⚠️ 2026-10-09 修 bug「全屏通知没有颜色」：配色档必须**无条件先设**。
    //    原来只在"确认/紧急"那条 else 分支里调 setSeverity ⇒ **纯展示的全屏通知**
    //    （kind=fullscreen 且不需要确认）压根没设 severity，全屏永远是默认黑底，
    //    remind/inform/urgent 三档配色全被吞掉。而且不设还会残留上一条的档。
    //    （severity 只对 Fullscreen 生效，Centered/Capsule 走各自的固定配色，不受影响。）
    IslandOverlay::instance()->setSeverity(severity);

    const bool needsInteraction = needConfirm || emergency;

    // 2026-11-01（顶替 ClassIsland 第 2 步）：**提供方 / 渠道门控**。
    //   provider 缺省 admin（管理端下发）；插件经本地接口推的由调用方带 provider=plugin，
    //   这里再用 `from` 兜一层（老插件不带 provider 时也不会被当成管理端）。
    //   ⚠️ 未知名一律放行（见 provider_registry.h「未知名策略」）—— 老版本云端不该因为
    //      被控端升级而静默丢掉通知。
    //   ⚠️ 这是"这台讲台机要不要看这类提示"的本地开关，与云端「通知总开关」互不替代。
    {
        const QString from = params.value(QStringLiteral("from")).toString().trimmed();
        QString provider = params.value(QStringLiteral("provider")).toString().trimmed();
        if (provider.isEmpty())
            provider = from.startsWith(QLatin1String("plugin")) ? QStringLiteral("plugin")
                                                                : QStringLiteral("admin");
        const QString channel = params.value(QStringLiteral("channel")).toString().trimmed();
        if (!notify::NotifyProviders::instance().allow(provider, channel))
            return QStringLiteral("skipped:provider-disabled:") + provider;
    }

    if (!needsInteraction) {
        // 2026-11-01（顶替 ClassIsland 第 1 步）：flags.endAt = 绝对结束时刻（epoch 毫秒）。
        //   给了就"挂到那个时刻"而不是"播 durMs 毫秒"（岛内 EndTime 优先，见 setEndTimeForNext）。
        //   ⚠️ 只对**纯展示**生效：需确认/紧急的要等学生点，不能到点自己跑掉。
        //   ⚠️ 紧挨着展示调用设置：hint 是一次性的，中间任何一次 showIsland 都会把它吃掉。
        IslandOverlay::instance()->setEndTimeForNext(
            static_cast<qint64>(flags.value(QStringLiteral("endAt")).toDouble()));
        islandShow(title, content, QStringLiteral("bell"), durMs, false, QString(), f, true);
    } else {
        // 确认 / 紧急：按钮行（「确认」+ 快捷回复），点哪枚回哪句。
        // ⚠️ 胶囊那档太矮放不下按钮 ⇒ 升到 Centered（全屏仍是 Fullscreen）。
        if (f == IslandOverlay::Form::Capsule) f = IslandOverlay::Form::Centered;
        QStringList acts;
        acts << QStringLiteral("确认");
        for (const QString &rr : replies)
            if (!rr.trimmed().isEmpty() && !acts.contains(rr)) acts << rr;
        IslandOverlay::instance()->showWithActions(title, content, QStringLiteral("bell"),
                                                   f, acts, onConfirm, 0, true);
    }

    // TTS（调用方要求时）：读「标题：正文」，与岛渲染口径一致
    if (tts)
        speakText(content.trimmed().isEmpty() ? title
                                              : (title + QStringLiteral("：") + content));

    // Windows 通知中心留存（托盘在才有）—— 换呈现不该丢掉静默列表
    if (g_tray) {
        g_tray->showMessage(title, content.trimmed().isEmpty() ? title : content,
                            QSystemTrayIcon::Information, 10000);
    }
    return QString();   // 成功
}
// ── 考试模式提示（2026-11-01 · 顶替 ClassIsland 第 2 步）──────────────────────
// 归属「考试」提供方：设置页关掉「考试」⇒ 进出考试模式都不弹。
// 形态用 Capsule（不抢屏、不挡课件）——考试模式本身已经全屏拦截，提示只需"告知一声"。
// ⚠️ 与考试门控（NotificationGate::ExamActive）的分工：那个拦**别人**的通知，
//    这个是**考试自己**的告知；两者互不替代。
void notifyExamMode(bool entering, const QString &detail)
{
    const QString channel = entering ? QStringLiteral("exam.start") : QStringLiteral("exam.end");
    if (!notify::NotifyProviders::instance().allow(QStringLiteral("exam"), channel))
        return;
    const QString title = entering ? QStringLiteral("考试模式已开始") : QStringLiteral("考试模式已结束");
    QString desc = detail.trimmed();
    if (desc.isEmpty())
        desc = entering ? QStringLiteral("请勿切换窗口，完成后会自动结束")
                        : QStringLiteral("已恢复平常状态");
    IslandOverlay::instance()->setProviderForNext(QStringLiteral("exam"));
    IslandOverlay::instance()->setChannelForNext(QStringLiteral("考试"));
    IslandOverlay::instance()->setSkipHistoryForNext(true);
    islandShow(title, desc, entering ? QStringLiteral("ban") : QStringLiteral("circle-check"),
               entering ? 8000 : 6000, false, QString(), IslandOverlay::Form::Capsule, false);
}
