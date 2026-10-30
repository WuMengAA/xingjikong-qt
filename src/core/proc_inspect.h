#pragma once

#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include <windows.h>   // HWND / HANDLE / LPARAM / BOOL / CALLBACK

/* 进程 / 可见窗口只读检查（main 拆分 #13）
 *
 * snapshotProcessNames / enumWindowsCb / visibleWindowsCached / windowTitleOf / memMbOf
 * 从 main.cpp 匿名域迁出。纯 Win32 只读快照，**零 main 全局依赖**；
 * execute_action.cpp 经 g_actionDeps 函数指针访问（无需改动），
 * main.cpp 的 ActionDeps 填充取地址即用（函数名不变）。
 *
 * WinCache 完整定义在 action/execute_action.h（ActionDeps 共享类型），此处前向声明即可。
 */
struct WinCache;

QHash<quint32, QString> snapshotProcessNames();
BOOL CALLBACK enumWindowsCb(HWND hwnd, LPARAM lp);
const WinCache &visibleWindowsCached();
QString windowTitleOf(HWND hwnd);
int memMbOf(quint32 pid);
