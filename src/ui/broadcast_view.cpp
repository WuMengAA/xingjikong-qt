// 屏幕广播 · 被控端全屏显示实现。
#include "broadcast_view.h"

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QPixmap>
#include <QDebug>
#include <windows.h>

BroadcastView &BroadcastView::inst()
{
    static BroadcastView s;
    return s;
}

BroadcastView::BroadcastView() = default;

void BroadcastView::ensureWindow()
{
    if (m_window) return;
    m_window = new QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_window->setAttribute(Qt::WA_DeleteOnClose, false);
    m_window->setStyleSheet(QStringLiteral(
        "background:#0A0E1A; color:#F1F5F9;"
        "QWidget { background:#0A0E1A; }"
        "QLabel#hint { font-size:14px; color:#94A3B8; background:transparent; }"));

    m_image = new QLabel(m_window);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setScaledContents(true);  // 拉伸填满
    m_image->setStyleSheet(QStringLiteral("background:#000; border:none;"));

    auto *hint = new QLabel(QStringLiteral("正在接收老师屏幕…"), m_window);
    hint->setObjectName(QStringLiteral("hint"));
    hint->setAlignment(Qt::AlignCenter);

    auto *lay = new QVBoxLayout(m_window);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_image, 1);
    lay->addWidget(hint);
    lay->setContentsMargins(0, 0, 0, 24);

    if (QScreen *s = QGuiApplication::primaryScreen())
        m_window->setGeometry(s->geometry());
    else
        m_window->setGeometry(0, 0, 1280, 720);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
    if (HWND h = (HWND)m_window->winId())
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
}

void BroadcastView::showFrame(const QByteArray &jpeg)
{
    if (!m_active) m_active = true;
    ensureWindow();
    if (!m_image) return;
    QPixmap pm;
    if (pm.loadFromData(jpeg)) m_image->setPixmap(pm);
}

void BroadcastView::stop()
{
    if (!m_active) return;
    m_active = false;
    if (m_window) {
        m_window->close();
        m_window->deleteLater();
        m_window = nullptr;
        m_image = nullptr;
    }
    qInfo("[bcast] 屏幕广播结束，学生恢复");
}
