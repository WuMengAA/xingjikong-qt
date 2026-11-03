// 星集控被控端 · 通知与呈现指令 —— executeAction 按域拆分 #1（2026-11-02 · 屎山整理）
//
// 负责：定时通知 / 时间同步 / 岛组件面板 / PPT 放映 / 定时闹钟 / 大屏通知。
//
// 由 src/action/execute_action.cpp 原 2029 行单函数机械拆出：**块体逐字保留**，
// 唯一改动是每块**内部**首行插入 `handled = true;`（告诉调用方这条指令归本域管）。
// 函数名/参数/分支顺序/错误文案/回执字段全部不变，行为零变化。
//
// 拆分依据是 AGENTS.md §10.5 文件规模红线（.cpp ≤ 600 行）与 §10.6 禁上帝类：
// 原文件是一个函数里的 54 个指令分支，随产品演进步步膨胀，任何一处改动都要在
// 2000 行里定位。按**指令域**拆才对得上「一条指令归谁管」这个真实边界。

#include "execute_action_shared.h"
#include "../island/island_countdown_target.h"

ExecOut executeNoticeAction(const QString &action, const QJsonObject &params, bool &handled)
{
    ExecOut out;

    // ── 定时通知（2026-10-27 · F 收尾 · 第六刀）：schedule_notify / list_notifies / cancel_notify ──
    // 与闹钟（AlarmDomain）分工：闹钟 = 响铃/锁屏/TTS「叫醒」；这里 = 走通知体系（岛/弹窗/跑马灯/全屏）「告诉」。
    // 产品原话「定时任务里应包括关机、定时闹钟（附有锁屏与相间选时，类似手机闹钟）」——关机与闹钟已有，这条是补的定时通知。
    if (action == QStringLiteral("schedule_notify")) {
        handled = true;
        NotifyDomain::NotifyTask t;
        t.id = params.value(QStringLiteral("id")).toString().trimmed();
        if (t.id.isEmpty())
            t.id = QStringLiteral("ntf-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        t.time = params.value(QStringLiteral("time")).toString().trimmed();
        t.times = params.value(QStringLiteral("times")).toInt(1);
        t.intervalMin = params.value(QStringLiteral("intervalMin")).toInt(0);
        t.title = params.value(QStringLiteral("title")).toString();
        t.content = params.value(QStringLiteral("content")).toString();
        t.channel = params.value(QStringLiteral("channel")).toString().trimmed();
        t.createdMs = QDateTime::currentMSecsSinceEpoch();
        const QJsonArray daysArr = params.value(QStringLiteral("days")).toArray();
        for (const auto &dv : daysArr) {
            const int day = dv.toInt(0);
            if (day >= 1 && day <= 7 && !t.days.contains(day)) t.days.append(day);
        }
        QString err;
        if (!NotifyDomain::upsert(t, &err)) {      // 校验失败如实报，不静默
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule_notify: ") + err;
            return out;
        }
        const qint64 next = NotifyDomain::nextFireMs(t, QDateTime::currentMSecsSinceEpoch(), 0);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), t.id);
        out.data.insert(QStringLiteral("times"), qMax(1, t.times));
        out.data.insert(QStringLiteral("nextAt"),
                        next > 0 ? QDateTime::fromMSecsSinceEpoch(next).toString(Qt::ISODateWithMs)
                                 : QString());
        if (!t.days.isEmpty() && t.times > 1)
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("days 非空时按每天一次处理，times 被忽略"));
        return out;
    }

    if (action == QStringLiteral("list_notifies")) {
        handled = true;
        const QVector<NotifyDomain::NotifyTask> tasks = NotifyDomain::loadTasks();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonArray items;
        for (const auto &t : tasks) {
            QJsonObject o;
            o.insert(QStringLiteral("id"), t.id);
            o.insert(QStringLiteral("time"), t.time);
            o.insert(QStringLiteral("times"), t.times);
            o.insert(QStringLiteral("intervalMin"), t.intervalMin);
            o.insert(QStringLiteral("title"), t.title);
            o.insert(QStringLiteral("content"), t.content);
            o.insert(QStringLiteral("channel"), t.channel);
            QJsonArray d;
            for (int day : t.days) d.append(day);
            o.insert(QStringLiteral("days"), d);
            const qint64 nx = NotifyDomain::nextFireMs(t, now, 0);
            o.insert(QStringLiteral("nextAt"),
                     nx > 0 ? QDateTime::fromMSecsSinceEpoch(nx).toString(Qt::ISODateWithMs)
                            : QString());
            items.append(o);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), items.size());
        return out;
    }

    if (action == QStringLiteral("cancel_notify")) {
        handled = true;
        const QString id = params.value(QStringLiteral("id")).toString().trimmed();
        if (!NotifyDomain::cancel(id)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("cancel_notify: 找不到 id=%1 的定时通知").arg(id);
            return out;
        }
        out.result = QStringLiteral("done");
        return out;
    }

    // ── 时间同步（2026-10-27 · G3 灵动岛拓展第二项）：time_sync / time_offset / time_status ──
    // 三条指令共用 TimesyncDomain（NTP + 人工偏移）。产品口径：提醒要准点，且学校铃声
    // 与标准时间常有偏差 —— 偏移只改**判定**，不改系统钟（理由见 timesync_domain.h）。
    if (action == QStringLiteral("time_sync")) {
        handled = true;
        // 异步：NTP 一轮最长 3s×2 台，阻塞等会让整条指令超时（云端 10s 兜底）。
        // ⇒ 只报"是否真的发起了"，结果让 time_status 查 —— 不谎称已成功。
        const bool started = TimesyncDomain::syncNow();
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("started"), started);
        out.data.insert(QStringLiteral("note"),
                        started ? QStringLiteral("已发起 NTP 同步，约 1–6 秒后用 time_status 查看结果")
                                : QStringLiteral("已有一轮同步在进行，本次未重复发起"));
        return out;
    }

    if (action == QStringLiteral("time_offset")) {
        handled = true;
        const QJsonValue jm = params.value(QStringLiteral("minutes"));
        const int m = jm.isDouble() ? jm.toInt() : jm.toString().trimmed().toInt();
        if (!jm.isDouble() && jm.toString().trimmed().isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("time_offset: 需要 minutes（整数，正=判定时间往前拨；0=取消偏移）");
            return out;
        }
        if (!TimesyncDomain::setOffsetMinutes(m)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("time_offset: minutes=%1 越界（允许 ±720）").arg(m);
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("offsetMin"), m);
        out.data.insert(QStringLiteral("skewMs"), (double)TimesyncDomain::skewMs());
        out.data.insert(QStringLiteral("note"),
                        QStringLiteral("只影响课表/提醒判定，系统时钟未修改"));
        return out;
    }

    if (action == QStringLiteral("time_status")) {
        handled = true;
        const TimesyncDomain::Status st = TimesyncDomain::status();
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("ntpOk"), st.ntpOk);
        out.data.insert(QStringLiteral("deltaMs"), (double)st.deltaMs);
        out.data.insert(QStringLiteral("offsetMin"), st.offsetMin);
        out.data.insert(QStringLiteral("skewMs"), (double)TimesyncDomain::skewMs());
        out.data.insert(QStringLiteral("server"), st.server);
        out.data.insert(QStringLiteral("lastAttempt"), st.lastAttempt);
        out.data.insert(QStringLiteral("lastSyncAt"),
                        st.lastSyncMs > 0
                            ? QDateTime::fromMSecsSinceEpoch(st.lastSyncMs).toString(Qt::ISODateWithMs)
                            : QString());
        out.data.insert(QStringLiteral("error"), st.error);
        // 判定用时间 + 系统时间的对照：一眼看出偏移生效了没有（诊断必备）
        out.data.insert(QStringLiteral("judgeTime"),
                        TimesyncDomain::now().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        out.data.insert(QStringLiteral("systemTime"),
                        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        return out;
    }

    // ── 岛组件面板（2026-10-27 · G4/G5/G6）：island_panel / island_panel_close ──
    // 对标 ClassIsland 的「组件」：日期 / 课表 / 倒计时。
    // 与通知的区别（产品口径）：通知是"事件"（来一条看完就走），面板是"看板"
    //   （今天第几节、还有多久下课 —— 要停下来看清、反复看）。
    // 所以面板走岛**展开区**并且可以常驻，不像通知那样 6 秒收走。
    if (action == QStringLiteral("island_panel")) {
        handled = true;
        const QString face = params.value(QStringLiteral("face")).toString().trimmed().toLower();
        // durationSec：0 = 常驻（等 close 或下一条通知顶掉）；缺省 60 秒。
        // ⚠️ 不用 showIsland 的 5 秒默认值 —— 课表看 5 秒根本读不完。
        const int durSec = params.contains(QStringLiteral("durationSec"))
            ? params.value(QStringLiteral("durationSec")).toInt(60) : 60;
        if (durSec < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("island_panel: durationSec 不能为负（0 = 常驻）");
            return out;
        }
        QString err;
        QJsonObject data;
        if (!PanelDomain::show(face, params, durSec * 1000, &err, &data)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("island_panel: ") + err;
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("face"), face);
        out.data.insert(QStringLiteral("durationSec"), durSec);
        // 把**真正显示出去的数据**也回执一份：管理端/端到端脚本能直接断言
        // "课表拿到几节、倒计时目标对不对"，不用去截图猜。
        out.data.insert(QStringLiteral("panel"), data);
        return out;
    }

    if (action == QStringLiteral("island_panel_close")) {
        handled = true;
        PanelDomain::close();
        out.result = QStringLiteral("done");
        return out;
    }

    // ── 倒计时固定目标（2026-11-03 · Task1 + Task5：替代 ClassIsland 倒计时组件）──
    //   管理端发 `set_countdown_target{to:"12:30",label:"午休"}` 持久化到 QSettings
    //   （键 panel/countdownTarget），常驻倒计时面（face 9）即显示该死时刻，跨重启保留。
    //   `{clear:true}` 或 `to` 空 ⇒ 清除，回落「课表边界」语义。
    //   to 必须 HH:MM 或 ISO8601，非法 ⇒ failed（不让坏配置悄悄生效）。
    //   ⚠️ 这是「配置」不是「临时面板」：临时面板走 island_panel{face:"countdown"}，
    //      两者语义同源但生命周期不同（临时=当次，固定=常驻持久化）。
    if (action == QStringLiteral("set_countdown_target")) {
        handled = true;
        const QJsonObject res = CountdownTarget::set(params);
        if (!res.value(QStringLiteral("ok")).toBool(false)) {
            out.result = QStringLiteral("failed");
            out.error = res.value(QStringLiteral("error")).toString();
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("to"), CountdownTarget::params().value(QStringLiteral("to")));
        return out;
    }

    // ── PPT 放映控制（2026-10-27 · I 批，用户称"尤为重要"）：ppt_status / ppt_next / ppt_prev / ppt_goto / ppt_app ──
    // 口径（照用户原话）：**没开 PPT 或没在放映 → 如实 failed，绝不假成功**。
    // 但 data 里**仍然回完整状态**（app/presenting/slide/total/available）——
    // 管理端要靠它显示"未放映"中性徽标，而不是把一条状态查询当红色错误。
    if (action == QStringLiteral("ppt_status") || action == QStringLiteral("ppt_next")
        || action == QStringLiteral("ppt_prev") || action == QStringLiteral("ppt_goto")
        || action == QStringLiteral("ppt_app")) {
            handled = true;

        // 三个字段是"本次实际控的是哪个"，每一次回执都带 —— 绝不让老师以为在控 Office 实际控的是 WPS
        const auto fillState = [&out](const PptDomain::Status &st) {
            const QString label = (st.app == QLatin1String("wps"))       ? QStringLiteral("WPS 演示")
                                : (st.app == QLatin1String("powerpoint")) ? QStringLiteral("PowerPoint")
                                                                          : QString();
            out.data.insert(QStringLiteral("app"), st.app);
            out.data.insert(QStringLiteral("appLabel"), label);
            out.data.insert(QStringLiteral("running"), st.running);
            out.data.insert(QStringLiteral("presenting"), st.presenting);
            out.data.insert(QStringLiteral("slide"), st.slide);
            out.data.insert(QStringLiteral("total"), st.total);
            out.data.insert(QStringLiteral("presentation"), st.presentation);
            QJsonArray avail;
            for (const QString &a : st.available) avail.append(a);
            out.data.insert(QStringLiteral("available"), avail);
        };

        if (action == QStringLiteral("ppt_app")) {
            const QString want = params.value(QStringLiteral("app")).toString().trimmed().toLower();
            QString err;
            bool saved = false;
            if (!PptDomain::setPreferredApp(want, &err, &saved)) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("ppt_app: ") + err;
                return out;
            }
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("preferred"), PptDomain::preferredApp());
            out.data.insert(QStringLiteral("saved"), saved);
            // 切完立刻探一次：告诉调用方"现在实际能控的是哪个"（互斥时这条最关键）
            fillState(PptDomain::query());
            if (!saved)
                out.data.insert(QStringLiteral("note"), err);
            return out;
        }

        if (action == QStringLiteral("ppt_status")) {
            const PptDomain::Status st = PptDomain::query();
            fillState(st);
            out.data.insert(QStringLiteral("preferred"), PptDomain::preferredApp());
            if (!st.ok || !st.presenting) {
                out.result = QStringLiteral("failed");
                out.error = st.error.isEmpty() ? QStringLiteral("没有在放映") : st.error;
                out.data.insert(QStringLiteral("note"),
                                QStringLiteral("状态已采集，但当前没有可翻页的放映"));
                return out;
            }
            out.result = QStringLiteral("done");
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("正在放映：第 %1 / %2 页").arg(st.slide).arg(st.total));
            return out;
        }

        int gotoIndex = 0;
        if (action == QStringLiteral("ppt_goto")) {
            const QJsonValue jv = params.value(QStringLiteral("index"));
            gotoIndex = jv.isDouble() ? jv.toInt() : jv.toString().trimmed().toInt();
            if (gotoIndex < 1) {
                out.result = QStringLiteral("failed");
                out.error = QStringLiteral("ppt_goto: 需要 index（页码从 1 开始）");
                return out;
            }
        }

        PptDomain::Status st;
        QString err;
        const bool done = (action == QStringLiteral("ppt_goto"))
            ? PptDomain::gotoSlide(gotoIndex, &st, &err)
            : PptDomain::step(action == QStringLiteral("ppt_next") ? 1 : -1, &st, &err);
        if (!done) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("%1: %2").arg(action, err);
            // 失败也把**能拿到的状态**带上（例如"没在放映"时 slide/total 为 0，
            // 管理端据此把按钮置灰而不是反复点）
            fillState(PptDomain::query());
            return out;
        }
        fillState(st);
        out.result = QStringLiteral("done");
        if (!st.presenting) {
            // 翻到最后一页 PowerPoint 会结束放映 —— 动作成功了，但要如实说清
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("已翻到末尾，放映已结束（现在没有可翻页的放映）"));
        } else if (action == QStringLiteral("ppt_goto") && st.slide != gotoIndex) {
            // 请求值与实际停的页不一致（PPT 会夹到有效范围）⇒ 说清，不回显请求值
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("请求第 %1 页，实际停在第 %2 / %3 页")
                                .arg(gotoIndex).arg(st.slide).arg(st.total));
        } else {
            out.data.insert(QStringLiteral("note"),
                            QStringLiteral("第 %1 / %2 页").arg(st.slide).arg(st.total));
        }
        return out;
    }

    // ── 闹钟（2026-10-26 · P1 产品笔记）：schedule_alarm / list_alarms / cancel_alarm ──
    if (action == QStringLiteral("schedule_alarm")) {
        handled = true;
        const QString time = params.value(QStringLiteral("time")).toString().trimmed();
        if (time.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule_alarm: 需要 time（HH:MM，24 小时制）");
            return out;
        }
        const QStringList tp = time.split(QLatin1Char(':'));
        bool okH = false, okM = false;
        if (tp.size() == 2) { tp.at(0).toInt(&okH); tp.at(1).toInt(&okM); }
        if (!okH || !okM) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule_alarm: time 格式应为 HH:MM");
            return out;
        }
        AlarmDomain::AlarmTask t;
        t.id = QStringLiteral("alm-") + QString::number(QDateTime::currentMSecsSinceEpoch());
        t.time = time;
        t.text = params.value(QStringLiteral("text")).toString().trimmed();
        t.lock = params.value(QStringLiteral("lock")).toBool(false);
        t.createdMs = QDateTime::currentMSecsSinceEpoch();
        const QJsonArray daysArr = params.value(QStringLiteral("days")).toArray();
        for (const auto &dv : daysArr) {
            const int day = dv.toInt(0);
            if (day >= 1 && day <= 7 && !t.days.contains(day)) t.days.append(day);
        }
        const qint64 next = AlarmDomain::nextAlarmMs(t, QDateTime::currentMSecsSinceEpoch());
        if (next < 0) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("schedule_alarm: 无法计算触发时间（检查 time/days）");
            return out;
        }
        QVector<AlarmDomain::AlarmTask> cur = AlarmDomain::loadAlarms();
        cur.append(t);
        AlarmDomain::saveAlarms(cur);
        AlarmDomain::armAlarmTimer(t);
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("id"), t.id);
        out.data.insert(QStringLiteral("nextAt"),
                        QDateTime::fromMSecsSinceEpoch(next).toString(Qt::ISODateWithMs));
        return out;
    }

    if (action == QStringLiteral("list_alarms")) {
        handled = true;
        const QVector<AlarmDomain::AlarmTask> tasks = AlarmDomain::loadAlarms();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QJsonArray items;
        for (const auto &t : tasks) {
            QJsonObject o;
            o.insert(QStringLiteral("id"), t.id);
            o.insert(QStringLiteral("time"), t.time);
            o.insert(QStringLiteral("text"), t.text);
            o.insert(QStringLiteral("lock"), t.lock);
            QJsonArray d;
            for (int day : t.days) d.append(day);
            o.insert(QStringLiteral("days"), d);
            const qint64 nx = AlarmDomain::nextAlarmMs(t, now);
            o.insert(QStringLiteral("nextAt"),
                     nx > 0 ? QDateTime::fromMSecsSinceEpoch(nx).toString(Qt::ISODateWithMs) : QString());
            items.append(o);
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("items"), items);
        out.data.insert(QStringLiteral("count"), items.size());
        return out;
    }

    if (action == QStringLiteral("cancel_alarm")) {
        handled = true;
        const QString id = params.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("cancel_alarm: 需要 id");
            return out;
        }
        // 2026-10-27 第五刀：定时器表随闹钟域一起搬走了，这里只问结果
        if (!AlarmDomain::cancelAlarm(id)) {
            out.result = QStringLiteral("failed");
            out.error = QStringLiteral("找不到 id=%1 的闹钟（可能已响或已取消）").arg(id);
            return out;
        }
        out.result = QStringLiteral("done");
        return out;
    }

    // 2026-10-04（乙阶段 2）：大屏通知（notify）——popup 居中弹窗 / island 灵动岛 / fullscreen 全屏遮罩，
    // 可配 TTS 朗读。站点广播（broadcast.ts）改走云端 WS 指令通道后，教室大屏不再依赖 CIMS。
    if (action == QStringLiteral("notify")) {
        handled = true;
        const QString err = notifyFromParams(params);
        // 2026-10-21（审查 #7）："skipped:" 前缀 = 门控静默丢弃（考试中），不是失败
        //   —— 管理端不该看到一条 failed 去重试/报警。
        if (err.startsWith(QLatin1String("skipped:"))) {
            out.result = QStringLiteral("skipped");
            out.error = err.mid(8);
            return out;
        }
        if (!err.isEmpty()) {
            out.result = QStringLiteral("failed");
            out.error = err;
            return out;
        }
        out.result = QStringLiteral("done");
        out.data.insert(QStringLiteral("notice_id"),
                        params.value(QStringLiteral("notice_id")).toVariant().toJsonValue());
        out.data.insert(QStringLiteral("kind"),
                        params.value(QStringLiteral("kind")).toString(QStringLiteral("popup")));
        return out;
    }

    return out;   // handled 仍为 false：这条指令不归本域，调用方继续往下问
}
