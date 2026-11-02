#include "timetable_apply.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QDebug>
#include <QSaveFile>

namespace TimetableApply {

namespace {

constexpr const char *kSchema = "classisland/v1";

QString clipId(const QString &s) { return s.trimmed().left(64); }

/** "08:00" / "08:00:00" → "HH:mm:ss"（ClassIsland 的 TimeLayout 要带秒）。 */
QString normTime(const QString &raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty()) return QString();
    const QStringList parts = s.split(QLatin1Char(':'));
    if (parts.size() < 2) return QString();
    const QString hh = parts.at(0).rightJustified(2, QLatin1Char('0'));
    const QString mm = parts.at(1).leftJustified(2, QLatin1Char('0'));
    const QString ss = parts.size() > 2 ? parts.at(2).leftJustified(2, QLatin1Char('0'))
                                        : QStringLiteral("00");
    return QStringLiteral("%1:%2:%3").arg(hh, mm, ss);
}

/** 落盘前备份原档案（只留最近一份，避免一天下发十次塞满磁盘）。 */
void backupOnce(const QString &path)
{
    if (!QFile::exists(path)) return;
    const QString bak = path + QStringLiteral(".bak");
    QFile::remove(bak);
    if (!QFile::copy(path, bak))
        qWarning().noquote() << "[timetable] ⚠️ 备份原档案失败（继续覆盖）：" << path;
}

} // namespace

Result applyEnvelope(const QJsonObject &envelope, const QString &profilePath,
                     const QString &classCode)
{
    Result out;
    out.path = profilePath;

    // ── 1. 校验（挡住"写坏了比不写更糟"的两类） ──────────────────────────
    const QString schema = envelope.value(QStringLiteral("schema")).toString();
    if (schema != QLatin1String(kSchema)) {
        out.error = QStringLiteral("课表格式不受支持：%1（本机只认 %2）")
                        .arg(schema.isEmpty() ? QStringLiteral("（缺 schema）") : schema,
                             QLatin1String(kSchema));
        return out;
    }
    const QJsonArray days = envelope.value(QStringLiteral("days")).toArray();
    if (days.isEmpty()) {
        out.error = QStringLiteral("课表是空的（days=0），拒绝落盘 —— 否则本机课表会被清空");
        return out;
    }
    if (profilePath.trimmed().isEmpty()) {
        out.error = QStringLiteral("未配置课表档案路径");
        return out;
    }

    // ── 2. 时间表：**每种 timeLayoutId 各留一张**，不许合并成一个 ─────────
    // ★ 2026-11-03 修的真 bug（现场：下发 6 天 59 节，落盘 5 天 56 节，今日只显示 3 节）：
    //   改造前这里只取「第一个有课的日」引用的那张 TimeLayout，然后把**所有** ClassPlan
    //   的 TimeLayoutId 都指向它。云端一天的课表有 12 节、周日那张布局只有 3 个课时，
    //   多出来的 9 节在 schedule_clock 里撞上 slot >= slotTimeTable.size() → 静默丢弃。
    //   正确做法：云端的 layouts 有几个 id 就写几张，每个 ClassPlan 指向自己那张。
    const QJsonArray layouts = envelope.value(QStringLiteral("layouts")).toArray();
    QHash<QString, QJsonArray> itemsByLayout;          // 云端 timeLayoutId -> items[]
    for (const QJsonValue &l : layouts) {
        const QJsonObject lo = l.toObject();
        const QString id = lo.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;
        itemsByLayout.insert(id, lo.value(QStringLiteral("items")).toArray());
    }

    // 每张布局至少要能放下「引用它的那些天里最长的一天」的课时数。
    // 云端布局与课时本来就同源，正常相等；这里仍然按天数上限兜底补齐 ——
    // 宁可多一行时间表，也不能再出现「有 12 节课却只排出 3 个时段」的静默截断。
    QMap<QString, int> needSlots;
    QMap<QString, QJsonArray> sampleLessons;           // 补齐时取用的样本课时
    for (const QJsonValue &dv : days) {
        const QJsonObject d = dv.toObject();
        QString tl = d.value(QStringLiteral("timeLayoutId")).toString();
        if (tl.isEmpty()) tl = QStringLiteral("layout-from-cloud");
        const QJsonArray ls = d.value(QStringLiteral("lessons")).toArray();
        if (ls.size() > needSlots.value(tl)) {
            needSlots[tl] = ls.size();
            sampleLessons[tl] = ls;
        } else if (!needSlots.contains(tl)) {
            needSlots.insert(tl, 0);
            sampleLessons.insert(tl, ls);
        }
    }
    if (needSlots.isEmpty()) {
        out.error = QStringLiteral("课表里没有任何可用的节次时间（days 里没有 timeLayoutId）");
        return out;
    }

    QMap<QString, QJsonArray> layoutRows;
    for (auto it = needSlots.begin(); it != needSlots.end(); ++it) {
        QJsonArray rows;
        int lessonRows = 0;
        for (const QJsonValue &v : itemsByLayout.value(it.key())) {
            const QJsonObject item = v.toObject();
            const QString start = normTime(item.value(QStringLiteral("start")).toString());
            const QString end = normTime(item.value(QStringLiteral("end")).toString());
            if (start.isEmpty() || end.isEmpty()) continue;
            const bool isLesson = item.value(QStringLiteral("kind")).toString() == QLatin1String("lesson");
            if (isLesson) ++lessonRows;
            QJsonObject row;
            row.insert(QStringLiteral("StartTime"), start);
            row.insert(QStringLiteral("EndTime"), end);
            row.insert(QStringLiteral("TimeType"), isLesson ? 0 : 1);
            row.insert(QStringLiteral("BreakName"),
                       isLesson ? QString() : item.value(QStringLiteral("label")).toString());
            rows.append(row);
        }
        // 布局缺失 / 课时行不够：用课时自带的 start/end 补齐（追在末尾，保持既有下标不动）。
        const QJsonArray sample = sampleLessons.value(it.key());
        for (int i = lessonRows; i < it.value() && i < sample.size(); ++i) {
            const QJsonObject l = sample.at(i).toObject();
            const QString start = normTime(l.value(QStringLiteral("start")).toString());
            const QString end = normTime(l.value(QStringLiteral("end")).toString());
            if (start.isEmpty() || end.isEmpty()) continue;
            QJsonObject row;
            row.insert(QStringLiteral("StartTime"), start);
            row.insert(QStringLiteral("EndTime"), end);
            row.insert(QStringLiteral("TimeType"), 0);
            row.insert(QStringLiteral("BreakName"), QString());
            rows.append(row);
        }
        if (rows.isEmpty()) {
            out.error = QStringLiteral("课表里没有任何可用的节次时间（start/end 全空）");
            return out;
        }
        layoutRows.insert(it.key(), rows);
    }

    // ── 3. 组装 ClassIsland 官方 Profile（真机结构：TimeRule + Classes 下标） ──
    // 为什么用真机结构：Profile::fromJson 见到 TimeRule 才走真机分支，
    // 才能把 ClassPlanGroups 的 ClassPlanIds 从 AssociatedGroup 反向收集回来。
    // 用简化结构会得到「档案能读但课表群没成员」—— 界面显示没课、日志无异常。
    const QString groupId = QStringLiteral("group-default");
    QJsonObject subjects;
    for (const QJsonValue &v : envelope.value(QStringLiteral("subjects")).toArray()) {
        const QJsonObject s = v.toObject();
        const QString id = clipId(s.value(QStringLiteral("id")).toString());
        if (id.isEmpty()) continue;
        QJsonObject so;
        so.insert(QStringLiteral("Id"), id);
        so.insert(QStringLiteral("Name"), s.value(QStringLiteral("name")).toString());
        so.insert(QStringLiteral("SimplifiedName"), s.value(QStringLiteral("initial")).toString());
        so.insert(QStringLiteral("Teacher"), s.value(QStringLiteral("teacher")).toString());
        so.insert(QStringLiteral("Room"), QString());
        subjects.insert(id, so);
    }

    QJsonObject classPlans;
    int lessonCount = 0;
    for (const QJsonValue &dv : days) {
        const QJsonObject d = dv.toObject();
        const int dow = d.value(QStringLiteral("dow")).toInt();
        // ★ 云端 dow 是 ClassIsland 口径 **0=周日 … 6=周六**（class-timetable.js 的 DOW 表）。
        //   原先写 `dow < 1 || dow > 7` 直接把「周日」整条丢掉 —— 现场 6 天变 5 天的根因。
        if (dow < 0 || dow > 6) continue;
        QJsonArray classes;
        for (const QJsonValue &lv : d.value(QStringLiteral("lessons")).toArray()) {
            const QJsonObject l = lv.toObject();
            QJsonObject cell;
            cell.insert(QStringLiteral("SubjectId"), l.value(QStringLiteral("subjectId")).toString());
            // 空科目也要占位：Classes 下标就是节次，跳过一格会让后面全体前移。
            cell.insert(QStringLiteral("IsEnabled"), !l.value(QStringLiteral("subjectId")).toString().isEmpty());
            classes.append(cell);
            ++lessonCount;
        }
        const QString cpId = QStringLiteral("cp-%1").arg(dow);
        QString tl = d.value(QStringLiteral("timeLayoutId")).toString();
        if (tl.isEmpty()) tl = QStringLiteral("layout-from-cloud");
        QJsonObject cp;
        cp.insert(QStringLiteral("Id"), cpId);
        cp.insert(QStringLiteral("Name"),
                  d.value(QStringLiteral("name")).toString(
                      QStringLiteral("周%1").arg(dow)));
        // ★ 每天指向自己那张时间表 —— 写成同一张就是这次「12 节只排 3 个时段」的根因。
        cp.insert(QStringLiteral("TimeLayoutId"), tl);
        cp.insert(QStringLiteral("AssociatedGroup"), groupId);
        QJsonObject rule;
        // ★ ClassIsland 的 WeekDay 是 **0=周日 … 6=周六**；云端信封的 dow 是 Qt 口径
        //   （1=周一 … 7=周日）。不换算的话"周日"会变成"周一"，整周课表错位一天。
        //   证据：D:\Classlsland 档案里 Name=周一 的 plan 为 WeekDay=1、Name=周日 的为 WeekDay=0。
        rule.insert(QStringLiteral("WeekDay"), dow % 7);
        rule.insert(QStringLiteral("WeekCountDiv"), 0);        // 0 = 每周都上（ClassIsland 口径）
        rule.insert(QStringLiteral("WeekCountDivTotal"), 1);
        rule.insert(QStringLiteral("IsActive"), true);
        cp.insert(QStringLiteral("TimeRule"), rule);
        cp.insert(QStringLiteral("Classes"), classes);
        classPlans.insert(cpId, cp);
        ++out.dayCount;
    }

    // 每种布局一张表（名字取自云端，缺省「云端下发」）。
    QJsonObject timeLayouts;
    for (auto it = layoutRows.begin(); it != layoutRows.end(); ++it) {
        QString name = QStringLiteral("云端下发");
        for (const QJsonValue &l : layouts) {
            const QJsonObject lo = l.toObject();
            if (lo.value(QStringLiteral("id")).toString() == it.key()) {
                const QString n = lo.value(QStringLiteral("name")).toString();
                if (!n.isEmpty()) name = n;
                break;
            }
        }
        QJsonObject tl;
        tl.insert(QStringLiteral("Id"), it.key());
        tl.insert(QStringLiteral("Name"), name);
        tl.insert(QStringLiteral("Layouts"), it.value());
        timeLayouts.insert(it.key(), tl);
    }

    QJsonObject grp;
    grp.insert(QStringLiteral("Id"), groupId);
    grp.insert(QStringLiteral("Name"),
               envelope.value(QStringLiteral("group")).toObject()
                   .value(QStringLiteral("label")).toString(QStringLiteral("云端课表")));
    grp.insert(QStringLiteral("Color"), QStringLiteral("#2FB8C6"));
    // ⚠️ 故意**不写** ClassPlanIds：真机结构没有这个字段，Profile::fromJson 靠
    // AssociatedGroup 反向收集。写了反而会让 isReal 检测失效（见 profile.h）。
    QJsonObject groups;
    groups.insert(groupId, grp);

    QJsonObject root;
    root.insert(QStringLiteral("Name"),
                classCode.isEmpty() ? QStringLiteral("云端下发")
                                    : QStringLiteral("云端下发 · %1").arg(classCode));
    root.insert(QStringLiteral("Id"), QStringLiteral("profile-cloud"));
    root.insert(QStringLiteral("IsActive"), true);
    root.insert(QStringLiteral("SelectedClassPlanGroupId"), groupId);
    root.insert(QStringLiteral("TimeLayouts"), timeLayouts);
    root.insert(QStringLiteral("ClassPlans"), classPlans);
    root.insert(QStringLiteral("Subjects"), subjects);
    root.insert(QStringLiteral("ClassPlanGroups"), groups);

    // ── 4. 内容未变就不写盘（下发是幂等的：老师点两次不该让岛重排两次） ──
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QFile probe(profilePath);
    if (probe.open(QIODevice::ReadOnly)) {
        const QByteArray old = probe.readAll();
        probe.close();
        if (old == payload) {
            out.ok = true;
            out.changed = false;
            out.lessonCount = lessonCount;
            qInfo().noquote() << "[timetable] 下发的课表与本地一致，跳过写盘：" << profilePath;
            return out;
        }
    }

    // ── 5. 原子写盘（QSaveFile：写 .tmp → 成功才 rename） ─────────────────
    // 直接 truncate 原文件时若进程被杀，档案变半截 JSON，ClassIsland 与本机同时读坏。
    QDir().mkpath(QFileInfo(profilePath).absolutePath());
    backupOnce(profilePath);
    QSaveFile f(profilePath);
    if (!f.open(QIODevice::WriteOnly)) {
        out.error = QStringLiteral("无法写入课表档案：%1").arg(f.errorString());
        return out;
    }
    if (f.write(payload) != payload.size()) {
        out.error = QStringLiteral("写入不完整：%1").arg(f.errorString());
        f.cancelWriting();
        return out;
    }
    if (!f.commit()) {
        out.error = QStringLiteral("提交失败：%1").arg(f.errorString());
        return out;
    }
    out.ok = true;
    out.changed = true;
    out.lessonCount = lessonCount;
    qInfo().noquote() << QStringLiteral("[timetable] 云端课表已落地：%1 天 / %2 节 → %3")
                             .arg(out.dayCount).arg(lessonCount).arg(profilePath);
    return out;
}

} // namespace TimetableApply
