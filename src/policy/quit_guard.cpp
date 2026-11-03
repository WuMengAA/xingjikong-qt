// 退出守卫实现（2026-11-03 · 用户工作笔记序号 2）。设计与口径见 quit_guard.h 头注释。
//
// 为什么是 SHA-256(salt + 口令) 而不是明文：教室机是学生能摸到的机器，
// QSettings 就是注册表里一个可读的键，明文口令等于当众写着"别的班也能退"。
// 固定盐不是机密，只用来挡彩虹表与"同一口令在每台机器上哈希相同"这两件事；
// **换盐 = 所有已设口令失效（回落到未启用）**，所以这里写死不要动。
#include "quit_guard.h"

#include <QCryptographicHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>

namespace {
constexpr char kKey[] = "agent/quitPassword";
constexpr char kSalt[] = "Stelarith/QuitGuard/v1";

QString hashOf(const QString &plain)
{
    const QByteArray raw = QByteArray(kSalt) + plain.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex());
}

QString storedHash()
{
    QSettings s;
    return s.value(QString::fromUtf8(kKey)).toString();
}
}   // namespace

namespace QuitGuard {

bool hasPassword()
{
    return !storedHash().isEmpty();
}

void setPassword(const QString &plain)
{
    QSettings s;
    if (plain.isEmpty()) {
        s.remove(QString::fromUtf8(kKey));       // 空串 = 清除（回到"退出不要口令"）
    } else {
        s.setValue(QString::fromUtf8(kKey), hashOf(plain));
    }
    s.sync();
}

bool confirmQuit()
{
    if (!hasPassword())
        return true;                             // 默认不打扰：没设口令的存量机器行为不变

    bool ok = false;
    const QString entered = QInputDialog::getText(
        nullptr,
        QStringLiteral("退出被控端"),
        QStringLiteral("这台设备设了退出口令，输入口令后才能退出："),
        QLineEdit::Password, QString(), &ok);
    if (!ok)
        return false;                            // 点了取消 / 关掉对话框 —— 不退出
    if (hashOf(entered) == storedHash())
        return true;

    QMessageBox::warning(
        nullptr, QStringLiteral("退出口令不对"),
        QStringLiteral("口令不正确，本次不退出。\n"
                       "忘记口令请让管理员清除 QSettings 键 agent/quitPassword。"));
    return false;
}

}   // namespace QuitGuard
