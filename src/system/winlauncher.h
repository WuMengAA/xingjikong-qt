#pragma once
// 星集控 · Windows 子进程静默启动工具（2026-10-19 · 需求①：杜绝任何场景弹出 cmd 窗口）
//
// 背景：QProcess 启动 cmd.exe / powershell.exe 这类**控制台子系统**程序时，Windows 会为
// 其分配一个可见的控制台窗口（黑窗一闪而过）。在自更新 / OTA 回滚 / OTA 重启助手 / 插件
// 解压 / 建桌面快捷方式 等场景都出现，严重干扰用户体验。
//
// 修复：两个 helper，统一用 CREATE_NO_WINDOW（连隐藏的控制台都不分配，比 SW_HIDE 干净）。
//   · stelarithHideChildConsole(p)       —— 进程内 QProcess（tar / powershell 解压、插件解压）：
//       start() 前调一次，给 CreateProcess 挂 CREATE_NO_WINDOW。
//   · stelarithStartHiddenDetached(...)  —— detached 场景（cmd /c 脚本、powershell 建快捷方式）：
//       QProcess::startDetached 无法挂 modifier，故直接用 CreateProcessW(..., CREATE_NO_WINDOW)
//       自拉起；子进程独立于父进程存活（父退出后脚本继续跑，满足自更新/回滚的"让位"需求）。
//
// 非 Windows（Android）下：前者 no-op（本来就没控制台），后者退化成 QProcess::startDetached，
// 行为与原代码等价。

#include <QProcess>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <string>
#  include <vector>
#endif

// 给进程内 QProcess 挂上"无控制台窗口"标志（start() 之前调用一次）。
inline void stelarithHideChildConsole(QProcess &p)
{
#ifdef Q_OS_WIN
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#else
    Q_UNUSED(p);
#endif
}

// detached 静默启动：返回是否成功拉起。子进程独立于本进程存活。
// program 走 PATH 解析；args 的命令行转义与加引号规则对齐 QProcess 原有行为
//（含空格/制表符/双引号的参数整体加外层双引号，不转义内部双引号）。
#ifdef Q_OS_WIN
inline bool stelarithStartHiddenDetached(const QString &program,
                                         const QStringList &args,
                                         const QString &workingDir = QString())
{
    QStringList all;
    all.append(program);
    all.append(args);
    QString cmd;
    for (const QString &a : all) {
        const bool needQuote = a.contains(QLatin1Char(' ')) || a.contains(QLatin1Char('\t'))
                               || a.contains(QLatin1Char('"'));
        if (needQuote) cmd += QLatin1Char('"');
        cmd += a;
        if (needQuote) cmd += QLatin1Char('"');
        cmd += QLatin1Char(' ');
    }
    if (cmd.endsWith(QLatin1Char(' '))) cmd.chop(1);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    const std::wstring cmdW = cmd.toStdWString();
    std::vector<wchar_t> buf(cmdW.begin(), cmdW.end());
    buf.push_back(L'\0');
    const std::wstring dirW = workingDir.toStdWString();
    const BOOL rc = CreateProcessW(
        nullptr, buf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr,
        workingDir.isEmpty() ? nullptr : dirW.c_str(),
        &si, &pi);
    if (rc) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    return false;
}
#else
inline bool stelarithStartHiddenDetached(const QString &program,
                                         const QStringList &args,
                                         const QString &workingDir = QString())
{
    return QProcess::startDetached(program, args, workingDir);
}
#endif
