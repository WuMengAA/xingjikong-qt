#pragma once
// 通知门控 / 考试模式禁通知与监视（2026-10-08 · 通知体系升级 §6.2 独立模块）
//
// 背景（用户 2026-10-08 拍板）：考试模式激活期间，**一切**通知/提醒/弹幕/灵动岛/
// 全屏/截图/摄像头必须静默 —— 不弹、不抓、不传。
//
// 本组件只负责「决定放行还是拦截」，不负责「考试模式怎么开/关/写白名单」——
// 那由既有的 exam_mode 状态机管。main.cpp 在调用任何通知/采集前先问它一声：
//   if (!NotificationGate::allow(kNotif)) return;   // 考试中 → 静默丢弃
//
// 独立 QObject：无 UI、无网络依赖，可单测。
//   · allow(kind) 恒为"考试激活 → 拒绝所有"；非考试 → 全放行
//   · 提供 examActivated()/examEnded() 供上层在 exam 状态切换时通知本门控
//     （即使上层忘了同步，也提供 isExamActive 读取侧兜底）
//   · 通知种类枚举对齐通知体系（island/danmaku/fullscreen/confirm/loop/snapshot/camera/audio）
#include <QObject>
#include <QString>

class NotificationGate : public QObject {
    Q_OBJECT
public:
    enum Kind {
        Island = 0,      // 灵动岛胶囊
        Danmaku,         // 弹幕
        Fullscreen,      // 全屏
        NeedConfirm,     // 需确认
        LoopRemind,      // 循环提醒
        Snapshot,        // 截图（相机抓拍）
        Camera,          // 摄像头采集/录制
        AudioCapture,    // 麦克风拾音（噪音检测/对讲）
        Count
    };
    Q_ENUM(Kind)

    explicit NotificationGate(QObject *parent = nullptr);

    // 考试激活期间：一切种类 → 拒绝；否则放行。
    // main.cpp 在每个通知/采集动作前调用，拒绝时静默返回（不弹、不记 error）。
    bool allow(Kind k) const { return !m_examActive; }

    // 由上层在考试状态切换点调用（exam_mode 开始/结束）。
    void setExamActive(bool on);

    bool examActive() const { return m_examActive; }

    static QString kindName(Kind k);

signals:
    // 考试状态变化（供 UI/其它模块感知）
    void examStateChanged(bool active);

private:
    bool m_examActive = false;
};