// Android 系统通知栏（2026-10-11）——被控端回复等事件在**系统通知栏**弹提醒。
//
// 背景：管理端通知此前只走应用内 DynamicIsland（QML showNote）；手机 App 切到
// 后台或息屏时看不到。这里用 Android NotificationManager 补一条系统横幅通知。
//
// 用 QJniObject（Qt Core public JNI API）直接调 android.app.Notification：
//   · API 26+ 用 NotificationChannel + Notification.Builder(Context, channelId)
//   · 真机 OPPO（API 34）走这条；minSdk 21 老设备 Builder(context, channelId)
//     抛异常就静默放弃（通知不是关键路径，不为此升级 minSdk）。
// 一体化：不做 PendingIntent（App 在前台使用，横幅提醒即达意；点了自动取消）。

#include "android_notify.h"

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QtCore/private/qandroidextras_p.h>   // QtAndroidPrivate::requestPermission（POST_NOTIFICATIONS）
#include <QtCore/qcoreapplication_platform.h>   // QNativeInterface::QAndroidApplication::context()
#include <QDebug>

namespace {
constexpr const char *kChannelId = "stelarith_msg";
constexpr jint kNotifyId = 1;
bool g_postPermissionRequested = false;   // Android 13+ 首次弹授权框，只请求一次
}

void androidSystemNotify(const QString &title, const QString &text)
{
    // Android 13+（API 33）通知栏横幅必须运行期授权；未授权则 NotificationManager 静默丢弃。
    // 首次弹框（fire-and-forget：授权即显示，拒绝也无害——下次再弹）。
    if (!g_postPermissionRequested) {
        g_postPermissionRequested = true;
        QtAndroidPrivate::requestPermission(QStringLiteral("android.permission.POST_NOTIFICATIONS"));
    }

    const QJniObject ctx(QNativeInterface::QAndroidApplication::context().object());
    if (!ctx.isValid()) {
        qWarning("[viewer] Android 通知：拿不到 Activity context");
        return;
    }
    QJniObject nm = ctx.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
        QJniObject::fromString("notification").object<jstring>());
    if (!nm.isValid()) return;

    // 频道（API 26+；真机 API 34 必须。老设备（minSdk 21）会抛 —— 捕获后走 Builder 旧重载）
    QJniObject channel("android/app/NotificationChannel",
                       "(Ljava/lang/String;Ljava/lang/CharSequence;I)V",
                       QJniObject::fromString(QLatin1String(kChannelId)).object<jstring>(),
                       QJniObject::fromString(QStringLiteral("星集控消息")).object<jstring>(),
                       jint(3));   // IMPORTANCE_HIGH
    if (channel.isValid())
        nm.callMethod<void>("createNotificationChannel",
                            "(Landroid/app/NotificationChannel;)V", channel.object());

    QJniObject builder("android/app/Notification$Builder",
                       "(Landroid/content/Context;Ljava/lang/String;)V",
                       ctx.object(),
                       QJniObject::fromString(QLatin1String(kChannelId)).object<jstring>());
    if (!builder.isValid()) {
        // 老设备兜底：Builder(Context) 无 channel
        builder = QJniObject("android/app/Notification$Builder",
                             "(Landroid/content/Context;)V", ctx.object());
        if (!builder.isValid()) return;
    }

    // 小图标：用应用自己的 icon（资源 id）
    QJniObject ai = ctx.callObjectMethod("getApplicationInfo",
                                         "()Landroid/content/pm/ApplicationInfo;");
    if (ai.isValid()) {
        const jint icon = ai.getField<jint>("icon");
        builder.callMethod<void>("setSmallIcon", "(I)V", icon);
    }
    builder.callMethod<void>("setContentTitle", "(Ljava/lang/CharSequence;)V",
                             QJniObject::fromString(title).object<jstring>());
    builder.callMethod<void>("setContentText", "(Ljava/lang/CharSequence;)V",
                             QJniObject::fromString(text).object<jstring>());
    builder.callMethod<void>("setAutoCancel", "(Z)V", jboolean(true));

    const QJniObject notif = builder.callObjectMethod("build", "()Landroid/app/Notification;");
    if (notif.isValid())
        nm.callMethod<void>("notify", "(ILandroid/app/Notification;)V",
                            kNotifyId, notif.object());
}
#else
// 桌面有 QSystemTrayIcon + 灵动岛，不需要系统通知栏；留空桩（调用处 #ifdef 掉即可）。
void androidSystemNotify(const QString &, const QString &) {}
#endif