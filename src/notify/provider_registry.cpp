// 星集控 · 通知提供方注册表 · 实现（2026-11-01）
// 设计说明与「未知名放行」等口径见 provider_registry.h 顶部。

#include "provider_registry.h"

#include <QSettings>

namespace notify {

namespace {

// QSettings 键（设备级；与管理端下发无关 —— 这是"这台讲台机要不要看这类提示"）
QString kOrderKey()          { return QStringLiteral("notify/providers/order"); }
QString kEnabledKey(const QString &id) { return QStringLiteral("notify/providers/") + id + QStringLiteral("/enabled"); }
QString kChannelKey(const QString &cid) { return QStringLiteral("notify/channels/") + cid; }

// 读写助手：**每次显式 sync**。
// ⚠️ 2026-11-01 实测踩坑：用临时对象 `QSettings().setValue(...)` 时，写入没落地
//    （同一进程内紧接着回读得到 <unset>，探针 --providers 三条断言假红）。
//    显式 sync 之后确定可读回；顺手把"读默认值"也收在一处，避免各处口径不一。
void writeValue(const QString &key, const QVariant &v)
{
    QSettings s;
    s.setValue(key, v);
    s.sync();
}

void removeKey(const QString &key)
{
    QSettings s;
    s.remove(key);
    s.sync();
}

QVariant readValue(const QString &key, const QVariant &dflt)
{
    QSettings s;
    return s.contains(key) ? s.value(key) : dflt;
}

}   // namespace

NotifyProviders &NotifyProviders::instance()
{
    static NotifyProviders s;
    return s;
}

QVector<ProviderDef> NotifyProviders::builtins() const
{
    auto chan = [](const QString &id, const QString &name, const QString &desc) {
        ChannelDef c;
        c.id = id;
        c.name = name;
        c.desc = desc;
        return c;
    };
    auto prov = [](const QString &id, const QString &name, const QString &icon,
                   const QString &desc, QVector<ChannelDef> channels) {
        ProviderDef p;
        p.id = id;
        p.name = name;
        p.icon = icon;
        p.desc = desc;
        p.channels = channels;
        return p;
    };

    // ⚠️ 这里的顺序 = 默认优先级（索引越小越优先），用户 2026-11-01 裁决：
    //    管理端 > 课程 > 对讲 > 文件 > U盘 > 自动化 > 插件 > 考试
    //    「考试」排最后是裁决，不是笔误（见 .h 顶部说明）。
    QVector<ProviderDef> out;
    out << prov(QStringLiteral("admin"), QStringLiteral("管理端"),
                QStringLiteral("bell"),
                QStringLiteral("老师/管理员从管理端下发的通知与广播"),
                { chan(QStringLiteral("admin.notice"), QStringLiteral("通知"),
                       QStringLiteral("普通通知与公告")),
                  chan(QStringLiteral("admin.confirm"), QStringLiteral("需确认的通知"),
                       QStringLiteral("带「确认/快捷回复」按钮的通知")) });

    out << prov(QStringLiteral("class"), QStringLiteral("课程提醒"),
                QStringLiteral("clock"),
                QStringLiteral("课表自己响的上下课提示（不是老师发的）"),
                { chan(QStringLiteral("class.upcoming"), QStringLiteral("准备上课"),
                       QStringLiteral("上课前若干秒提醒，挂到上课为止")),
                  chan(QStringLiteral("class.started"), QStringLiteral("上课"),
                       QStringLiteral("上课那一刻的提示")),
                  chan(QStringLiteral("class.ended"), QStringLiteral("下课"),
                       QStringLiteral("下课那一刻的提示")) });

    out << prov(QStringLiteral("voice"), QStringLiteral("对讲"),
                QStringLiteral("microphone"),
                QStringLiteral("语音对讲与频谱相关的岛提示"),
                { chan(QStringLiteral("voice.talk"), QStringLiteral("对讲中"),
                       QStringLiteral("对讲进行中的状态提示")) });

    out << prov(QStringLiteral("file"), QStringLiteral("文件"),
                QStringLiteral("folder"),
                QStringLiteral("课件收发与传输进度"),
                { chan(QStringLiteral("file.recv"), QStringLiteral("接收进度"),
                       QStringLiteral("正在接收文件时的进度卡")),
                  chan(QStringLiteral("file.done"), QStringLiteral("收发完成"),
                       QStringLiteral("文件收完/发完的提示")) });

    out << prov(QStringLiteral("usb"), QStringLiteral("U盘助手"),
                QStringLiteral("floppy-disk"),
                QStringLiteral("U 盘插入/拔出的询问与提示"),
                { chan(QStringLiteral("usb.insert"), QStringLiteral("插入询问"),
                       QStringLiteral("插盘时问「打开/忽略」")),
                  chan(QStringLiteral("usb.remove"), QStringLiteral("拔出提示"),
                       QStringLiteral("拔盘时的轻提示（默认关）")) });

    out << prov(QStringLiteral("automation"), QStringLiteral("自动化"),
                QStringLiteral("clock"),
                QStringLiteral("自动化规则触发的提醒"),
                { chan(QStringLiteral("automation.rule"), QStringLiteral("规则提醒"),
                       QStringLiteral("规则命中时弹的提示")) });

    out << prov(QStringLiteral("plugin"), QStringLiteral("插件"),
                QStringLiteral("rocket"),
                QStringLiteral("第三方插件经本地接口推的提示"),
                { chan(QStringLiteral("plugin.push"), QStringLiteral("插件推送"),
                       QStringLiteral("带 [插件名] 前缀的推送")) });

    out << prov(QStringLiteral("exam"), QStringLiteral("考试"),
                QStringLiteral("ban"),
                QStringLiteral("考试模式开始/结束的提示"),
                { chan(QStringLiteral("exam.start"), QStringLiteral("考试开始"),
                       QStringLiteral("进入考试模式时的提示")),
                  chan(QStringLiteral("exam.end"), QStringLiteral("考试结束"),
                       QStringLiteral("退出考试模式时的提示")) });

    return out;
}

QStringList NotifyProviders::defaultOrder() const
{
    QStringList ids;
    for (const ProviderDef &p : builtins()) ids << p.id;
    return ids;
}

QStringList NotifyProviders::order() const
{
    const QVector<ProviderDef> all = builtins();
    QStringList known;
    for (const ProviderDef &p : all) known << p.id;

    // 落盘的序 + 兜底：丢掉已不存在的 id，把新增的提供方按内置序补在末尾。
    // （被控端升级新增提供方时，用户改过的序不会被冲掉，新项也不会丢。）
    QStringList out;
    const QStringList saved = readValue(kOrderKey(), QStringList()).toStringList();
    for (const QString &id : saved)
        if (known.contains(id) && !out.contains(id)) out << id;
    for (const QString &id : known)
        if (!out.contains(id)) out << id;
    return out;
}

int NotifyProviders::priorityIndex(const QString &providerId) const
{
    return order().indexOf(providerId);
}

bool NotifyProviders::enabled(const QString &providerId) const
{
    for (const ProviderDef &p : builtins())
        if (p.id == providerId)
            return readValue(kEnabledKey(p.id), p.enabledByDefault).toBool();
    return true;   // 未知名：放行（见 .h「未知名策略」）
}

void NotifyProviders::setEnabled(const QString &providerId, bool on)
{
    for (const ProviderDef &p : builtins()) {
        if (p.id != providerId) continue;
        writeValue(kEnabledKey(p.id), on);
        return;
    }
}

bool NotifyProviders::channelEnabled(const QString &providerId, const QString &channelId) const
{
    for (const ProviderDef &p : builtins()) {
        if (p.id != providerId) continue;
        for (const ChannelDef &c : p.channels) {
            if (c.id != channelId) continue;
            return readValue(kChannelKey(c.id), c.enabledByDefault).toBool();
        }
        return true;   // 该提供方下没有这个渠道：不因为写错渠道名就静默丢掉
    }
    return true;
}

void NotifyProviders::setChannelEnabled(const QString &providerId, const QString &channelId, bool on)
{
    for (const ProviderDef &p : builtins()) {
        if (p.id != providerId) continue;
        for (const ChannelDef &c : p.channels)
            if (c.id == channelId) { writeValue(kChannelKey(c.id), on); return; }
        return;
    }
}

void NotifyProviders::moveUp(const QString &providerId)
{
    QStringList o = order();
    const int i = o.indexOf(providerId);
    if (i <= 0) return;
    o.move(i, i - 1);
    writeValue(kOrderKey(), o);
}

void NotifyProviders::moveDown(const QString &providerId)
{
    QStringList o = order();
    const int i = o.indexOf(providerId);
    if (i < 0 || i >= o.size() - 1) return;
    o.move(i, i + 1);
    writeValue(kOrderKey(), o);
}

void NotifyProviders::resetToDefaults()
{
    removeKey(kOrderKey());
    for (const ProviderDef &p : builtins()) {
        removeKey(kEnabledKey(p.id));
        for (const ChannelDef &c : p.channels) removeKey(kChannelKey(c.id));
    }
}

bool NotifyProviders::allow(const QString &providerId, const QString &channelId) const
{
    if (providerId.isEmpty()) return true;      // 没声明来源 = 老调用方，不拦
    bool known = false;
    for (const ProviderDef &p : builtins()) {
        if (p.id != providerId) continue;
        known = true;
        if (!enabled(providerId)) return false;
        break;
    }
    if (!known) return true;                    // 未知名：放行
    if (!channelId.isEmpty() && !channelEnabled(providerId, channelId)) return false;
    return true;
}

QVariantList NotifyProviders::settingsModel() const
{
    const QVector<ProviderDef> all = builtins();
    const QStringList ord = order();

    // 按"生效次序"排，且把 priority 换成 1 基给界面用（C++ 算好，QML 不做序号运算）。
    QVariantList out;
    for (int idx = 0; idx < ord.size(); ++idx) {
        const QString id = ord.at(idx);
        const ProviderDef *def = nullptr;
        for (const ProviderDef &p : all)
            if (p.id == id) { def = &p; break; }
        if (!def) continue;

        QVariantList chans;
        for (const ChannelDef &c : def->channels) {
            QVariantMap cm;
            cm.insert(QStringLiteral("id"), c.id);
            cm.insert(QStringLiteral("name"), c.name);
            cm.insert(QStringLiteral("desc"), c.desc);
            cm.insert(QStringLiteral("enabled"), channelEnabled(def->id, c.id));
            chans << cm;
        }

        QVariantMap m;
        m.insert(QStringLiteral("id"), def->id);
        m.insert(QStringLiteral("name"), def->name);
        m.insert(QStringLiteral("icon"), def->icon);
        m.insert(QStringLiteral("desc"), def->desc);
        m.insert(QStringLiteral("enabled"), enabled(def->id));
        m.insert(QStringLiteral("priority"), idx + 1);           // 1 基
        m.insert(QStringLiteral("canUp"), idx > 0);
        m.insert(QStringLiteral("canDown"), idx < ord.size() - 1);
        // 副标题由 C++ 拼好 —— QML 侧禁字符串拼接/序号运算（§10.9 纯图形铁律）。
        m.insert(QStringLiteral("subtitle"),
                 QStringLiteral("优先级 %1 · %2").arg(idx + 1).arg(def->desc));
        m.insert(QStringLiteral("channels"), chans);
        out << m;
    }
    return out;
}

}   // namespace notify
