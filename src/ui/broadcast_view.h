// 屏幕广播 · 被控端全屏显示（设计文档《屏幕广播-第一版设计》2026-10-07）
//
// 老师屏幕 JPEG 帧 → 全屏置顶窗口显示（深色底 + 画面拉伸 + 底部"正在接收老师屏幕"提示）。
// 复用 exam_mode 的全屏窗口思路（无边框 + 置顶），但内容是动态画面不是倒计时。
// 收到 broadcast.stop / 停止广播 3 秒无帧 → 自动关闭。

#ifndef BROADCAST_VIEW_H
#define BROADCAST_VIEW_H

#include <QObject>
#include <QByteArray>
#include <QLabel>

class BroadcastView : public QObject
{
    Q_OBJECT
public:
    static BroadcastView &inst();

    /** 显示一帧 JPEG（老师屏幕）。窗口会自动创建/保持。 */
    void showFrame(const QByteArray &jpeg);
    /** 结束广播：关闭全屏窗口，学生恢复。 */
    void stop();

    bool active() const { return m_active; }

private:
    BroadcastView();
    void ensureWindow();

    QWidget *m_window = nullptr;
    QLabel *m_image = nullptr;
    bool m_active = false;
};

#endif // BROADCAST_VIEW_H
