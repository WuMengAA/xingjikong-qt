#pragma once
// 星集控 · 通知提供方注册表（2026-11-01 · 灵动岛顶替 ClassIsland 第 2 步）
//
// ── 为什么需要它 ────────────────────────────────────────────────────────
// 此前「这条通知是谁发的」只有一个 `kind` 字符串（popup/island/fullscreen/banner/
// danmaku/loop）和一个 `from`（来源名，仅用于黑名单）。同一个 kind 既可能是管理端
// 发的、也可能是课表自己响的、还能是插件推的 —— 老师想"只关掉插件那堆提示"时
// 无处可关。注册表把「来源」变成一等公民：**提供方 → 渠道** 两级，每级可单独开关、
// 可排优先级。
//
// ── 与 ClassIsland 的关系（重要）────────────────────────────────────────
// 机制参照其「提醒提供方 / 提醒渠道」两级模型（见 docs/ClassIsland源码学习-…），
// **代码为自有实现，未复制其任何代码**（对方 GPL-3.0）。差异：
//   · 我们不注册"设置控件类型"，开关/次序统一由本表 + QSettings 承载；
//   · 不做特性反射自注册，内置清单写死在本文件（插件提供方的开关走同一张表）。
//
// ── 优先级方向 ─────────────────────────────────────────────────────────
// `order()` 的**索引越小 = 越靠前 = 越先抢到岛**。默认序（用户 2026-11-01 裁决）：
//   管理端 > 课程 > 对讲 > 文件 > U盘 > 自动化 > 插件 > 考试
// ⚠️ 「考试」排在最后是**用户明确裁决**，不是笔误：考试期间的静默由考试模式自己管
//    （模块里的 quietInExam），不靠优先级抢屏；这一条别"顺手改成第一"。
//
// ── 未知名策略 ─────────────────────────────────────────────────────────
// 查询一个**不在表里**的 providerId 一律**放行**（返回 true）。理由：老版本云端/插件
// 发来的通知不该因为被控端升级而静默丢掉 —— 宁可多弹一条，也不要"老师发了没人看见"。

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace notify {

struct ChannelDef {
    QString id;                 // 稳定标识，落 QSettings 用（如 "class.started"）
    QString name;               // 显示名
    QString desc;               // 一行说明（设置页灰字）
    bool    enabledByDefault = true;
};

struct ProviderDef {
    QString id;                 // "admin" / "class" / ...
    QString name;               // 管理端 / 课程提醒 ...
    QString icon;               // lucide 图标名（resources/icons 里实存）
    QString desc;
    bool    enabledByDefault = true;
    QVector<ChannelDef> channels;
};

class NotifyProviders {
public:
    static NotifyProviders &instance();

    // 内置清单；**顺序即默认优先级**（决定 order() 的兜底次序）
    QVector<ProviderDef> builtins() const;

    // 生效的优先级次序（用户改过的序在前；新增的提供方按内置序补在末尾）
    QStringList order() const;

    // 启用状态（未落盘的键 → 用内置默认值）
    bool enabled(const QString &providerId) const;
    void setEnabled(const QString &providerId, bool on);
    bool channelEnabled(const QString &providerId, const QString &channelId) const;
    void setChannelEnabled(const QString &providerId, const QString &channelId, bool on);

    // 优先级调整（上移 = 更优先）。越界无操作；未知 id 无操作。
    void moveUp(const QString &providerId);
    void moveDown(const QString &providerId);
    void resetToDefaults();

    // 门控入口：这条通知现在允许上岛吗？
    //   · provider 未知名 → true（见文件头「未知名策略」）
    //   · provider 被关 → false
    //   · 给了 channelId 且该渠道被关 → false
    bool allow(const QString &providerId, const QString &channelId = QString()) const;

    // 给 QML 的一行一提供方的结果值（C++ 算好，QML 只画）：
    //   { id, name, icon, desc, enabled, priority(1 基), canUp, canDown,
    //     statusText, channels: [{ id, name, desc, enabled }] }
    QVariantList settingsModel() const;

    // 优先级序号（0 基；未知 id 返回 -1）
    int priorityIndex(const QString &providerId) const;

private:
    NotifyProviders() = default;
    QStringList defaultOrder() const;   // 内置顺序的 id 列表
};

}   // namespace notify
