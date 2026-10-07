// 星集控被控端 · 灵动岛（2026-10-07，设计文档 3.8 被控端版）
//
// 需求（用户 2026-10-07）：被控端的活动（通知 / 语音 / 广播 / 考试 / 录像等）
// **全部经过灵动岛**——顶部居中胶囊，形状与大小、位置按状态转变（灵动），
// 带缓动动画；信息展示 + 交互穿透（点击可穿透到下层，不挡操作）；
// 深色 / 浅色两套配色。
//
// 本组件是 QWidget 实现（被控端 Qt Widgets 栈，不引 QML）：
//   · 顶部居中，随内容宽度自适应；高度 56px 胶囊 ↔ 168px 展开，QPropertyAnimation 缓动
//   · 深/浅色由系统（Qt::colorScheme）或调用方 setLightMode() 决定
//   · 默认交互穿透（WA_TransparentForMouseEvents）；需要点按（如确认/查看）时
//     调用方 setInteractive(true) 关闭穿透
//   · 自动收起：时长到后淡出；紧急/交互模式不自动关
//
// 与 NotifyWindow 的关系：NotifyWindow 保留（全屏遮罩/居中弹窗/紧急确认仍在它那里），
// 灵动岛接管 **island 形态** 与各类瞬态提示（语音/广播/考试倒计时等）——见 island.cpp。

#ifndef STELARITH_ISLAND_H
#define STELARITH_ISLAND_H

#include <QWidget>
#include <QString>
#include <QPointer>
#include <QTimer>
#include <QPropertyAnimation>

class QMouseEvent;
class QPaintEvent;

class IslandOverlay : public QWidget
{
    Q_OBJECT
public:
    // 单例：被控端全局唯一（同时间只显示一个灵动岛，新状态顶掉旧的）
    static IslandOverlay *instance();

    enum Shape { Collapsed = 56, Expanded = 168 };

    // 显示一条（icon 为 emoji/单字符；title 主文案，desc 次文案）
    void showIsland(const QString &title, const QString &desc = QString(),
                    const QString &icon = QString(),
                    int durationMs = 5000);
    // 切换展开（点击展开看详情）/ 收起
    void setExpanded(bool on);
    bool isExpanded() const { return m_expanded; }

    // 深/浅色：true=浅色（白底黑字），false=深色（黑底白字，默认）
    void setLightMode(bool light);
    bool lightMode() const { return m_light; }

    // 交互穿透：true=点击穿透到下层应用（纯展示，默认）；false=本岛接收鼠标（可点确认/查看）
    void setInteractive(bool on);
    bool interactive() const { return m_interactive; }

    // 立即隐藏（不再等自动收起）
    void dismiss();

signals:
    void expandedChanged(bool expanded);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    explicit IslandOverlay(QWidget *parent = nullptr);
    ~IslandOverlay() override;

    void applyTheme();         // 按深浅色换算 QSS 背景/前景
    void animateGeometry(const QRect &target);   // 弹性缓动改位置/大小
    void positionForCurrent(); // 顶部居中（含安全区偏移）

    QString m_title;
    QString m_desc;
    QString m_icon;
    int m_durationMs = 5000;
    bool m_expanded = false;
    bool m_light = false;
    bool m_interactive = false;   // 默认穿透

    QTimer *m_autoClose = nullptr;
    QPropertyAnimation *m_geoAnim = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;

    friend class IslandPrivate;
};

#endif // STELARITH_ISLAND_H