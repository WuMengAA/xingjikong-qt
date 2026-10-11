// Android 系统通知栏（2026-10-11）— 见 android_notify.cpp。
#pragma once

#include <QString>

/** 在 Android 系统通知栏弹一条横幅；桌面为空实现（走灵动岛/托盘）。
 *  @param title 通知标题
 *  @param text  正文 */
void androidSystemNotify(const QString &title, const QString &text);