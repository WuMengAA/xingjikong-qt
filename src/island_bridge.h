// 星集控被控端 · 灵动岛 QML 桥（2026-10-11）
//
// 目的：把 IslandOverlay 的**状态**暴露给 QML，让岛的外观交给 qml/island/IslandRoot.qml 画。
//
// ── 边界（重要）────────────────────────────────────────────────
// · 本类**不是**公开 API：只在 island.h 里前置声明 + private 成员指针，
//   main.cpp 一行都不用改（它只认 IslandOverlay 的公开方法）。
// · 逻辑（几何/动画/定时器/鼠标/生命周期）**全部留在 IslandOverlay**，
//   本类只做「读成员 → Q_PROPERTY」和「QML 回调 → 调 IslandOverlay 方法」。
// · 这不是"把岛搬到 QML"：是把**绘制**搬过去。#935 行里那些注释写满踩坑记录的
//   状态机（m_closing 淡出守卫 / kDragSlop 方向锁 / explorer /select）一个字都不动。
//
// ── 数据流 ─────────────────────────────────────────────────────
//   C++ 改状态 → IslandOverlay 调 bridge->sync() → NOTIFY 全发 → QML 重绘
//   QML 点按钮 → bridge.fireAction(label) → IslandOverlay 的 m_onAction 回调
//   QML 手势   → bridge.requestExpand(on) → IslandOverlay::setExpanded(on)
//
// ⚠️ sync() 用「一次性全 NOTIFY」而不是逐字段判重：岛的状态字段少（~10 个），
//    而判重逻辑一旦漏一个就会出现"改了不刷新"这种最难查的 bug。全发最多多几次
//    QML 绑定求值，代价可控。

#ifndef STELARITH_ISLAND_BRIDGE_H
#define STELARITH_ISLAND_BRIDGE_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class IslandOverlay;

class IslandBridge : public QObject
{
    Q_OBJECT

    // ── 内容 ────────────────────────────────────────────────
    Q_PROPERTY(QString title    READ title    NOTIFY changed)
    Q_PROPERTY(QString desc     READ desc     NOTIFY changed)
    Q_PROPERTY(QString icon     READ icon     NOTIFY changed)
    // ── 形态 ────────────────────────────────────────────────
    // "capsule" | "centered" | "fullscreen"（对齐 IslandOverlay::Form）
    Q_PROPERTY(QString form     READ form     NOTIFY changed)
    // "" | "urgent" | "inform" | "remind"（仅全屏生效，白名单与旧 NotifyWindow 同）
    Q_PROPERTY(QString severity READ severity NOTIFY changed)
    // ── 主题 / 状态 ─────────────────────────────────────────
    Q_PROPERTY(bool light       READ light       NOTIFY changed)
    Q_PROPERTY(bool idle        READ idle        NOTIFY changed)
    Q_PROPERTY(bool expanded    READ expanded    NOTIFY changed)
    Q_PROPERTY(bool interactive READ interactive NOTIFY changed)
    // 非空 = 这条可点击打开所在文件夹（explorer /select）
    Q_PROPERTY(bool canOpen     READ canOpen     NOTIFY changed)
    // ── 交互 ────────────────────────────────────────────────
    // [{ label: "确认", role: "primary"|"secondary" }, ...]
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    // 多状态聚合（清单 #2）。被控端当前是"一条内容"模型（showIsland 顶掉旧的），
    // 这个列表暂时由 IslandOverlay 组装成单元素；将来接任务中心时可直接扩多元素。
    // [{ k: "exam", t: "标题", d: "描述", p: -1 }, ...]
    Q_PROPERTY(QVariantList states  READ states  NOTIFY changed)

public:
    explicit IslandBridge(IslandOverlay *host, QObject *parent = nullptr);

    QString title() const;
    QString desc() const;
    QString icon() const;
    QString form() const;
    QString severity() const;
    bool light() const;
    bool idle() const;
    bool expanded() const;
    bool interactive() const;
    bool canOpen() const;
    QVariantList actions() const;
    QVariantList states() const;

    // 状态变了就调这个：一把全 NOTIFY。IslandOverlay 在每个改状态的点收口调用。
    void sync();

    // QML → C++：点了某枚动作按钮（文案）。
    // ⚠️ 复用 IslandOverlay 的既有语义：回调一次 → 清按钮 → 取消交互 → dismiss。
    //    不能只在 QML 里改个属性了事，否则 m_actions 还留在 C++ 里，下次上屏会带出来。
    Q_INVOKABLE void fireAction(const QString &label);

    // QML → C++：手势请求展开/收起。
    Q_INVOKABLE void requestExpand(bool on);

private:
    IslandOverlay *m_host = nullptr;   // 不持有（IslandOverlay 是单例，活得比本对象久）

signals:
    void changed();
};

#endif // STELARITH_ISLAND_BRIDGE_H
