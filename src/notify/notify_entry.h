#pragma once
#include <QString>
#include <QJsonObject>
#include <QSystemTrayIcon>

#include "../island/island.h"   // IslandOverlay（Form 枚举 / instance()）

// 灵动岛统一入口（2026-10-07 · #98）：被控端一切通知统一走 IslandOverlay。
// 2026-10-30 main 拆分 #7：islandShow / islandShowAt / trayNotify / notifyFromParams
// 从 main.cpp（原 3541-3808）迁出。状态全局（g_tray / g_remoteControl / g_notifGate /
// g_loopReminder / sendActionReceipt）仍驻 main（文件作用域外部链接），本模块 extern 复用。
// ⚠️ 函数名与原 main static 一致 ⇒ 调用点不改名；默认参数放声明处（定义处不带，避免 C2572）。

// 轻量提示统一走灵动岛（顶部胶囊/居中卡片/全屏，见 island.h 的 Form）。
void islandShow(const QString &title, const QString &desc = QString(),
                const QString &icon = QString(), int ms = 5000, bool interactive = false,
                const QString &openPath = QString(),
                IslandOverlay::Form form = IslandOverlay::Form::Capsule,
                bool fromAdmin = false);

// 同上的「带发生时刻」版本：插件推的通知用调用那一刻的 ts，而非 agent 处理那一刻。
void islandShowAt(const QString &title, const QString &desc, const QString &icon,
                  int ms, IslandOverlay::Form form, bool fromAdmin, qint64 noticeAtMs);

// 托盘气泡兜底：结果型事件（如文件落到本机）再往 Windows 通知中心推一条。
void trayNotify(const QString &title, const QString &body,
                QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information,
                int ms = 8000);

// 解析 params（kind/title/content/seconds/notice_id/flags）并弹窗。
// 返回 "" = 成功；非空 = 错误（"skipped:" 前缀 = 门控静默丢弃）。
QString notifyFromParams(const QJsonObject &params, const QString &id = QString());

// 考试模式开始/结束的岛提示（2026-11-01 · 顶替 ClassIsland 第 2 步）。
// 归属「考试」提供方（provider=exam / channel exam.start|exam.end）——关掉后不弹。
// 为什么放在这里：考试模式本身不弹任何东西，没有这条「考试」提供方就没有可门控的内容
//   （摆一个点了没反应的开关比不摆更糟）。
void notifyExamMode(bool entering, const QString &detail);