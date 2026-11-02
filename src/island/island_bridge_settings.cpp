// 灵动岛桥 · 「设」面方法（2026-10-27 · C 批）
//
// 为什么单独一个 .cpp：island_bridge.cpp 已逼近 600 行红线（ppt_domain 当年就是撞这条
// 线被拆的），而 C 批的「设置」方法（系统音量 / 通知音量 / 黑名单）又是一整组。
// 把它们挪到这里，桥本体只留公共状态 + 批注动作，互不挤占。
//
// 类声明仍在 island_bridge.h（同一个 IslandBridge 类，C++ 允许方法分散在多个 TU 定义），
// 这里只放实现。读侧同样不缓存（每次从设备/ QSettings 现读），与桥其它 getter 口径一致。

#include "island_bridge.h"
#include "island.h"
// 系统主音量（Core Audio / WASAPI）
#include "../media/system_volume.h"
// 通知音量 / 黑名单（设备级 QSettings，与管理端同一份）
#include "../core/AgentSettings.h"

int IslandBridge::systemVolume() const
{
    int v = -1;
    QString err;
    if (getSystemMasterVolume(&v, &err))
        return v;
    qWarning("[island] 读系统主音量失败：%s", qPrintable(err));
    return -1;
}

void IslandBridge::setSystemVolume(int v)
{
    const int clamped = qBound(0, v, 100);
    QString err;
    if (!setSystemMasterVolume(clamped, &err)) {
        qWarning("[island] 设置系统音量 %d%% 失败：%s", clamped, qPrintable(err));
        return;   // 失败不 sync（保持原值，UI 下次打开会重读真实值）
    }
    sync();
}

int IslandBridge::notifyVolume() const
{
    return AgentSettings::notifyVolume();
}

void IslandBridge::setNotifyVolume(int v)
{
    AgentSettings::setNotifyVolume(v);
    sync();
}

QVariantList IslandBridge::notifyBlacklist() const
{
    QVariantList out;
    for (const QString &s : AgentSettings::notifyBlacklist())
        out.append(s);
    return out;
}

void IslandBridge::settingsAction(const QString &cmd)
{
    const QString c = cmd.trimmed();
    if (c.startsWith(QLatin1String("sysvol:"))) {
        bool ok = false;
        const int v = c.mid(7).toInt(&ok);
        if (ok) setSystemVolume(v);
    } else if (c.startsWith(QLatin1String("notifyvol:"))) {
        bool ok = false;
        const int v = c.mid(10).toInt(&ok);
        if (ok) setNotifyVolume(v);
    } else if (c.startsWith(QLatin1String("blacklist:add:"))) {
        const QString who = c.mid(13).trimmed();
        if (!who.isEmpty()) {
            QStringList bl = AgentSettings::notifyBlacklist();
            if (!bl.contains(who)) {
                bl.append(who);
                AgentSettings::setNotifyBlacklist(bl);
            }
            sync();
        }
    } else if (c.startsWith(QLatin1String("blacklist:remove:"))) {
        const QString who = c.mid(16).trimmed();
        QStringList bl = AgentSettings::notifyBlacklist();
        if (bl.removeAll(who) > 0)
            AgentSettings::setNotifyBlacklist(bl);
        sync();
    } else if (c == QLatin1String("blacklist:clear")) {
        AgentSettings::setNotifyBlacklist(QStringList());
        sync();
    } else {
        qWarning("[island] 不认识的 settings 动作：%s", qPrintable(c));
    }
}
