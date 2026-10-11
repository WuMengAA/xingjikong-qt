// 小鲸鱼音效播放器实现（2026-10-11）
// 详见 whale_audio.h 的设计说明。

#include "whale_audio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QResource>
#include <QStandardPaths>
#include <QSysInfo>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <mmsystem.h>
#endif

WhaleAudio *WhaleAudio::instance()
{
    static WhaleAudio *inst = nullptr;
    if (!inst) inst = new WhaleAudio();
    return inst;
}

WhaleAudio::WhaleAudio(QObject *parent)
    : QObject(parent)
{
}

QString WhaleAudio::qrcPath(Sound s) const
{
    switch (s) {
    case Sound::PressYa:   return QStringLiteral(":/whale/Ya1.wav");
    case Sound::ReleaseYa: return QStringLiteral(":/whale/Ya2.wav");
    case Sound::PressD:    return QStringLiteral(":/whale/D1.wav");
    case Sound::ReleaseD:  return QStringLiteral(":/whale/D2.wav");
    case Sound::TaskEndA:  return QStringLiteral(":/whale/minecraft-exp-orb.wav");
    case Sound::TaskEndB:  return QStringLiteral(":/whale/task-end-a.wav");
    case Sound::Notify:    return QStringLiteral(":/whale/minecraft-exp-orb.wav");
    }
    return {};
}

QString WhaleAudio::extractToTemp(const QString &qrcPathStr) const
{
    // PlaySoundW 不认 qrc 路径。从 QResource 读字节、写到固定临时文件。
    // 文件名固定 = whale_<basename>.wav，每次覆盖，不累积。
    const QFileInfo fi(qrcPathStr);
    const QString baseName = fi.fileName();
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempDir.isEmpty()) return {};
    QDir().mkpath(tempDir);
    const QString outPath = tempDir + QStringLiteral("/stelarith_whale_") + baseName;

    QFile out(outPath);
    if (out.exists() && out.size() > 0) {
        // 已存在且非空 → 假定内容没变（qrc 是编译期资产），直接用
        return outPath;
    }
    if (!out.open(QIODevice::WriteOnly)) return {};

    QResource res(qrcPathStr);
    if (!res.isValid()) { out.close(); out.remove(); return {}; }
    out.write(reinterpret_cast<const char *>(res.data()), res.size());
    out.close();
    return outPath;
}

void WhaleAudio::play(Sound s, qreal volume)
{
    Q_UNUSED(volume)  // winmm 不支持音量

#ifdef Q_OS_WIN
    const QString qrc = qrcPath(s);
    const QString tmp = extractToTemp(qrc);
    if (tmp.isEmpty()) return;

    // PlaySoundW：SND_ASYNC 不阻塞、SND_NODEFAULT 无文件不 beep
    PlaySoundW(reinterpret_cast<LPCWSTR>(tmp.utf16()),
               nullptr,
               SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#else
    Q_UNUSED(s)
#endif
}

void WhaleAudio::playByName(const QString &name, qreal volume)
{
    const QString n = name.trimmed().toLower();
    if      (n == "press_ya" || n == "ya1")     play(Sound::PressYa, volume);
    else if (n == "release_ya" || n == "ya2")   play(Sound::ReleaseYa, volume);
    else if (n == "press_d" || n == "d1")       play(Sound::PressD, volume);
    else if (n == "release_d" || n == "d2")    play(Sound::ReleaseD, volume);
    else if (n == "task_end_a" || n == "exp")   play(Sound::TaskEndA, volume);
    else if (n == "task_end_b")                 play(Sound::TaskEndB, volume);
    else if (n == "notify")                     play(Sound::Notify, volume);
}
