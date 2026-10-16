// 星集控 · 内置模块：U盘助手（2026-10-16 · 用户点单「U盘插件：灵动岛弹窗+询问是否打开」）
//
// 做什么：U 盘（可移动磁盘）插入 → 灵动岛居中弹一条**带按钮**的提示（打开 / 忽略）；
//         点「打开」→ 打开该盘**根目录**（explorer，只开文件夹，绝不自动运行盘上任何东西）。
//
// 为什么要做成**内置模块**而不是进程外插件：检测靠 Windows 原生窗口消息
//   WM_DEVICECHANGE（DBT_DEVICEARRIVAL / DBT_DEVICEREMOVECOMPLETE），
//   这条消息只投到 agent 自己的窗口/事件循环 —— 独立进程插件收不到。
//
// 安全口径（与岛既有的 setOpenPath 同款，不许突破）：
//   · 只开**文件夹**，绝不 ShellExecute 打开盘上任何文件 / 绝不自动运行 autorun.inf
//     （学生机上的 U 盘内容不可信，"替系统决定运行什么"是事故级风险）。
//   · 考试模式下默认静默（quietInExam）：上课/考试时不弹、不打扰。
//
// ⚠️ AUTOMOC：本头被 usbmodule.cpp 与 main.cpp include ⇒ 会单独编译成 TU，
//    所以头里不引用 main.cpp 任何符号；Windows 原生类型全部留在 .cpp（那里才 include
//    windows.h / dbt.h），头只留 Qt 类型 —— 免得 windows.h 的宏污染每个 TU。

#pragma once

#include "modulehost.h"

#include <QAbstractNativeEventFilter>
#include <QSet>
#include <QString>
#include <QStringList>

class UsbModule : public AgentModule, public QAbstractNativeEventFilter
{
public:
    explicit UsbModule(QObject *parent = nullptr) : AgentModule(parent) {}

    ModuleManifest manifest() const override;
    void start() override;
    void stop() override;
    bool execAction(const QString &action, const QJsonObject &params, ModuleResult &out) override;

    /** Windows 原生事件：WM_DEVICECHANGE（U盘插拔）。不认领的消息一律返回 false。 */
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    /** 当前已识别的可移动盘（"E:" 形态，已排序）。execAction("usb_drives") 与弹窗共用。 */
    QStringList removableDrives() const;

private:
    void onDriveArrived(const QString &drive);     // 插入（延后 600ms 判类型再处理）
    void onDriveRemoved(const QString &drive);     // 拔出
    bool setting(const QString &key, bool dflt) const;

    QSet<QString> m_known;      // 已识别到的可移动盘（去重 + 拔出时清理）
};
