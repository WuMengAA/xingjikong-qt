#include "notification_gate.h"

NotificationGate::NotificationGate(QObject *parent) : QObject(parent) {}

void NotificationGate::setExamActive(bool on)
{
    if (m_examActive == on) return;
    m_examActive = on;
    emit examStateChanged(on);
}

QString NotificationGate::kindName(NotificationGate::Kind k)
{
    switch (k) {
    case Island:      return QStringLiteral("island");
    case Danmaku:     return QStringLiteral("danmaku");
    case Fullscreen:  return QStringLiteral("fullscreen");
    case NeedConfirm: return QStringLiteral("needConfirm");
    case LoopRemind:  return QStringLiteral("loopRemind");
    case Snapshot:    return QStringLiteral("snapshot");
    case Camera:      return QStringLiteral("camera");
    case AudioCapture:return QStringLiteral("audioCapture");
    default:          return QStringLiteral("?");
    }
}