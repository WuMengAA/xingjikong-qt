// 小鲸鱼音效播放器（2026-10-11）
//
// dsh-whale-widget 的音效素材已 cp 到 resources/whale/ 并转 wav：
//   ya1.wav / ya2.wav  = 按压 / 松开（小黄鸭）
//   d1.wav / d2.wav    = 按压 / 松开（音效1）
//   minecraft-exp-orb.wav = 任务结束音（Minecraft 经验球）
//   task-end-a.wav        = 任务结束音（预设 A）
//
// Qt 6.12 编译时没装 Multimedia 模块（lib/cmake 下无 Qt6Multimedia），
// 所以走 Windows 原生 winmm.dll 的 PlaySoundW：
//   · SND_FILENAME  从文件路径播
//   · SND_ASYNC     不阻塞（播完自动返回，不需要回调）
//   · SND_NODEFAULT 找不到文件不播默认 beep
//
// 素材走 qrc：:/whale/<name>.wav。PlaySound 不认 qrc，所以先从 QResource
// 读出字节、写到 %TEMP% 下的临时 wav 再播。临时文件名固定（避免每次新建一个、
// 教室大屏跑一天 C 盘满了）。

#ifndef WHALE_AUDIO_H
#define WHALE_AUDIO_H

#include <QObject>
#include <QString>

class WhaleAudio : public QObject
{
    Q_OBJECT
public:
    enum class Sound {
        PressYa,     // ya1.wav 按压音（小黄鸭）
        ReleaseYa,   // ya2.wav 松开音（小黄鸭）
        PressD,      // d1.wav  按压音（音效1）
        ReleaseD,    // d2.wav  松开音（音效1）
        TaskEndA,    // minecraft-exp-orb.wav
        TaskEndB,    // task-end-a.wav
        Notify       // minecraft-exp-orb.wav（通知上岛提示音，与 TaskEndA 同文件但语义不同）
    };
    Q_ENUM(Sound)

    static WhaleAudio *instance();

    // 播一个音效。volume 0.0~1.0（winmm 不支持音量，靠系统音量管，
    // 这个参数留接口位、当前实现忽略它）。
    Q_INVOKABLE void play(Sound s, qreal volume = 1.0);

    // QML 侧用名字播（避免 QML 里直接用枚举的跨版本坑）
    Q_INVOKABLE void playByName(const QString &name, qreal volume = 1.0);

private:
    explicit WhaleAudio(QObject *parent = nullptr);
    QString extractToTemp(const QString &qrcPath) const;
    QString qrcPath(Sound s) const;
};

#endif // WHALE_AUDIO_H
