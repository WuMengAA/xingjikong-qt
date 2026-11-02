// 课表下发消息的落地接线（2026-11-02）
#include "timetable_apply_wire.h"

#include <QDebug>
#include <QJsonObject>
#include <QString>

#include "profile_repository.h"
#include "schedule_clock.h"
#include "timetable_apply.h"

namespace TimetableApplyWire {
namespace {

Deps g_deps{nullptr, nullptr, nullptr};

}  // namespace

void init(const Deps &deps)
{
    g_deps = deps;
}

bool handle(const QJsonObject &payload)
{
    // 协议：{ type:"timetable-apply", profile:{...normalizeClassIslandProfile 的信封...} }
    const QJsonObject env = payload.value(QStringLiteral("profile")).toObject();

    ScheduleClock *clock = g_deps.clock ? g_deps.clock() : nullptr;
    const QString path = clock ? clock->profilePath()
                               : ProfileRepository::defaultProfilePath();
    const QString code = g_deps.classCode ? g_deps.classCode() : QString();

    const TimetableApply::Result r = TimetableApply::applyEnvelope(env, path, code);
    if (!r.ok) {
        qWarning().noquote() << QStringLiteral("[agent-qt] 课表下发失败：%1").arg(r.error);
    } else if (r.changed && clock) {
        // changed=false 时**不重载**：内容一样还重载会让岛上「今天上什么」闪一下重排。
        clock->reloadProfile();
    }

    // 回执必须发：管理端要能看到「这台到底收没收到」，否则下发永远是「发出去了」。
    if (g_deps.reply) {
        QJsonObject rc;
        rc.insert(QStringLiteral("ok"), r.ok);
        if (!r.error.isEmpty()) rc.insert(QStringLiteral("error"), r.error);
        rc.insert(QStringLiteral("changed"), r.changed);
        rc.insert(QStringLiteral("days"), r.dayCount);
        rc.insert(QStringLiteral("lessons"), r.lessonCount);
        if (!code.isEmpty()) rc.insert(QStringLiteral("classCode"), code);
        g_deps.reply(QStringLiteral("timetable-applied"), rc);
    }
    return true;
}

}  // namespace TimetableApplyWire
