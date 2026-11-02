#include "plugin_api_bridge.h"
#include "pluginhost.h"
#include "plugin_action_whitelist.h"
#include "../island/island.h"
#include "../notify/notify_entry.h"
#include "../schedule/schedule_event_bus.h"
#include "../schedule/schedule_clock.h"
#include "../core/AgentSettings.h"

namespace {

// ⚠️ PluginAgentApi 的字段是**裸函数指针**（不是 std::function），不能捕获。
// 所以课表时钟走一个文件作用域指针带进去。只在 build 时赋值一次；
// 之后 main 若换掉时钟（当前不会），要重新调用 build 才会生效。
ScheduleClock *s_clock = nullptr;

// 插件给的 form 可能是 7、-1 之类（JSON 里没有枚举）。越界一律回落胶囊：
// 让 std::function 里出现非法枚举值，比"形态不对"难查得多——那会在 switch
// 的 default 分支里静默画成空白，界面上只是"岛不见了"。
IslandOverlay::Form sanitizeForm(int form)
{
    return (form >= int(IslandOverlay::Form::Capsule) && form <= int(IslandOverlay::Form::Mini))
               ? static_cast<IslandOverlay::Form>(form)
               : IslandOverlay::Form::Capsule;
}

} // namespace

PluginAgentApi pluginApiBridgeBuild(ScheduleClock *clock)
{
    PluginAgentApi api;
    api.islandShow = [](const QString &title, const QString &desc, const QString &icon,
                        int form, bool fromAdmin, int durationMs) {
        islandShow(title, desc, icon, durationMs, false, QString(),
                   sanitizeForm(form), fromAdmin);
    };
    api.notify = [](const QJsonObject &params) { notifyFromParams(params); };
    // P0-0（2026-10-17）：插件推的通知用「插件调用那一刻」当发生时刻（见 islandShowAt）。
    // 传进来的 title 已带「[插件名] 」前缀、长度与频率已由 PluginHost::handleIslandShow 治理过；
    // fromAdmin 恒 false —— 插件不是管理端，历史排序不能被它污染。
    api.islandShowAt = [](const QString &title, const QString &desc, const QString &icon,
                          int form, bool fromAdmin, int durationMs, qint64 noticeAtMs) {
        islandShowAt(title, desc, icon, durationMs, sanitizeForm(form), fromAdmin, noticeAtMs);
    };
    // 第三方推岛开关（设置页）：pluginhost 每次推岛前问一次，关掉即静默丢弃并计数。
    api.senderPriorityLimit = []() { return AgentSettings::pluginIslandPush(); };
    // 沙箱验证：插件回包里带岛的状态快照（只读）。同机并存生产实例时，截图分不清是哪个进程的岛。
    api.islandSnapshot = []() { return IslandOverlay::instance()->historySnapshotJson(); };
    api.settingsGet = [](const QString &key) -> QJsonValue {
        if (key == QStringLiteral("noiseEnabled"))   return AgentSettings::noiseEnabled();
        if (key == QStringLiteral("voiceEnabled"))   return AgentSettings::voiceEnabled();
        if (key == QStringLiteral("notifyEnabled"))  return AgentSettings::notifyEnabled();
        if (key == QStringLiteral("islandIdle"))     return AgentSettings::islandIdle();
        if (key == QStringLiteral("lightMode"))      return AgentSettings::lightMode();
        return QJsonValue();
    };
    api.settingsSet = [](const QString &key, const QJsonValue &v) {
        const bool on = v.toBool();
        if (key == QStringLiteral("noiseEnabled"))       AgentSettings::setNoiseEnabled(on);
        else if (key == QStringLiteral("voiceEnabled"))  AgentSettings::setVoiceEnabled(on);
        else if (key == QStringLiteral("notifyEnabled")) AgentSettings::setNotifyEnabled(on);
        else if (key == QStringLiteral("islandIdle"))    AgentSettings::setIslandIdle(on);
        else if (key == QStringLiteral("lightMode"))     AgentSettings::setLightMode(on);
    };
    api.subscribeEvents = [](const QString &, const QStringList &) { /* 订阅登记在宿主侧，事件经 forwardEvent 推送 */ };
    // 2026-11-02（插件大升级 #2）：课表查询 —— 中途装上的插件靠它问"现在上到哪"。
    // clock 为 nullptr 时 buildSnapshot 回 available=false，与"今天没课"（lessons=[]）可区分。
    s_clock = clock;
    api.scheduleGet = []() -> QJsonValue {
        return QJsonValue(ScheduleEventBus::buildSnapshot(s_clock));
    };
    // 2026-11-02（插件大升级 #3）：cmd:exec —— 此前只有权限词、零实现。
    api.runAction = [](const QString &action, const QJsonObject &params) -> PluginResult {
        return PluginActionWhitelist::run(action, params);
    };
    return api;
}
