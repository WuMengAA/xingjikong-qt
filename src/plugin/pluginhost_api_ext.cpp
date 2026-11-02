// 星集控被控端 · 插件宿主「能力扩展」段（2026-11-02）
// ───────────────────────────────────────────────────────────────────────────
// 从 pluginhost.cpp 抽出的两段 api 实现：
//   schedule_get  课表查询（需 events:schedule）
//   run_action    受限指令执行（需 cmd:exec）
//
// 为什么单独一个 TU：宿主主文件撞 600 行红线；而且这两段与 P0-0「第三方推岛治理」
// 是两条互不相干的线 —— 治理那条改一次要看头注释 + 令牌桶 + 审计计数三处上下文，
// 塞在一起会让人以为改课表查询也得懂限流。
//
// 宿主只做**权限闸**：谁能调、缺哪个权限，在这里判。
// "哪些指令可以给插件"是产品决策，判在注入方（PluginActionWhitelist）。
#include "pluginhost.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>

void PluginHost::handleApiScheduleGet(Conn *c, QJsonObject &r)
{
    // 只读，但仍要 events:schedule 权限 —— 课表本身是可识别信息（哪个班上什么课），
    // 不该让只申请了 island 的插件白拿。
    if (!c->granted.contains(QStringLiteral("events:schedule"))) {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), QStringLiteral("权限不足：events:schedule"));
        return;
    }
    if (!m_api.scheduleGet) {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), QStringLiteral("课表能力未注入（宿主未配置课表源）"));
        return;
    }
    r.insert(QStringLiteral("ok"), true);
    r.insert(QStringLiteral("value"), m_api.scheduleGet());
}

void PluginHost::handleApiRunAction(Conn *c, const QJsonObject &args, QJsonObject &r)
{
    if (!c->granted.contains(QStringLiteral("cmd:exec"))) {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), QStringLiteral("权限不足：cmd:exec"));
        return;
    }
    const QString act = args.value(QStringLiteral("action")).toString().trimmed();
    if (act.isEmpty()) {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), QStringLiteral("缺少 action 参数"));
        return;
    }
    if (!m_api.runAction) {
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("error"), QStringLiteral("指令通道未注入"));
        return;
    }
    const PluginResult pr = m_api.runAction(act, args.value(QStringLiteral("params")).toObject());
    // 注意口径：白名单拒绝时 result="denied" 且 error 非空 ⇒ ok=false。
    // 若把"白名单拒绝"也回 ok=true，插件作者会以为指令跑过了。
    r.insert(QStringLiteral("ok"), pr.error.isEmpty());
    if (!pr.error.isEmpty()) r.insert(QStringLiteral("error"), pr.error);
    r.insert(QStringLiteral("result"), pr.result);
    if (!pr.data.isEmpty()) r.insert(QStringLiteral("data"), pr.data);
}
