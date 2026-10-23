#include "viewerbackend.h"
#include "class/class_service.h"

// 班级域入站分发（#460 · 2026-10-23 · viewerbackend P1 拆分第一刀）
//
// onTextMessage 的 else-if 链只做"是不是班级域的"判断（一个条件五个 type），
// 真正的分派在这一处 —— 与 viewerbackend_file/voice/account 同款拆分范式：
// viewerbackend.cpp 的增长止步于路由，业务进独立 TU。
//
// 班级归属云端已判（canAccess + knownDevices），到这里只剩"装配本地模型"，
// 因此不做任何权限/班级校验（再判一份就是两口径，必漂）。
void ViewerBackend::onClassDomainMessage(const QString &type, const QJsonObject &pay)
{
    if (!m_classService) return;
    if (type == QLatin1String("class-subscribed")) {
        m_classService->onClassSubscribed(pay);
    } else if (type == QLatin1String("class-unsubscribed")) {
        m_classService->onClassUnsubscribed(pay);
    } else if (type == QLatin1String("class.event")) {
        m_classService->onClassEvent(pay);
    } else if (type == QLatin1String("class.member.online")) {
        m_classService->onClassMemberOnline(pay);
    } else if (type == QLatin1String("class.member.offline")) {
        m_classService->onClassMemberOffline(pay);
    }
}
